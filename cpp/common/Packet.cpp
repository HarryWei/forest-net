/** @file Packet.cpp 
 *
 *  @author Jon Turner
 *  @date 2011
 *  This is open source software licensed under the Apache 2.0 license.
 *  See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#include "Packet.h"
#include "CtlPkt.h"

namespace forest {

Packet::Packet() {
	version = 1; buffer = 0;
}

Packet::~Packet() {}

/** Unpack the packet from the buffer.
 *  @return true on success
 */
bool Packet::unpack() {
	if (buffer == 0) return false;
	buffer_t& b = *buffer;
	uint32_t x = ntohl(b[0]);
	version = (x >> 28) & 0xf;
	length = (x >> 16) & 0xfff;
	if (version != 1 || length < Forest::OVERHEAD)
		return false;
	type = (Forest::ptyp_t) ((x >> 8) & 0xff);
	flags = (x & 0xff);
	comtree = ntohl(b[1]);
	srcAdr = ntohl(b[2]);
	dstAdr = ntohl(b[3]);
	return true;
}

/** Pack the packet into the buffer.
 *  @return true on success
 */
bool Packet::pack() {
	if (buffer == 0) return false;
	buffer_t& b = *buffer;
        uint32_t x = (version << 28)
                     | ((length & 0xfff) << 16) 
                     | ((type & 0xff) << 8)
                     | (flags & 0xff);
        b[0] = htonl(x);
        b[1] = htonl(comtree);
        b[2] = htonl(srcAdr);
        b[3] = htonl(dstAdr);
	return true;
}

/** Verify the header error check.
 *  @return true on success
 */
bool Packet::hdrErrCheck() const {
        return true;
}

/** Verify the payload error check.
 *  @return true on success
 */
bool Packet::payErrCheck() const {
        return true;
}

/** Update the header error check based on buffer contents.
 *  @return true on success
 */
void Packet::hdrErrUpdate() {
}

/** Update the payload error check based on buffer contents.
 *  @return true on success
 */
void Packet::payErrUpdate() {
}

/** Read an input packet and pack fields into buffer.
 *  @return true on success
 */
bool Packet::read(istream& in) {
	int flgs, comt; string ptypString;

	Util::skipBlank(in);
	if (!Util::readInt(in,length,true) ||
	    !Util::readWord(in,ptypString,true) ||
	    !Util::readInt(in,flgs,true) ||
	    !Util::readInt(in,comt,true) ||
	    !Forest::readForestAdr(in,srcAdr) ||
	    !Forest::readForestAdr(in,dstAdr))
		return false;
	flags = (flgs_t) flgs; comtree = (comt_t) comt;

             if (ptypString == "data")       type = Forest::CLIENT_DATA;
        else if (ptypString == "sub_unsub")  type = Forest::SUB_UNSUB;
        else if (ptypString == "connect")    type = Forest::CONNECT;
        else if (ptypString == "disconnect") type = Forest::DISCONNECT;
        else if (ptypString == "rteRep")     type = Forest::RTE_REPLY;
        else if (ptypString == "client_sig") type = Forest::CLIENT_SIG;
        else if (ptypString == "net_sig")    type = Forest::NET_SIG;
        else Util::fatal("Packet::getPacket: invalid packet type");

	if (buffer == 0) return true;
	buffer_t& b = *buffer;
	pack(); int32_t x;
	for (int i = 0; i < min(8,(length-HDRLEN)/4); i++) {
		if (Util::readInt(in,x,true)) b[(HDRLEN/4)+i] = htonl(x);
		else b[(HDRLEN/4)+i] = 0;
	}
	hdrErrUpdate(); payErrUpdate();
	return true;
}

string Packet::pktTyp2string(Forest::ptyp_t type) {
	string s;
        if (type == Forest::CLIENT_DATA)     s = "data      ";
        else if (type == Forest::SUB_UNSUB)  s = "sub_unsub ";
        else if (type == Forest::CLIENT_SIG) s = "client_sig";
        else if (type == Forest::CONNECT)    s = "connect   ";
        else if (type == Forest::DISCONNECT) s = "disconnect";
        else if (type == Forest::NET_SIG)    s = "net_sig   ";
        else if (type == Forest::RTE_REPLY)  s = "rteReply  ";
        else if (type == Forest::RTR_CTL)    s = "rtr_ctl   ";
        else if (type == Forest::VOQSTATUS)  s = "voq_status";
        else                         	     s = "undef ";
	return s;
}

bool Packet::string2pktTyp(string& s, Forest::ptyp_t& type) {
	if (s == "data")		type = Forest::CLIENT_DATA;
	else if (s == "sub_unsub")	type = Forest::SUB_UNSUB;
	else if (s == "client_sig")	type = Forest::CLIENT_SIG;
	else if (s == "connect")	type = Forest::CONNECT;
	else if (s == "disconnect")	type = Forest::DISCONNECT;
	else if (s == "net_sig")	type = Forest::NET_SIG;
	else if (s == "rteReply")	type = Forest::RTE_REPLY;
	else if (s == "rtr_ctl")	type = Forest::RTR_CTL;
	else if (s == "voq_status")	type = Forest::VOQSTATUS;
	else if (s == "undef")		type = Forest::UNDEF_PKT;
	else return false;
	return true;
}

/** Create a string representing packet contents.
 *  @param b is a reference to a buffer containing the packet
 *  @return the string
 */
string Packet::toString() const {
	stringstream ss;
        ss << "len=" << setw(3) << length;
        ss << " typ=";

        if (type == Forest::CLIENT_DATA)     ss << "data      ";
        else if (type == Forest::SUB_UNSUB)  ss << "sub_unsub ";
        else if (type == Forest::CLIENT_SIG) ss << "client_sig";
        else if (type == Forest::CONNECT)    ss << "connect   ";
        else if (type == Forest::DISCONNECT) ss << "disconnect";
        else if (type == Forest::NET_SIG)    ss << "net_sig   ";
        else if (type == Forest::RTE_REPLY)  ss << "rteRep    ";
        else if (type == Forest::RTR_CTL)    ss << "rtr_ctl   ";
        else if (type == Forest::VOQSTATUS)  ss << "voq_status";
        else                         ss << "--------- ";
        ss << " flags=" << int(flags);
        ss << " comt=" << setw(3) << comtree;
        ss << " sadr=" << Forest::fAdr2string(srcAdr);
        ss << " dadr=" << Forest::fAdr2string(dstAdr);

	if (buffer == 0) {
		ss << endl; return ss.str();
	}
	buffer_t& b = *buffer;
	int32_t x;
        for (int i = 0; i < min(8,(length-HDRLEN)/4); i++) {
		x = ntohl(b[(HDRLEN/4)+i]);
                ss << " " << x;
	}
        ss << endl;
	if (type == Forest::CLIENT_SIG || type == Forest::NET_SIG) {
		CtlPkt cp(*this);
		ss << cp.toString();
	}
	return ss.str();
}

} // ends namespace

