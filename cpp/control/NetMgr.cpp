/** @file NetMgr.cpp 
 *
 *  @author Jon Turner
 *  @date 2011
 *  This is open source software licensed under the Apache 2.0 license.
 *  See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#include "NetMgr.h"

using namespace forest;

/** usage:
 *       NetMgr topoFile prefixFile finTime
 * 
 *  The first argument is the topology file (aka NetInfo file) that
 *  describes the network topology.
 *  PrefixFile is a file that maps IP prefixes of clients to forest routers.
 *  FinTime is the number of seconds to run.
 *  If zero, the NetMgr runs forever.
 */
int main(int argc, char *argv[]) {
	uint32_t finTime = 0;

	if (argc != 4 ||
	     sscanf(argv[3],"%d", &finTime) != 1)
		Util::fatal("usage: NetMgr topoFile prefixFile finTime");
	if (!NetMgr::init(argv[1], argv[2], finTime)) {
		Util::fatal("NetMgr: initialization failure");
	}
	NetMgr::runAll();
	exit(0);
}

namespace forest {

/** Initialization of NetMgr.
 *  @param topoFile is the name of the topology file
 *  @param pfxFile is the name of the prefix file
 *  @param finTime is the number of seconds to run (if zero, run forever)
 *  @return true on success, false on failure
 */
bool NetMgr::init(const char *topoFile, const char *pfxFile, int finTime) {
	int nPkts = 10000;
	ps = new PacketStore(nPkts+1);
	logger = new Logger();

	if (!readPrefixInfo(pfxFile)) return false;

	// setup table of network administrators
	admTbl = new AdminTable(100);

	dummyRecord = new char[RECORD_SIZE];
	for (char *p = dummyRecord; p < dummyRecord+RECORD_SIZE; p++) *p = ' ';
	dummyRecord[0] = '-'; dummyRecord[RECORD_SIZE-1] = '\n';

	maxRecord = 0;
	// read adminData file
	adminFile.open("adminData");
	if (!adminFile.good() || !admTbl->read(adminFile)) {
		logger->log("NetMgr::init: could not read adminData "
			    "file",2);
		return false;
	}
	adminFile.clear();
	// if size of file is not equal to count*RECORD_SIZE
	// re-write the file using padded records
	int n = admTbl->getMaxAdx();
	adminFile.seekp(0,ios_base::end);
	long len = adminFile.tellp();
	if (len != (n+1)*RECORD_SIZE) {
		for (int adx = 0; adx <= n; adx++) writeAdminRecord(adx);
	}

	// read NetInfo data structure from file
	int maxNode = 100000; int maxLink = 10000; int maxRtr = 5000; 
	int maxComtree = 10000;
	net = new NetInfo(maxNode, maxLink, maxRtr);
	comtrees = new ComtInfo(maxComtree,*net);

	ifstream fs; fs.open(topoFile);
	if (fs.fail() || !net->read(fs) || !comtrees->read(fs)) {
		cerr << "NetMgr::init: could not read topology file, or error "
		      	"in topology file\n";
		return false;
	}
	fs.close();

	// mark all routers as DOWN
	for (int rtr = net->firstRouter(); rtr != 0;
		 rtr = net->nextRouter(rtr)) {
		net->setStatus(rtr,NetInfo::DOWN);
	}

	// find node information for netMgr and cliMgr and initialize
	// associated data
	myAdr = 0; cliMgrAdr = comtCtlAdr = 0;
	ipa_t rtrIp;
	for (int c = net->firstController(); c != 0;
		 c = net->nextController(c)) {
		net->setStatus(c,NetInfo::DOWN);
		if (net->getNodeName(c) == "netMgr") {
			myIp = net->getLeafIpAdr(c);
			myAdr = net->getNodeAdr(c);
			int lnk = net->firstLinkAt(c);
			int rtr = net->getPeer(c,lnk);
			int llnk = net->getLLnum(lnk,rtr);
			int iface = net->getIface(llnk,rtr);
			if (iface == 0) {
				cerr << "NetMgr:init: can't find ip address "
				     << "of access router\n";
			}
			netMgr = c; nmRtr = rtr;
			rtrIp = net->getIfIpAdr(rtr,iface);
			rtrAdr = net->getNodeAdr(rtr);
		} else if (net->getNodeName(c) == "cliMgr") {
			cliMgrAdr = net->getNodeAdr(c);
		} else if (net->getNodeName(c) == "comtCtl") {
			comtCtlAdr = net->getNodeAdr(c);
		}
	}

	if (myAdr == 0 || cliMgrAdr == 0) {
		cerr << "could not find netMgr or cliMgr in topology file\n";
		return false;
	}

	ipp_t rtrPort = 0; // until router boots
	uint64_t nonce = 0; // likewise

	// create per-thread NetMgr objects and resize share output queue
	tpool = new NetMgr[numThreads+1];
	NetMgr::outq.resize(2*numThreads);

	// create and intitialize Substrate
	sub = new Substrate(numThreads, tpool, ps, logger);
	if (!sub->init(myAdr, myIp, rtrAdr, rtrIp, rtrPort, nonce,
		       Forest::NM_PORT, Forest::NM_PORT, finTime)) {
		logger->log("init: can't initialize substrate",2);
		return false;
	}
	sub->setRtrReady(false);

	return true;
}

/** Cleanup various data structures. */
void NetMgr::cleanup() {
	delete ps; delete net; delete comtrees; delete sub;
}

/** Start all the NetMgr threads, then start the Substrate and
 *  wait for it to finish.
 */
bool NetMgr::runAll() {
	// start NetMgr threads (uses Controller::start)
	for (int i = 1; i <= numThreads; i++) {
		tpool[i].thred = thread(Controller::start, &tpool[i], i, 100);
	}
	// start substrate and wait for it to finish
	sub->run();

	// and done
	cleanup();
	return true;
}

/** Worker thread.
 *  This method is run as a separate thread and does not terminate
 *  until the process does. It communicates with a Substrate thread
 *  through a pair of queues, passing packet indexes back and forth
 *  across the thread boundary. The queues are implemented in the Controller
 *  class, which is a superclass of NetMgr. Note that there is a separate
 *  input queue for each thread, but a shared output queue.
 *  Whenever a packet index is passed through its queue to
 *  a handler, the handler "owns" the corresponding packet and can
 *  read/write it without locking. The handler is required free any
 *  packets that it no longer needs to the packet store.
 *  When the handler has completed the requested operation, it sends
 *  a 0 value back to the main thread, signalling that it is available
 *  to handle another task.
 */
bool NetMgr::run() {
	while (true) {
		pktx px = inq.deq();
		bool success = false;
		if (px < 0) {
			// in this case, p is really a negated socket number
			// for a remote client
			int sock = -px;
			success = handleConsole(sock);
		} else {
			Packet& p = ps->getPacket(px);
			CtlPkt cp(p);
			switch (cp.type) {
			case CtlPkt::CLIENT_CONNECT:
			case CtlPkt::CLIENT_DISCONNECT:
				success = handleConDisc(px,cp);
				break;
			case CtlPkt::NEW_SESSION:
				success = handleNewSession(px,cp);
				break;
			case CtlPkt::CANCEL_SESSION:
				success = handleCancelSession(px,cp);
				break;
			case CtlPkt::BOOT_LEAF:
				// allow just one node to boot at a time
				net->lock();
				success = handleBootLeaf(px,cp);
				net->unlock();
				break;
			case CtlPkt::BOOT_ROUTER:
				// allow just one node to boot at a time
				net->lock();
				success = handleBootRouter(px,cp);
				net->unlock();
				break;
			default:
				errReply(px,cp, "invalid control packet "
						"type for NetMgr");
				break;
			}
		}
		if (!success) {
			cerr << "NetMgr::run: operation failed\n"
			     << ps->getPacket(px).toString();
		}
		ps->free(px); // release px now that we're done
		outq.enq(pair<int,int>(0,myThx)); // signal completion to
						  // substrate thread
	}
	return true;
}

/** Send a request packet and return the reply.
 *  @param px is the packet number of the packet to be sent
 *  @param len is the length of the packet's payload
 *  @param dest is the destinatin address it is to be sent to
 *  @param opx is the packet number of the original request that started
 *  the operation being processed, or zero
 *  @param s is an optional string to be included in an error reply, that
 *  should be sent to the host that made the original request, if we do not
 *  get a positive reply; no error reply is sent when opx == 0
 *  @param return the packet index of the reply, if it is a positive reply;
 *  otherwise return 0 after freeing the packet
 */
pktx NetMgr::sendRequest(pktx px, int len, fAdr_t dest,
			 pktx opx, const string& s) {
	Packet& p = ps->getPacket(px);
	p.length = Forest::OVERHEAD + len;
	p.type = Forest::NET_SIG; p.flags = 0;
	p.dstAdr = dest; p.srcAdr = myAdr;
	p.pack(); p.hdrErrUpdate(); p.payErrUpdate();

	outq.enq(pair<int,int>(px,myThx)); // send request through substrate
	pktx rx = inq.deq();	// and get reply back

	CtlPkt cr(ps->getPacket(rx));
	if (cr.mode == CtlPkt::POS_REPLY) return rx;

	if (opx != 0) {
		CtlPkt cop(ps->getPacket(opx));
		if (cr.mode == CtlPkt::NO_REPLY) {
			errReply(opx,cop,s + " (no response from target)");
		} else {
			string s1; cr.xtrError(s1);
			errReply(opx,cop,s + " (" + s1 + ")");
		}
	}
	ps->free(rx);
	return 0;
}

/** Send a request packet and return the reply.
 *  This version omits the last two arguments of the full version.
 *  @param px is the packet number of the packet to be sent
 *  @param len is the length of the packet's payload
 *  @param dest is the destinatin address it is to be sent to
 *  @param return the packet index of the reply, if it is a positive reply;
 *  otherwise return 0 after freeing the packet
 */
pktx NetMgr::sendRequest(pktx px, int len, fAdr_t dest) {
	return sendRequest(px,len,dest,0,"");
}

/** Send a reply packet back through the substrate.
 *  @param px is the index of the packet to be sent
 *  @param len is the length of the packet's payload
 *  @param dest is the destination address for the packet
 */
void NetMgr::sendReply(pktx px, int len, fAdr_t dest) {
	Packet& p = ps->getPacket(px);
	p.length = Forest::OVERHEAD + len;
	p.type = Forest::NET_SIG; p.flags = 0;
	p.dstAdr = dest; p.srcAdr = myAdr;
	p.pack(); p.hdrErrUpdate(); p.payErrUpdate();
	outq.enq(pair<int,int>(px,myThx));
}

/** Format and send an error reply packet.
 *  @param px is the packet number of the packet being replied to
 *  @param cp is the control packet unpacked from px
 *  @param s is a string to be included in the reply packet
 */
void NetMgr::errReply(pktx px, CtlPkt& cp, const string& s) {
	pktx ex = ps->fullCopy(px); Packet& e = ps->getPacket(ex);
	CtlPkt ce(e);
	ce.fmtError("operation failed [" + s + "]", cp.seqNum);
	sendReply(ex, ce.paylen, ps->getPacket(px).srcAdr);
}

/** Handle a connection from a remote console.
 *  Interprets variety of requests from a remote console,
 *  including requests to login, get information about routers,
 *  monitor packet flows and so forth.
 *  @param sock is a socket number for an open stream socket
 *  @return true if the interaction proceeds normally, followed
 *  by a normal close; return false if an error occurs
 */
bool NetMgr::handleConsole(int sock) {
/* omit for now
	NetBuffer buf(sock,1024);
	string cmd, reply, adminName;
	bool loggedIn;

	// verify initial "greeting"
	if (!buf.readLine(cmd) || cmd != "Forest-Console-v1") {
		Np4d::sendString(sock,"misformatted initial dialog\n"
				      "overAndOut\n");
		return false;
	}
	// main processing loop for requests from client
	loggedIn = false;
	while (buf.readAlphas(cmd)) {
cerr << "cmd=" << cmd << endl;
		reply.assign("success\n");
		if (cmd == "over") {
			// shouldn't happen, but ignore it, if it does
			buf.nextLine(); continue;
		} else if (cmd == "overAndOut") {
			buf.nextLine(); return true;
		} else if (cmd == "login") {
			loggedIn = login(buf,adminName,reply);
		} else if (!loggedIn) {
			continue; // must login before anything else
		} else if (cmd == "newAccount") {
			newAccount(buf,adminName,reply);
		} else if (cmd == "getProfile") {
			getProfile(buf,adminName,reply);
		} else if (cmd == "updateProfile") {
			updateProfile(buf,adminName,reply);
		} else if (cmd == "changePassword") {
			changePassword(buf,adminName,reply);
		} else if (cmd == "getNet") {
			string s; net->toString(s);
			reply.append(s);
		} else if (cmd == "getLinkTable") {
			getLinkTable(buf,reply, cph);
		} else if (cmd == "getComtreeTable") {
			getComtreeTable(buf,reply, cph);
		} else if (cmd == "getIfaceTable") {
			getIfaceTable(buf,reply);
		} else if (cmd == "getRouteTable") {
			getRouteTable(buf,reply);
		} else if (cmd == "addFilter") {
			addFilter(buf,reply);
		} else if (cmd == "modFilter") {
			modFilter(buf,reply);
		} else if (cmd == "dropFilter") {
			dropFilter(buf,reply);
		} else if (cmd == "getFilter") {
			getFilter(buf,reply);
		} else if (cmd == "getFilterSet") {
			getFilterSet(buf,reply);
		} else if (cmd == "getLoggedPackets") {
			getLoggedPackets(buf,reply);
		} else if (cmd == "enableLocalLog") {
			enableLocalLog(buf,reply);
		} else {
			reply = "unrecognized input\n";
		}
cerr << "sending reply: " << reply;
		reply.append("over\n");
		Np4d::sendString(sock,reply);
	}
	return true;
cerr << "terminating" << endl;
*/
	return true;
}


// omit console code for now

// /** Handle a login request.
//  *  Reads from the socket to identify admin and obtain password
//  *  @param buf is a NetBuffer associated with the stream socket to an admin
//  *  @param adminName is a reference to a string in which the name of the
//  *  admin is returned
//  *  @param reply is a reference to a string in which an error message may be
//  *  returned if the operation does not succeed.
//  *  @return true on success, else false
//  */
// bool login(NetBuffer& buf, string& adminName, string& reply) {
// 	string pwd, s1, s2;
// 	if (buf.verify(':') && buf.readName(adminName) && buf.nextLine() &&
// 	    buf.readAlphas(s1) && s1 == "password" &&
// 	      buf.verify(':') && buf.readWord(pwd) && buf.nextLine() &&
// 	    buf.readLine(s2) && s2 == "over") {
// 		int adx =admTbl->getAdmin(adminName);
// 		// locks adx
// 		if (adx == 0) {
// 			reply = "login failed: try again\n";
// 			return false;
// 		} else if (admTbl->checkPassword(adx,pwd)) {
// 			admTbl->releaseAdmin(adx);
// 			return true;
// 		} else {
// 			admTbl->releaseAdmin(adx);
// 			reply = "login failed: try again\n";
// 			return false;
// 		}
// 	} else {
// 		reply = "misformatted login request\n";
// 		return false;
// 	}
// }
// 
// /** Handle a new account request.
//  *  Reads from the socket to identify admin and obtain password
//  *  @param buf is a NetBuffer associated with the stream socket to an admin
//  *  @param adminName is a reference to a string in which the name of the
//  *  admin is returned
//  *  @param reply is a reference to a string in which an error message may be
//  *  returned if the operation does not succeed.
//  *  @return true on success, else false
//  */
// bool newAccount(NetBuffer& buf, string& adminName, string& reply) {
// 	string newName, pwd, s1, s2;
// 	if (buf.verify(':') && buf.readName(newName) && buf.nextLine() &&
// 	    buf.readAlphas(s1) && s1 == "password" &&
// 	      buf.verify(':') && buf.readWord(pwd) && buf.nextLine() &&
// 	    buf.readLine(s2) && s2 == "over") {
// 		int adx =admTbl->getAdmin(newName);
// 		// locks adx
// 		if (adx != 0) {
// 			admTbl->releaseAdmin(adx);
// 			reply = "name not available, select another\n";
// 			return false;
// 		}
// 		adx = admTbl->addAdmin(newName,pwd);
// 		if (adx != 0) {
// 			writeAdminRecord(adx);
// 			admTbl->releaseAdmin(adx);
// 			return true;
// 		} else {
// 			reply = "unable to add admin\n";
// 			return false;
// 		}
// 	} else {
// 		reply = "misformatted new admin request\n";
// 		return false;
// 	}
// }
// 
// /** Handle a get profile request.
//  *  Reads from the socket to identify the target admin.
//  *  @param buf is a NetBuffer associated with the stream socket to an admin
//  *  @param adminName is the name of the currently logged-in admin
//  *  @param reply is a reference to a string in which an error message may be
//  *  returned if the operation does not succeed.
//  */
// void getProfile(NetBuffer& buf, string& adminName, string& reply) {
// 	string s, targetName;
// 	if (!buf.verify(':') || !buf.readName(targetName) ||
// 	    !buf.nextLine() || !buf.readLine(s) || s != "over") {
// 		reply = "misformatted get profile request"; return;
// 	}
// 	int tadx = admTbl->getAdmin(targetName);
// 	reply  = "realName: \"" + admTbl->getRealName(tadx) + "\"\n";
// 	reply += "email: " + admTbl->getEmail(tadx) + "\n";
// 	admTbl->releaseAdmin(tadx);
// }
// 
// /** Handle an update profile request.
//  *  Reads from the socket to identify the target admin. This operation
//  *  requires either that the target admin is the currently logged-in
//  *  admin, or that the logged-in admin has administrative privileges.
//  *  @param buf is a NetBuffer associated with the stream socket to an admin
//  *  @param adminName is the name of the currently logged-in admin
//  *  @param reply is a reference to a string in which an error message may be
//  *  returned if the operation does not succeed.
//  */
// void updateProfile(NetBuffer& buf, string& adminName, string& reply) {
// 	string s, targetName;
// 	if (!buf.verify(':') || !buf.readName(targetName) ||
// 	    !buf.nextLine()) {
// 		reply = "misformatted updateProfile request\n"; return;
// 	}
// 	// read profile information from admin
// 	string item; RateSpec rates;
// 	string realName, email; 
// 	while (buf.readAlphas(item)) {
// 		if (item == "realName" && buf.verify(':') &&
// 		    buf.readString(realName) && buf.nextLine()) {
// 			// that's all for now
// 		} else if (item == "email" && buf.verify(':') &&
// 		    buf.readWord(email) && buf.nextLine()) {
// 			// that's all for now
// 		} else if (item == "over" && buf.nextLine()) {
// 			break;
// 		} else {
// 			reply = "misformatted update profile request (" +
// 				item + ")\n";
// 			return;
// 		}
// 	}
// 	int tadx = admTbl->getAdmin(targetName);
// 	if (realName.length() > 0) admTbl->setRealName(tadx,realName);
// 	if (email.length() > 0) admTbl->setEmail(tadx,email);
// 	writeAdminRecord(tadx);
// 	admTbl->releaseAdmin(tadx);
// 	return;
// }
// 
// /** Handle a change password request.
//  *  Reads from the socket to identify the target admin. This operation
//  *  requires either that the target admin is the currently logged-in
//  *  admin, or that the logged-in admin has administrative privileges.
//  *  @param buf is a NetBuffer associated with the stream socket to an admin
//  *  @param adminName is the name of the currently logged-in admin
//  *  @param reply is a reference to a string in which an error message may be
//  *  returned if the operation does not succeed.
//  */
// void changePassword(NetBuffer& buf, string& adminName, string& reply) {
// 	string targetName, pwd;
// 	if (!buf.verify(':') || !buf.readName(targetName) ||
// 	    !buf.readWord(pwd) || !buf.nextLine()) {
// 		reply = "misformatted change password request\n"; return;
// 	}
// 	int tadx = admTbl->getAdmin(targetName);
// 	admTbl->setPassword(tadx,pwd);
// 	writeAdminRecord(tadx);
// 	admTbl->releaseAdmin(tadx);
// 	return;
// }
// 
// /** Allocate log filter in router and return filter index to Console.
//  *  Reads router name from the socket.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void addFilter(NetBuffer& buf, string& reply, CpHandler& cph) {
// 	string rtrName; int rtr;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 	    (rtr = net->getNodeNum(rtrName)) == 0) {
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	pktx repx; CtlPkt repCp;
// 	repCp.reset();
// 	repx = cph.addFilter(radr, repCp);
// 	if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 		reply.assign("could not add log filter\n"); return;
// 	}
// 	stringstream ss; ss << repCp.index1;
// 	reply.append(ss.str());
// 	//string index = to_string(repCp.index1);
// 	//reply.append(index);
// 	reply.append("\n");
// }
// 
// /** Modify log filter in router.
//  *  Reads router name and filter index from the socket.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void modFilter(NetBuffer& buf, string& reply, CpHandler& cph){
// 	string rtrName; int rtr, fx = -1;
// 	string filterString;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 			((rtr = net->getNodeNum(rtrName)) == 0) || 
// 			!buf.readInt(fx) || (fx < 0) ||
// 			!buf.nextLine() || !buf.readLine(filterString)){
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	pktx repx; CtlPkt repCp;
// 	repCp.reset();
// 	repx = cph.modFilter(radr, fx, filterString, repCp);
// 	if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 		reply.assign("could not modify log filter\n"); return;
// 	}
// }
// 
// /** Drop log filter from router.
//  *  Reads router name and filter index from the socket.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void dropFilter(NetBuffer& buf, string& reply, CpHandler& cph){
// 	string rtrName; int rtr, fx = -1;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 			((rtr = net->getNodeNum(rtrName)) == 0) ||
// 			!buf.readInt(fx) || (fx < 0)){
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	pktx repx; CtlPkt repCp;
// 	repCp.reset();
// 	repx = cph.dropFilter(radr, fx, repCp);
// 	if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 		reply.assign("could not drop log filter\n"); return;
// 	}
// }
// 
// /** Get log filter from router and return to Console.
//  *  Reads router name and filter index from the socket.
//  *  Log filter is returned as a string line.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void getFilter(NetBuffer& buf, string& reply, CpHandler& cph){
// 	string rtrName; int rtr, fx = -1;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 			((rtr = net->getNodeNum(rtrName)) == 0) ||
// 			!buf.readInt(fx) || (fx < 0)){
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	pktx repx; CtlPkt repCp;
// 	repCp.reset();
// 	repx = cph.getFilter(radr, fx, repCp);
// 	if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 		reply.assign("could not get log filter\n"); return;
// 	}
// 	reply.append(repCp.stringData);
// 	reply.append("\n");
// }
// 
// /** Get log filter set from router and return to Console.
//  *  Reads router name from the socket.
//  *	Log filter sets are returned as a text string which each entry on 
//  *  a seprate line.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void getFilterSet(NetBuffer& buf, string& reply, CpHandler& cph){
// 	string rtrName; int rtr;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 			((rtr = net->getNodeNum(rtrName)) == 0)){
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	pktx repx; CtlPkt repCp;
// 	int fx = 0;
// 	while(true){
// 		repCp.reset();
// 		repx = cph.getFilterSet(radr, fx, 10, repCp);
// 		if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 			reply.assign("could not get filter set \n"); return;
// 		}
// 		reply.append(repCp.stringData);
// 		if(repCp.index2 == 0) return;
// 		fx = repCp.index2;
// 	}
// }
// 
// /** Get logged packets from router and return to Console.
//  *  Reads router name from the socket.
//  *	Logged packets are returned as a text string which each entry on a
//  *  separate line.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void getLoggedPackets(NetBuffer& buf, string& reply, CpHandler& cph){
// 	string rtrName; int rtr;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 			((rtr = net->getNodeNum(rtrName)) == 0)){
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	pktx repx; CtlPkt repCp;
// 	repCp.reset();
// 	repx = cph.getLoggedPackets(radr, repCp);
// 	if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 		reply.assign("could not get logged packets \n"); return;
// 	}
// 	reply.append(repCp.stringData);
// 	reply.append("\n");
// }
// 
// /** Enable local log in router
//  *  Reads router name from the socket.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void enableLocalLog(NetBuffer& buf, string& reply, CpHandler& cph){
// 	string rtrName; int rtr; bool on;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 			((rtr = net->getNodeNum(rtrName)) == 0) ||
// 			!buf.readBit(on)){
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	pktx repx; CtlPkt repCp;
// 	repCp.reset();
// 	repx = cph.enableLocalLog(radr, on, repCp);
// 	if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 		reply.assign("could not get logged packets \n"); return;
// 	}
// }
// 
// /** Get link table from router and return to Console.
//  *  Table is returned as a text string which each entry on a separate line.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void getLinkTable(NetBuffer& buf, string& reply, CpHandler& cph) {
// 	string rtrName; int rtr;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 	    (rtr = net->getNodeNum(rtrName)) == 0) {
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	int lnk = 0;
// 	pktx repx; CtlPkt repCp;
// 	while (true) {
// 		repCp.reset();
// 		repx = cph.getLinkSet(radr, lnk, 10, repCp);
// 		if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 			reply.assign("could not read link table\n"); return;
// 		}
// 		reply.append(repCp.stringData);
// 		if (repCp.index2 == 0) return;
// 		lnk = repCp.index2;
// 	}
// }
// 
// /** Get comtree table from router and return to Console.
//  *  Table is returned as a text string which each entry on a separate line.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void getComtreeTable(NetBuffer& buf, string& reply, CpHandler& cph) {
// 	string rtrName; int rtr;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 	    (rtr = net->getNodeNum(rtrName)) == 0) {
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	int lnk = 0;
// 	pktx repx; CtlPkt repCp;
// 	while (true) {
// 		repCp.reset();
// 		repx = cph.getComtreeSet(radr, lnk, 10, repCp);
// 		if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 			reply.assign("could not read comtree table\n"); return;
// 		}
// 		reply.append(repCp.stringData);
// 		if (repCp.index2 == 0) return;
// 		lnk = repCp.index2;
// 	}
// }
// 
// /** Get iface table from router and return to Console.
//  *  Table is returned as a text string which each entry on a separate line.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void getIfaceTable(NetBuffer& buf, string& reply, CpHandler& cph) {
// 	string rtrName; int rtr;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 	    (rtr = net->getNodeNum(rtrName)) == 0) {
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	int lnk = 0;
// 	pktx repx; CtlPkt repCp;
// 	while (true) {
// 		repCp.reset();
// 		repx = cph.getIfaceSet(radr, lnk, 10, repCp);
// 		if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 			reply.assign("could not read iface table\n"); return;
// 		}
// 		reply.append(repCp.stringData);
// 		if (repCp.index2 == 0) return;
// 		lnk = repCp.index2;
// 	}
// }
// 
// /** Get route table from router and return to Console.
//  *  Table is returned as a text string which each entry on a separate line.
//  *  @param buf is a reference to a NetBuffer object for the socket
//  *  @param reply is a reference to a string to be returned to console
//  *  @param cph is a reference to this thread's control packet hander
//  */
// void getRouteTable(NetBuffer& buf, string& reply, CpHandler& cph) {
// 	string rtrName; int rtr;
// 	if (!buf.verify(':') || !buf.readName(rtrName) ||
// 	    (rtr = net->getNodeNum(rtrName)) == 0) {
// 		reply.assign("invalid request\n"); return;
// 	}
// 	fAdr_t radr = net->getNodeAdr(rtr);
// 	int lnk = 0;
// 	pktx repx; CtlPkt repCp;
// 	while (true) {
// 		repCp.reset();
// 		repx = cph.getRouteSet(radr, lnk, 10, repCp);
// 		if (repx == 0 || repCp.mode != CtlPkt::POS_REPLY) {
// 			reply.assign("could not read route table\n"); return;
// 		}
// 		reply.append(repCp.stringData);
// 		if (repCp.index2 == 0) return;
// 		lnk = repCp.index2;
// 	}
// }

/** Handle a connection/disconnection notification from a router.
 *  The request is acknowledged and then forwarded to the
 *  client manager.
 *  @param px is the packet number of the request packet
 *  @param cp is the control packet structure for p (already unpacked)
 *  @return true if the operation was completed successfully,
 *  otherwise false; an error reply is considered a successful
 *  completion; the operation can fail if it cannot allocate
 *  packets, or if the client manager never acknowledges the
 *  notification.
 */
bool NetMgr::handleConDisc(pktx px, CtlPkt& cp) {
	Packet& p = ps->getPacket(px);
	fAdr_t clientAdr, rtrAdr;
	if (cp.type == CtlPkt::CLIENT_CONNECT) {
		cp.xtrClientConnect(clientAdr, rtrAdr);
	} else {
		cp.xtrClientDisconnect(clientAdr, rtrAdr);
	}

	// send positive reply back to router
	pktx qx = ps->fullCopy(px); CtlPkt cq(ps->getPacket(qx));
	cq.fmtReply();
	sendReply(qx, cq.paylen, p.srcAdr);

	// now, send notification to client manager
	qx = ps->alloc(); cq.reset(ps->getPacket(qx));
	if (cp.type == CtlPkt::CLIENT_CONNECT)
		cq.fmtClientConnect(clientAdr, rtrAdr);
	else
		cq.fmtClientDisconnect(clientAdr, rtrAdr);
	pktx rx = sendRequest(qx, cq.paylen, cliMgrAdr);
	if (rx != 0) { ps->free(rx); return true; }
	return false;
}

/** Handle a new session request.
 *  This requires selecting a router and sending a sequence of
 *  control messages to configure the router to handle a new
 *  client connection.
 *  The operation can fail for a variety of reasons, at different
 *  points in the process. Any failure causes a negative reply
 *  to be sent back to the original requestor.
 *  @param px is the packet number of the request packet
 *  @param cp is the control packet structure for p (already unpacked)
 *  @return true if the operation is completed successfully, else false
 */
bool NetMgr::handleNewSession(pktx px, CtlPkt& cp) {
	Packet& p = ps->getPacket(px);
	ipa_t clientIp; RateSpec clientRates;
	cp.xtrNewSession(clientIp, clientRates);

	// determine which router to use
	fAdr_t rtrAdr;
	if (!findCliRtr(clientIp, rtrAdr)) {
		errReply(px, cp, "No router assigned to client's IP");
		return true;
	}
	int rtr = net->getNodeNum(rtrAdr);

	// find first iface for this router - refine this later
	int iface = 1;
	while (iface < net->getNumIf(rtr) && !net->validIf(rtr,iface)) iface++;

	// generate nonce to be used by client in connect packet
	uint64_t nonce = generateNonce();

	fAdr_t clientAdr = setupLeaf(0,px,cp,rtr,iface,nonce);
	if (clientAdr == 0) return false;

	// send positive reply back to sender
	pktx qx = ps->alloc(); CtlPkt cq(ps->getPacket(qx));
	cq.fmtNewSessionReply(clientAdr, rtrAdr, comtCtlAdr, 
			      net->getIfIpAdr(rtr,iface),
			      net->getIfPort(rtr,iface), nonce);
	sendReply(qx, cq.paylen, p.srcAdr);

	return true;
}

/** Setup a leaf by sending configuration packets to its access router.
 *  @param leaf is the node number for the leaf if this is a pre-configured
 *  leaf for which NetInfo object specifies various leaf parameters;
 *  it is 0 for leaf nodes that are added dynamically
 *  @param px is the packet number of the original request packet that
 *  initiated the setup process
 *  @param cp is the corresponding control packet (already unpacked)
 *  @param rtr is the node number of the access router for the new leaf
 *  @param iface is the interface of the access router where leaf connects
 *  @param nonce is the nonce that the leaf will use to connect
 *  @param useTunnel is an optional parameter that defaults to false;
 *  when it is true, it specifies that control packets should be
 *  addressed to the tunnel (ip,port) configured in the cph;
 *  @return the forest address of the new leaf on success, else 0
 */
fAdr_t NetMgr::setupLeaf(int leaf, pktx px, CtlPkt& cp, int rtr, int iface,
		 uint64_t nonce, bool useTunnel) {
	Forest::ntyp_t leafType; int leafLink; fAdr_t leafAdr; RateSpec rates;
	ipa_t leafIp;

	// first add the link at the router
	if (leaf == 0) { // dynamic client
		leafType = Forest::CLIENT; leafLink = leafIp = leafAdr = 0;
	} else { // static leaf with specified parameters
		leafType = net->getNodeType(leaf);
		int lnk = net->firstLinkAt(leaf);
		leafLink = net->getLLnum(lnk,rtr);
		leafAdr = net->getNodeAdr(leaf);
		rates = net->getLinkRates(lnk);
		leafIp = net->getLeafIpAdr(leaf);
	}
	fAdr_t rtrAdr = net->getNodeAdr(rtr);
	fAdr_t dest = (useTunnel ? 0 : rtrAdr); // TODO - check this
	pktx qx = ps->alloc(); CtlPkt cq(ps->getPacket(qx));
	cq.fmtAddLink(leafType, iface, leafLink, leafIp, 0, leafAdr, nonce);
	pktx rx = sendRequest(qx, cq.paylen, dest, px,
				"could not add link to leaf");
	if (rx == 0) return 0;
	CtlPkt cr(ps->getPacket(rx));
	cr.xtrAddLinkReply(leafLink, leafAdr);
	ps->free(rx);

	// next set the link rate
	qx = ps->alloc(); cq.reset(ps->getPacket(qx));
	cq.fmtModLink(leafLink,rates);
	rx = sendRequest(qx, cq.paylen, dest, px, "could not set link rates");
	if (rx == 0) return 0;
	ps->free(rx);

	// now add the new leaf to the neighbor comtree
	qx = ps->alloc(); cq.reset(ps->getPacket(qx));
	cq.fmtAddComtreeLink(Forest::NABOR_COMT,leafLink,0,0,0,0);
	rx = sendRequest(qx, cq.paylen, dest, px,
			 "could not add leaf to connection comtree");
	if (rx == 0) return 0;
	ps->free(rx);

	// now modify comtree link rate
	int ctx = comtrees->getComtIndex(Forest::NABOR_COMT);
	rates = comtrees->getDefLeafRates(ctx);
	comtrees->releaseComtree(ctx);
	qx = ps->alloc(); cq.reset(ps->getPacket(qx));
	cq.fmtModComtreeLink(Forest::NABOR_COMT,leafLink,rates);
	rx = sendRequest(qx, cq.paylen, dest, px,
			 "could not set rate on connection comtree");
	if (rx == 0) return 0;
	ps->free(rx);

	// now add the new leaf to the client signalling comtree
	qx = ps->alloc(); cq.reset(ps->getPacket(qx));
	cq.fmtAddComtreeLink(Forest::CLIENT_SIG_COMT,leafLink,0,0,0,0);
	rx = sendRequest(qx, cq.paylen, dest, px,
			 "could not add leaf to client signalling comtree");
	if (rx == 0) return 0;
	ps->free(rx);

	// now modify its comtree link rate
	ctx = comtrees->getComtIndex(Forest::CLIENT_SIG_COMT);
	rates = comtrees->getDefLeafRates(ctx);
	comtrees->releaseComtree(ctx);
	qx = ps->alloc(); cq.reset(ps->getPacket(qx));
	cq.fmtModComtreeLink(Forest::CLIENT_SIG_COMT,leafLink,rates);
	rx = sendRequest(qx, cq.paylen, dest, px,
			 "could not set rate on client signalling comtree");
	if (rx == 0) return 0;
	ps->free(rx);
	
	if (leafType == Forest::CLIENT) return leafAdr;

	// for controllers, also add to the network signaling comtree
	qx = ps->alloc(); cq.reset(ps->getPacket(qx));
	cq.fmtAddComtreeLink(Forest::NET_SIG_COMT,leafLink,0,0,0,0);
	rx = sendRequest(qx, cq.paylen, dest, px,
			 "could not add leaf to client signalling comtree");
	if (rx == 0) return 0;
	ps->free(rx);

	// and modify its comtree link rate
	ctx = comtrees->getComtIndex(Forest::NET_SIG_COMT);
	rates = comtrees->getDefLeafRates(ctx);
	comtrees->releaseComtree(ctx);
	qx = ps->alloc(); cq.reset(ps->getPacket(qx));
	cq.fmtModComtreeLink(Forest::NET_SIG_COMT,leafLink,rates);
	rx = sendRequest(qx, cq.paylen, dest, px,
			 "could not set rate on client signalling comtree");
	if (rx == 0) return 0;
	ps->free(rx);

	return leafAdr;
}

/** Handle a cancel session request.
 *  @param px is the packet number of the request packet
 *  @param cp is the control packet structure for p (already unpacked)
 *  @return true if the operation is completed successfully, else false
 */
bool NetMgr::handleCancelSession(pktx px, CtlPkt& cp) {
	fAdr_t clientAdr, rtrAdr;
	cp.xtrCancelSession(clientAdr, rtrAdr);

	// verify that clientAdr is in range for router
	int rtr = net->getNodeNum(rtrAdr);
	if (rtr == 0) {
		errReply(px,cp,"no router with specified address");
		return false;
	}
	pair<fAdr_t,fAdr_t> range;
	net->getLeafRange(rtr, range);
	if (clientAdr < range.first || clientAdr > range.second) {
		errReply(px,cp,"client address not in router's range");
		return false;
	}

	// drop the client's access link
	pktx qx = ps->alloc(); CtlPkt cq(ps->getPacket(qx));
	cq.fmtDropLink(0,clientAdr);
	int rx = sendRequest(qx, cq.paylen, rtrAdr, px, "could not drop link");
	if (rx == 0) return 0;
	ps->free(rx);

	// send positive reply back to sender
	qx = ps->fullCopy(px);
	Packet& q = ps->getPacket(qx);
	cq.reset(q);
	cq.fmtReply();
	sendReply(qx, cq.paylen, q.srcAdr);

	return true;
}

/** Handle boot process for a pre-configured leaf node.
 *  This handler is intended primarily for controllers.
 *  It first configures the router, so it will accept new leaf.
 *  It then sends leaf configuration information to a new leaf node,
 *  including address information for it, and its router,
 *  as well as a nonce to be used when it connects.
 *  @param px is the index of the incoming boot request packet
 *  @param cp is the control packet (already unpacked)
 */
bool NetMgr::handleBootLeaf(pktx px, CtlPkt& cp) {
	Packet& p = ps->getPacket(px);

	// first, find leaf in NetInfo object
	int leaf;
	for (leaf = net->firstLeaf(); leaf != 0; leaf = net->nextLeaf(leaf)) {
		if (net->getLeafIpAdr(leaf) == p.tunIp) break;
	}
	if (leaf == 0) {
		errReply(px,cp,"unknown leaf address");
		return false;
	}

	if (net->getStatus(leaf) == NetInfo::UP) {
		// final reply lost or delayed, resend and quit
		pktx qx = ps->fullCopy(px); CtlPkt cq(ps->getPacket(qx));
		cq.fmtReply();
		sendReply(qx, cq.paylen, 0);
		return true;
	}

	int lnk = net->firstLinkAt(leaf);
	int rtr = net->getPeer(leaf,lnk);
	fAdr_t rtrAdr = net->getNodeAdr(rtr);

	net->setStatus(leaf,NetInfo::BOOTING);

	if (net->getStatus(rtr) != NetInfo::UP) {
		errReply(px,cp,"access router is not yet up");
		net->setStatus(leaf,NetInfo::DOWN);
		return false;
	}

	// find first iface for this router - refine this later
	int iface = 1;
	while (iface < net->getNumIf(rtr) && !net->validIf(rtr,iface)) iface++;
	uint64_t nonce = generateNonce();

	// add link at router and configure rates/comtrees
	if (setupLeaf(leaf,px,cp,rtr,iface,nonce) == 0) {
		net->setStatus(leaf,NetInfo::DOWN);
		return false;
	}

	// Send configuration parameters to leaf
	pktx qx = ps->fullCopy(px); CtlPkt cq(ps->getPacket(qx));
	cq.fmtConfigLeaf(net->getNodeAdr(leaf),rtrAdr,
			   net->getIfIpAdr(rtr,iface),
		   	   net->getIfPort(rtr,iface), nonce);
	pktx rx = sendRequest(qx,cq.paylen,0,px,  // 0 dest sends direct
				"could not configure leaf node");
	if (rx == 0) {
		net->setStatus(leaf,NetInfo::DOWN);
		return false;
	}
	ps->free(rx);

	// finally, send positive ack
	qx = ps->fullCopy(px); cq.reset(ps->getPacket(qx));
	cq.fmtReply();
	sendReply(qx, cq.paylen, 0); // 0 sends it directly back to leaf
	net->setStatus(leaf,NetInfo::UP);

	logger->log("completed leaf boot request",0,p);
	return true;
}

/** Return a random nonce suitable for use when connecting a leaf.  */
uint64_t NetMgr::generateNonce() {
	uint64_t nonce;
	do {
		high_resolution_clock::time_point tp;
		tp = high_resolution_clock::now();
		nonce = tp.time_since_epoch().count();
		nonce *= random();
	} while (nonce == 0);
	return nonce;
}

/** Process the reply from an outgoing request packet
 *  This method implements a common pattern used by handlers
 *  that are configuring routers in response to a received request.
 *  The handler typically sends out a series of request packets and must
 *  then handle each reply. A missing or negative reply causes a negative 
 *  response to be sent back to the sender of the original request.
 *  @param px is the packet index of the incoming request that
 *  initiated the handler
 *  @param cp is the unpacked control packet for px
 *  @param reply is the packet index for a reply to some outgoing
 *  control packet that was sent by this handler
 *  @param repCp is a reference to the control packet for reply
 *  @param msg is an error message to be sent back to the original
 *  sender of px, if reply=0 or the associated control packet indicates
 *  a failure
 *  @return true if the reply is not zero and the control packet for
 *  reply indicates that the requested operation succeeded
bool processReply(pktx px, CtlPkt& cp, pktx rx, CtlPkt& cr, const string& msg) {
	if (cr.mode == CtlPkt::NO_REPLY) {
		errReply(px,cp,msg + " (no response from target)");
		ps->free(rx);
		return false;
	}
	if (cr.mode != CtlPkt::POS_REPLY) {
		errReply(px,cp,msg + " (" + cr.toString() + ")");
		ps->free(rx);
		return false;
	}
	ps->free(rx);
	return true;
}
 */

/** Handle a boot request from a router.
 *  This requires sending a series of control packets to the router
 *  to configure its interface table, its link table and its comtree
 *  table. When configuration is complete, we send a boot complete
 *  message to the router.
 *  The operation can fail for a variety of reasons, at different
 *  points in the process. Any failure causes a negative reply
 *  to be sent back to the original requestor.
 *  @param px is the packet number of the request packet
 *  @param cp is the control packet structure for p (already unpacked)
 *  @param cph is the control packet handler for this thread
 *  @return true if the operation is completed successfully;
 *  sending an error reply is considered a success; the operation
 *  fails if it cannot allocate required packets, or if the
 *  router fails to respond to either of the control messages
 *  sent to it.
 */
bool NetMgr::handleBootRouter(pktx px, CtlPkt& cp) {
	Packet& p = ps->getPacket(px);

	// find rtr index based on source address
	ipa_t rtrAdr = p.srcAdr;
	int rtr;
	for (rtr = net->firstRouter(); rtr != 0; rtr = net->nextRouter(rtr)) {
		if (net->getNodeAdr(rtr) == rtrAdr) break;
	}
	if (rtr == 0) {
		errReply(px,cp,"boot request from unknown router rejected\n");
		logger->log("handleBootRequest: received boot request from "
			   "unknown router\n",2,p);
		return true;
	}

	// reply immediately - just means "message received, standby"
	pktx qx = ps->fullCopy(px); CtlPkt cq(ps->getPacket(qx));
	cq.fmtReply();
	sendReply(qx, cq.paylen, 0);

	net->setStatus(rtr,NetInfo::BOOTING);

	// configure leaf address range
	qx = ps->fullCopy(px); cq.reset(ps->getPacket(qx));
	pair<fAdr_t,fAdr_t> leafRange; net->getLeafRange(rtr,leafRange);
	cq.fmtSetLeafRange(leafRange.first, leafRange.second);
	pktx rx = sendRequest(qx, cq.paylen, 0, px,
				"could not configure leaf range");
	if (rx == 0) {
		net->setStatus(rtr,NetInfo::DOWN);
		return false;
	}
	ps->free(rx);

	// add/configure interfaces
	// for each interface in table, do an add iface
	int nmLnk = net->getLLnum(net->firstLinkAt(netMgr),nmRtr);
	int nmIface = net->getIface(nmLnk,nmRtr);
	for (int i = 1; i <= net->getNumIf(rtr); i++) {
		if (!net->validIf(rtr,i)) continue;
		qx = ps->fullCopy(px); cq.reset (ps->getPacket(qx));
		cq.fmtAddIface(i,net->getIfIpAdr(rtr,i),net->getIfRates(rtr,i));
		rx = sendRequest(qx, cq.paylen, 0, px, 
				 "could not add interface at router");
		if (rx == 0) {
			net->setStatus(rtr,NetInfo::DOWN);
			return false;
		}
		ipa_t ifaceIp; ipp_t ifacePort;
		CtlPkt cr(ps->getPacket(rx));
		cr.xtrAddIfaceReply(ifaceIp,ifacePort);
		ps->free(rx);

		net->setIfPort(rtr,i,ifacePort);
		// if this is the network manager's router, configure port
		// in substrate
		if (rtr == nmRtr && i == nmIface) {
			sub->setRtrPort(ifacePort);
		}
	}

	// add/configure links to other routers
	for (int lnk = net->firstLinkAt(rtr); lnk != 0;
		 lnk = net->nextLinkAt(rtr,lnk)) {
		int peer = net->getPeer(rtr,lnk);
		if (net->getNodeType(peer) != Forest::ROUTER) continue;
		if (!setupEndpoint(lnk,rtr,px,cp,true)) {
			net->setStatus(rtr,NetInfo::DOWN);
			return false;
		}
	}

/*
Add code for special case of neighbor comtree.
Include all links.
Drop comtree 1 from topoFiles
*/

	// add/configure comtrees
	for (int ctx = comtrees->firstComtree(); ctx != 0;
		 ctx = comtrees->nextComtree(ctx)) {
		fAdr_t rtrAdr = net->getNodeAdr(rtr);
		if (!comtrees->isComtNode(ctx,rtrAdr)) continue;
		if (!setupComtree(ctx,rtr,px,cp,true)) {
			comtrees->releaseComtree(ctx);
			net->setStatus(rtr,NetInfo::DOWN);
			return false;
		}
	}

	// if this is net manager's router, configure link
	if (rtr == nmRtr) {
		uint64_t nonce = generateNonce();
		sub->setNonce(nonce);
		if (setupLeaf(netMgr,px,cp,rtr,nmIface,nonce,true) == 0) {
			Util::fatal("cannot configure NetMgr's access link");
		}
	}

	// finally, send boot complete packet
	qx = ps->fullCopy(px); cq.reset(ps->getPacket(qx));
	cq.fmtBootComplete();
	rx = sendRequest(qx, cq.paylen, 0);
	if (rx == 0) {
		logger->log("failed during boot complete step",0,p);
		return false;
	}
	ps->free(rx);

	// and finalize status
	net->setStatus(rtr,NetInfo::UP);
	if (rtr == nmRtr) sub->setRtrReady(true);
	logger->log("completed boot request",0,p);
	return true;
}

/** Configure a link endpoint at a router.
 *  This method is used when booting a router.
 *  It configures a link going to another router.
 *  @param lnk is the global link number for the link being configured
 *  @param rtr is the number of the router that is to be configured
 *  @param px is the packet that triggered this operation
 *  @param cp is the control packet from px
 *  @param useTunnel is an optional parameter that defaults to false;
 *  when it is true, it specifies that control packets should be
 *  addressed to the packet's tunnel (ip,port) 
 *  @return true if the configuration is successful, else false
 */
bool NetMgr::setupEndpoint(int lnk, int rtr, pktx px, CtlPkt& cp, bool useTunnel) {
	int llnk = net->getLLnum(lnk,rtr);
	int iface = net->getIface(llnk,rtr);
	fAdr_t rtrAdr = net->getNodeAdr(rtr);

	int peer = net->getPeer(rtr,lnk);
	fAdr_t peerAdr = net->getNodeAdr(peer);
	int plnk = net->getLLnum(lnk,peer);
	int i = net->getIface(plnk,peer);
	ipa_t peerIp = net->getIfIpAdr(peer,i);
	ipp_t peerPort = 0;

	uint64_t nonce;
	if (net->getStatus(peer) != NetInfo::UP) {
		nonce = generateNonce();
		net->setNonce(lnk,nonce);
	} else {
		peerPort = net->getIfPort(peer,i);
		nonce = net->getNonce(lnk);
	}
	// Add link endpoint at router
	pktx qx = ps->fullCopy(px); CtlPkt cq(ps->getPacket(px));
	cq.fmtAddLink(Forest::ROUTER, iface, llnk,
		      peerIp, peerPort, peerAdr, nonce);
	fAdr_t dest = (useTunnel ? 0 : rtrAdr);
	pktx rx = sendRequest(qx, cq.paylen, dest, px,
				"could not add link at router");
	if (rx == 0) return false;
	ps->free(rx);

	// And set the link rate
	qx = ps->fullCopy(px); cq.reset(ps->getPacket(px));
	RateSpec rs = net->getLinkRates(lnk);
	if (rtr == net->getLeft(lnk)) rs.flip();
	cq.fmtModLink(llnk, rs);
	rx = sendRequest(qx, cq.paylen, dest, px,
				"could not set link rates at router");
	if (rx == 0) return false;
	ps->free(rx);

	return true;
}

/** Setup a pre-configured comtree at a booting router.
 *  @param ctx is the index of the comtree being setup
 *  @param rtr is the number of the router
 *  @param px is the number of the packet that initiated the current operation
 *  @param cp is the control packet for px (already unpacked)
 *  @return true on success, false on failure
 */
bool NetMgr::setupComtree(int ctx, int rtr, pktx px, CtlPkt& cp,
		  bool useTunnel) {
	fAdr_t rtrAdr = net->getNodeAdr(rtr);
	
	comt_t comt = comtrees->getComtree(ctx);

	// first step is to add comtree at router
	pktx qx = ps->fullCopy(px); CtlPkt cq(ps->getPacket(px));
	cq.fmtAddComtree(comt);
	fAdr_t dest = (useTunnel ? 0 : rtrAdr);
	pktx rx = sendRequest(qx, cq.paylen, dest, px,
				"could not add comtree at router");
	if (rx == 0) return false;
	ps->free(rx);

	// next, add links to the comtree and set their data rates
	int plnk = comtrees->getPlink(ctx,rtrAdr);
	int parent = net->getPeer(rtr,plnk);
	for (int lnk = net->firstLinkAt(rtr); lnk != 0;
		 lnk = net->nextLinkAt(rtr,lnk)) {
		if (!comtrees->isComtLink(ctx,lnk)) continue;
		int peer = net->getPeer(rtr,lnk);
		if (net->getNodeType(peer) != Forest::ROUTER) continue;

		// get information about this link
		int llnk = net->getLLnum(lnk,rtr);
		fAdr_t peerAdr = net->getNodeAdr(peer);
		bool peerCoreFlag = comtrees->isCoreNode(ctx,peerAdr);

		// first, add comtree link
		pktx qx = ps->fullCopy(px); CtlPkt cq(ps->getPacket(px));
		cq.fmtAddComtreeLink(comt,llnk,peerCoreFlag,0,0,0);
		pktx rx = sendRequest(qx, cq.paylen, dest, px,
					"could not add comtree link at router");
		if (rx == 0) return false;
		ps->free(rx);

		// now, set link rates
		RateSpec rs;
		if (peer == parent) {
			rs = comtrees->getLinkRates(ctx,rtrAdr);
			rs.flip();
		} else {
			rs = comtrees->getLinkRates(ctx,peerAdr);
		}
		qx = ps->fullCopy(px); cq.reset(ps->getPacket(px));
		cq.fmtModComtreeLink(comt,llnk,rs);
		rx = sendRequest(qx, cq.paylen, dest, px, "could not "
					"set comtree link rates at router");
		if (rx == 0) return false;
		ps->free(rx);
	}
	// finally, we need to modify overall comtree attributes
	qx = ps->fullCopy(px); cq.reset(ps->getPacket(px));
	bool coreFlag = comtrees->isCoreNode(ctx,rtrAdr);
	cq.fmtModComtree(comt,coreFlag,plnk);
	rx = sendRequest(qx, cq.paylen, dest, px,
				"could not set comtree parameters at router");
	if (rx == 0) return false;
	ps->free(rx);
	return true;
}
	
/** Finds the router address of the router based on IP prefix
 *  @param cliIp is the ip of the client
 *  @param rtrAdr is the address of the router associated with this IP prefix
 *  @return true if there was an IP prefix found, false otherwise
 */
bool NetMgr::findCliRtr(ipa_t cliIp, fAdr_t& rtrAdr) {
	string cip = Np4d::ip2string(cliIp);
	for (unsigned int i = 0; i < (unsigned int) numPrefixes; ++i) {
		string ip = prefixes[i].prefix;
		unsigned int j = 0;
		while (j < ip.size() && j < cip.size()) {
			if (ip[j] != '*' && cip[j] != ip[j]) break;
			if (ip[j] == '*' ||
			    (j+1 == ip.size() && j+1 == cip.size())) {
				rtrAdr = prefixes[i].rtrAdr;
				return true;
			}
			j++;
		}
	}
	// if no prefix for client address, select router at random
	int i = Util::randint(0,net->getNumRouters()-1);
	for (int r = net->firstRouter(); r != 0; r = net->nextRouter(r)) {
		if (i-- == 0) {
			rtrAdr = net->getNodeAdr(r); return true;
		}
	}
	return false; // should never reach here
}

/** Reads the prefix file
 *  @param filename is the name of the prefix file
 *  @return true on success, false otherwise
 */
bool NetMgr::readPrefixInfo(const char *filename) {
	ifstream ifs; ifs.open(filename);
	if(ifs.fail()) return false;
	Util::skipBlank(ifs);
	int i = 0;
	while(!ifs.eof()) {
		string pfix; fAdr_t rtrAdr;
		ifs >> pfix;
		if(!Forest::readForestAdr(ifs,rtrAdr))
			break;
		prefixes[i].prefix = pfix;
		prefixes[i].rtrAdr = rtrAdr;
		Util::skipBlank(ifs);
		i++;
	}
	numPrefixes = i;
	cout << "read address info for " << numPrefixes << " prefixes" << endl;
	return true;
}

/** Write a record to the file of network administrators.
 *  The calling thread is assumed to hold a lock on the client.
 *  @param adx is a non-negative integer
 */
void NetMgr::writeAdminRecord(int adx) {
	if (adx < 0 || adx >= admTbl->getMaxAdmins()) return;

	unique_lock<mutex> lck(adminFileLock);
	if (maxRecord == 0) {
		adminFile.seekp(0,ios_base::end);
		maxRecord = adminFile.tellp()/RECORD_SIZE;
	}

	// position file pointer, adding dummy records if needed
	if (adx > maxRecord) {
		adminFile.seekp((maxRecord+1)*RECORD_SIZE);
		while (adx > maxRecord) {
			adminFile.write(dummyRecord,RECORD_SIZE);
			maxRecord++;
		}
	}
	adminFile.seekp(adx*RECORD_SIZE);

	if (admTbl->validAdmin(adx)) {
		string s = admTbl->admin2string(adx);
		s = "+ " + s;
		if (s.length() > RECORD_SIZE) {
			s.erase(RECORD_SIZE-1); s += "\n";
		} else {
			s.erase(s.length()-1);
			int len = RECORD_SIZE - s.length();
			char *p = dummyRecord + s.length();
			s.append(p,len);
		}
		adminFile.write(s.c_str(),RECORD_SIZE);
	} else {
		//s.assign(dummyRecord,RECORD_SIZE);
		adminFile.write(dummyRecord,RECORD_SIZE);
	}
	adminFile.flush();
	maxRecord = max(adx,maxRecord);
	return;
}

} // ends namespace
