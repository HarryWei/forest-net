/** @file CtlPkt.cpp
 *
 *  @author Jon Turner
 *  @date 2011
 *  This is open source software licensed under the Apache 2.0 license.
 *  See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

/** The CtlPkt class is used to pack and unpack forest control messages.
 *  The class has a public field for every field that can be used in
 *  in a control packet. To create a control packet, the user
 *  constructs a CtlPkt object. The user then specifies
 *  selected fields and calls pack, which packs the specified
 *  fields into the packet's payload and returns the length of
 *  the payload.
 *  
 *  To unpack a buffer, the user constructs a CtlPkt object
 *  and then calls unpack, specifying the length of the payload.
 *  The control packet fields can then be retrieved from the
 *  corresponding fields of the CtlPkt object.
 */

#include "CtlPkt.h"

namespace forest {

/** Constructor for CtlPkt with no initialization.
 */
CtlPkt::CtlPkt() { reset(); }

/** Construct a new control packet from the payload of a packet.
 *  @param p is a reference to a packet that contains a control packet
 */
CtlPkt::CtlPkt(const Packet& p) { reset(p); }

/** Constructor with partial initialization.
 *  Unspecified fields are set to recognized "undefined" values.
 *  @param payload1 is a pointer to the start of the payload of
 *  a packet buffer
 *  @param len is the number of valid bytes in the payload
 */
CtlPkt::CtlPkt(uint32_t* payload1, int len) {
	reset(payload1,len);
}

/** Constructor with full initialization.
 *  Unspecified fields are set to recognized "undefined" values.
 *  @param type1 is a the control packet type
 *  @param mode1 is a the control packet mode (REQUEST, POS_REPLY, NEG_REPLY)
 *  @param seqNum1 is a sequence number to include in the packet
 */
CtlPkt::CtlPkt(CpType type1, CpMode mode1, uint64_t seqNum1,
	       uint32_t* payload1) {
	reset(type1, mode1, seqNum1, payload1);
}

CtlPkt::CtlPkt(CpType type1, CpMode mode1, uint64_t seqNum1) {
	reset(type1, mode1, seqNum1);
}

/** Destructor for CtlPkt. */
CtlPkt::~CtlPkt() { } 

/** Reset control packet, re-initializing fields.
 *  Unspecified fields are set to recognized "undefined" values.
 *  @param type1 is a the control packet type
 *  @param mode1 is a the control packet mode (REQUEST, POS_REPLY, NEG_REPLY)
 *  @param seqNum1 is a sequence number to include in the packet
 */
void CtlPkt::reset(CpType type1, CpMode mode1, uint64_t seqNum1,
		   uint32_t* payload1) {
	reset();
	type = type1; mode = mode1; seqNum = seqNum1;
	payload = payload1;
}

void CtlPkt::reset(CpType type1, CpMode mode1, uint64_t seqNum1) {
	reset();
	type = type1; mode = mode1; seqNum = seqNum1;
}

/** Reset control packet, with partial initialization.
 *  Unspecified fields are set to recognized "undefined" values.
 *  @param payload1 is a pointer to the start of the payload of
 *  a packet buffer
 *  @param len is the number of valid bytes in the payload
 */
void CtlPkt::reset(uint32_t* payload1, int len) {
	reset(); payload = payload1; paylen = len;
}

/** Reset control packet, from a given packet's payload.
 *  Unspecified fields are set to recognized "undefined" values.
 *  @param p is a reference to a packet.
 */
void CtlPkt::reset(const Packet& p) {
	reset();
	payload = p.payload(); paylen = p.length-Forest::OVERHEAD; unpack();
}

void CtlPkt::reset() {
	// initialize all fields to undefined values
	type = UNDEF_CPTYPE; mode = UNDEF_MODE; seqNum = 0;
	adr1 = adr2 = adr3 = 0;
	ip1 = 0; ip2 = 0;
	port1 = 0; port2 = 0;
	nonce = 0;
	rspec1.set(-1); rspec2.set(-1);
	coreFlag = -1;
	iface = 0; link = 0; nodeType = Forest::UNDEF_NODE;
	comtree = 0; comtreeOwner = 0;
	count = -1;
	queue = 0;
	zipCode = 0;
	errMsg.clear();
	payload = 0; paylen = 0;
	lt = new LinkTable(100);
	firstLinkNum = 0; numOfLinks = 0; nextLinkNum = 0;
}

#define packPair(x,y) ((payload[pp++] = htonl(x)), (payload[pp++] = htonl(y)))
#define packNonce(x,y) ((payload[pp++] = htonl(x)), \
		(payload[pp++] = htonl((int) (((y)>>32)&0xffffffff))), \
		(payload[pp++] = htonl((int) ((y)&0xffffffff))))
#define packRspec(x,y) ((payload[pp++] = htonl(x)), \
				(payload[pp++] = htonl(y.bitRateUp)), \
				(payload[pp++] = htonl(y.bitRateDown)), \
				(payload[pp++] = htonl(y.pktRateUp)), \
				(payload[pp++] = htonl(y.pktRateDown)) \
			)

/** Pack CtlPkt fields into payload of packet.
 *  @return the length of the packed payload in bytes.
 *  or 0 if an error was detected.
 */
int CtlPkt::pack() {
	if (payload == 0) return 0;

	int pp = 0;
	payload[pp++] = htonl(type);
	payload[pp++] = htonl(mode);
	payload[pp++] = htonl((uint32_t) (seqNum >> 32));
	payload[pp++] = htonl((uint32_t) (seqNum & 0xffffffff));

	if (mode == NEG_REPLY) {
		int len = errMsg.length();
		if (len > 0) {
			len = min(len,MAX_STRING);
			payload[pp++] = htonl(ERRMSG);
			payload[pp++] = htonl(len);
			errMsg.copy((char*) &payload[pp], len);
		}
		paylen = 4*(pp + (len+3)/4);
		return paylen;
	}

	switch (type) {
	case CLIENT_ADD_COMTREE:
		if (mode == REQUEST) {
			if (zipCode == 0) return 0;
			packPair(ZIPCODE,zipCode);
		} else {
			packPair(COMTREE,comtree);
		}
		break;
	case CLIENT_DROP_COMTREE:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
		}
		break;
	case CLIENT_GET_COMTREE:
		if (mode == REQUEST) {
			packPair(COMTREE,comtree);
		} else {
			if (comtree == 0 || comtreeOwner == 0||
			    !rspec1.isSet() || !rspec2.isSet())
				return 0;
			packPair(COMTREE,comtree);
			packPair(COMTREE_OWNER,comtreeOwner);
			packRspec(RSPEC1,rspec1);
			packRspec(RSPEC2,rspec2);
		}
		break;
	case CLIENT_MOD_COMTREE:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
			if (rspec1.isSet()) packRspec(RSPEC1,rspec1);
			if (rspec2.isSet()) packRspec(RSPEC2,rspec2);
		} else {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
		}
		break;
	case CLIENT_JOIN_COMTREE:
		if (mode == REQUEST) {
			if (comtree == 0 || ip1 == 0 || port1 == 0)
				return 0;
			packPair(COMTREE,comtree);
			packPair(IP1,ip1); packPair(PORT1,port1);
		}
		break;
	case CLIENT_LEAVE_COMTREE:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
			packPair(IP1,ip1); packPair(PORT1,port1);
		}
		break;
	case CLIENT_RESIZE_COMTREE:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
		}
		break;
	case CLIENT_GET_LEAF_RATE:
		if (mode == REQUEST) {
			if (comtree == 0 || adr1 == 0)
				return 0;
			packPair(COMTREE,comtree);
			packPair(ADR1,adr1);
		} else {
			if (!rspec1.isSet()) return 0;
			packRspec(RSPEC1,rspec1);
		}
		break;
	case CLIENT_MOD_LEAF_RATE:
		if (mode == REQUEST) {
			if (comtree == 0 || adr1 == 0 ||
			    !rspec1.isSet())
				return 0;
			packPair(COMTREE,comtree);
			packPair(ADR1,adr1);
			packRspec(RSPEC1,rspec1);
		}
		break;
	case ADD_IFACE:
		if (mode == REQUEST) {
			if (iface == 0 || ip1 == 0 || !rspec1.isSet())
				return 0;
			packPair(IFACE,iface);
			packPair(IP1,ip1);
			packRspec(RSPEC1,rspec1);
		} else {
			if (ip1 == 0 || port1 == 0) return 0;
			packPair(IP1,ip1);
			packPair(PORT1,port1);
		}
		break;
	case DROP_IFACE:
		if (mode == REQUEST) {
			if (iface == 0) return 0;
			packPair(IFACE,iface);
		}
		break;
	case GET_IFACE:
		if (mode == REQUEST) {
			if (iface == 0) return 0;
			packPair(IFACE,iface);
		} else {
			if (iface == 0 || ip1 == 0 ||
			    !rspec1.isSet() || !rspec2.isSet())
				return 0;
			packPair(IFACE,iface);
			packPair(IP1,ip1);
			packPair(PORT1,port1);
			packRspec(RSPEC1,rspec1);
			packRspec(RSPEC2,rspec2);
		}
		break;
	case MOD_IFACE:
		if (mode == REQUEST) {
			if (iface == 0 || !rspec1.isSet())
				return 0;
			packPair(IFACE,iface);
			packRspec(RSPEC1,rspec1);
		}
		break;
	case ADD_LINK:
		if (mode == REQUEST) {
			if (nodeType == 0 || iface == 0) return 0;
			packPair(NODE_TYPE,nodeType);
			packPair(IFACE,iface);
			if (link != 0) packPair(LINK,link);
			if (ip1 != 0) packPair(IP1,ip1);
			if (port1 != 0) packPair(PORT1,port1);
			if (adr1 != 0) packPair(ADR1,adr1);
			if (nonce != 0) packNonce(NONCE,nonce);
		} else {
			if (link != 0) packPair(LINK,link);
			if (adr1 != 0) packPair(ADR1,adr1);
		}
		break;
	case DROP_LINK:
		if (mode == REQUEST) {
			if (link == 0 && adr1 == 0) return 0;
			if (link != 0) packPair(LINK,link);
			if (adr1 != 0) packPair(ADR1,adr1);
		}
		break;
	case GET_LINK:
		if (mode == REQUEST) {
			if (link == 0) return 0;
			packPair(LINK,link);
		} else {
			if (link == 0 || iface == 0 || nodeType == 0 ||
			    ip1 == 0 || port1 == 0 || adr1 == 0 ||
			    !rspec1.isSet() || !rspec2.isSet())
				return 0;
			packPair(LINK,link);
			packPair(IFACE,iface);
			packPair(NODE_TYPE,nodeType);
			packPair(IP1,ip1);
			packPair(PORT1,port1);
			packPair(ADR1,adr1);
			packRspec(RSPEC1,rspec1);
			packRspec(RSPEC2,rspec2);
		}
		break;
	case MOD_LINK:
		if (mode == REQUEST) {
			if (link == 0 || !rspec1.isSet()) return 0;
			packPair(LINK,link);
			packRspec(RSPEC1,rspec1);
		}
		break;
	case ADD_COMTREE:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
		}
		break;
	case DROP_COMTREE:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
		}
		break;
	case GET_COMTREE:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
		} else {
			if (comtree == 0 || coreFlag == -1 ||
			    link == 0 || count == -1)
				return 0;
			packPair(COMTREE,comtree);
			packPair(CORE_FLAG,coreFlag);
			packPair(LINK,link);
			packPair(count,count);
		}
		break;
	case MOD_COMTREE:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
			if (coreFlag != -1) packPair(CORE_FLAG,coreFlag);
			if (link != 0) packPair(LINK,link);
		}
		break;
	case ADD_COMTREE_LINK:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
			if (link != 0) packPair(LINK,link);
			if (coreFlag != -1) packPair(CORE_FLAG,coreFlag);
			if (ip1 != 0) packPair(IP1,ip1);
			if (port1 != 0) packPair(PORT1,port1);
			if (adr1 != 0) packPair(ADR1,adr1);
		} else {
			if (link == 0) return 0;
			packPair(LINK,link);
		}
		break;
	case DROP_COMTREE_LINK:
		if (mode == REQUEST) {
			if (comtree == 0) return 0;
			packPair(COMTREE,comtree);
			if (link != 0) packPair(LINK,link);
			if (ip1 != 0) packPair(IP1,ip1);
			if (port1 != 0) packPair(PORT1,port1);
			if (adr1 != 0) packPair(ADR1,adr1);
		}
		break;
	case MOD_COMTREE_LINK:
		if (mode == REQUEST) {
			if (comtree == 0 || link == 0) return 0;
			packPair(COMTREE,comtree);
			packPair(LINK,link);
			if (rspec1.isSet()) packRspec(RSPEC1,rspec1);
		}
		break;
	case GET_COMTREE_LINK:
		if (mode == REQUEST) {
			if (comtree == 0 || link == 0) return 0;
			packPair(COMTREE,comtree);
			packPair(LINK,link);
		} else {
			if (comtree == 0 || link == 0 ||
			    !rspec1.isSet() || queue == 0 || adr1 == 0)
				return 0;
			packPair(COMTREE,comtree);
			packPair(LINK,link);
			packRspec(RSPEC1,rspec1);
			packPair(QUEUE,queue);
			packPair(ADR1,adr1);
		}
		break;
	case ADD_ROUTE:
		if (mode == REQUEST) {
			if (comtree == 0 || adr1 == 0 || link == 0) return 0;
			packPair(COMTREE,comtree);
			packPair(ADR1,adr1);
			packPair(LINK,link);
			if (queue != 0) packPair(QUEUE,queue);
		}
		break;
	case DROP_ROUTE:
		if (mode == REQUEST) {
			if (comtree == 0 || adr1 == 0) return 0;
			packPair(COMTREE,comtree);
			packPair(ADR1,adr1);
		}
		break;
	case GET_ROUTE:
		if (mode == REQUEST) {
			if (comtree == 0 || adr1 == 0) return 0;
			packPair(COMTREE,comtree);
			packPair(ADR1,adr1);
		} else {
			if (comtree == 0 || adr1 == 0 || link == 0)
				return 0;
			packPair(COMTREE,comtree);
			packPair(ADR1,adr1);
			packPair(LINK,link);
		}
		break;
	case MOD_ROUTE:
		if (mode == REQUEST) {
			if (comtree == 0 || adr1 == 0) return 0;
			packPair(COMTREE,comtree);
			packPair(ADR1,adr1);
			if (link != 0) packPair(LINK,link);
			if (queue != 0) packPair(QUEUE,queue);
		}
		break;
	case ADD_ROUTE_LINK:
		if (mode == REQUEST) {
			if (comtree == 0 || adr1 == 0 || link == 0) return 0;
			packPair(COMTREE,comtree);
			packPair(ADR1,adr1);
			packPair(LINK,link);
		}
		break;
	case DROP_ROUTE_LINK:
		if (mode == REQUEST) {
			if (comtree == 0 || adr1 == 0 || link == 0) return 0;
			packPair(COMTREE,comtree);
			packPair(ADR1,adr1);
			packPair(LINK,link);
		}
		break;
	case NEW_SESSION:
		if (mode == REQUEST) {
			if (ip1 == 0 || !rspec1.isSet()) return 0;
			packPair(IP1,ip1);
			packRspec(RSPEC1,rspec1);
		} else {
			if (adr1 == 0 || adr2 == 0 || adr3 == 0 ||
			    ip1 == 0 || nonce == 0)
				return 0;
			packPair(ADR1,adr1);
			packPair(ADR2,adr2);
			packPair(ADR3,adr3);
			packPair(IP1,ip1);
			packPair(PORT1,port1);
			packNonce(NONCE,nonce);
		}
		break;
	case CANCEL_SESSION:
		if (mode == REQUEST) {
			if (adr1 == 0 || adr2 == 0) return 0;
			packPair(ADR1,adr1); packPair(ADR2,adr2);
		}
	case CLIENT_CONNECT:
		if (mode == REQUEST) {
			if (adr1 == 0 || adr2 == 0) return 0;
			packPair(ADR1,adr1);
			packPair(ADR2,adr2);
		}
		break;
	case CLIENT_DISCONNECT:
		if (mode == REQUEST) {
			if (adr1 == 0 || adr2 == 0) return 0;
			packPair(ADR1,adr1);
			packPair(ADR2,adr2);
		}
		break;
	case CONFIG_LEAF:
		if (mode == REQUEST) {
			if (adr1 == 0 || adr2 == 0 || ip1 == 0 ||
			    port1 == 0 || nonce == 0) return 0;
			packPair(ADR1,adr1); packPair(ADR2,adr2);
			packPair(IP1,ip1);   packPair(PORT1,port1);
			packNonce(NONCE,nonce);
		}
		break;
	case SET_LEAF_RANGE:
		if (mode == REQUEST) {
			if (adr1 == 0 || adr2 == 0) return 0;
			packPair(ADR1,adr1); packPair(ADR2,adr2);
		}
		break;
	case GET_LINK_SET: //Fend and Doowon
		if (mode == POS_REPLY) { //POS_REPLY
			int num = 0, i = 0;
			payload[pp++] = htonl(CtlPkt::LINKTABLE);
            pp++; //for the space for number of links.
            (firstLinkNum == 1) ? i = lt->firstLink() : i = firstLinkNum; //1 means the first
            cout << __FILE__ <<  " " << __FUNCTION__ << " REPLY - first link num " << firstLinkNum << " numOfLinks " << numOfLinks << endl;
            while (true){
            // for (i = lt->firstLink(); i != 0; i = lt->nextLink(i)) {
                packPair(LINK, 		i);
                packPair(IFACE, 	lt->getIface(i));
                packPair(IP1, 		lt->getPeerIpAdr(i));
                packPair(PORT1, 	lt->getPeerPort(i));
                packPair(NODE_TYPE, lt->getPeerType(i));
                packPair(ADR1, 		lt->getPeerAdr(i));
                packRspec(RSPEC1, 	lt->getRates(i));
                packNonce(NONCE, 	nonce);
                payload[pp++] = htonl(CtlPkt::LINKSET);
                string peerType;
                Forest::nodeType2string(lt->getPeerType(i), peerType);
                cout << __FUNCTION__ << " GET_LINK_SET " << peerType << endl;
                i = lt->nextLink(i);
                cout << __FUNCTION__ << " GET_LINK_SET " << i << " Num " << num << " numOfLinks " << numOfLinks << endl;
                if ((++num >= numOfLinks) || i == 0){
                	packPair(NEXT_LINK_NUM, i);
                	cout << __FUNCTION__ << " next link num " << i << endl;
                	break;
                }
            }
            payload[5] = htonl(num);
		} else if (mode == REQUEST){ //packing first link # and # of links
			packPair(FIRST_LINK_NUM, firstLinkNum);
			packPair(NUM_OF_LINK, numOfLinks);
			cout << "REQUEST -" << " first link num " << firstLinkNum << " numOfLinks " << numOfLinks << endl;
		}

	case BOOT_ROUTER:
	case BOOT_LEAF:
	case BOOT_COMPLETE:
	case BOOT_ABORT:
		break;
	default: break;
	}
	paylen = 4*pp;
	return paylen;
}

#define unpackWord(x) (x = ntohl(payload[pp++]))
#define unpackRspec(x) ((x).set(ntohl(payload[pp++]), \
			      ntohl(payload[pp++]), \
			      ntohl(payload[pp++]), \
			      ntohl(payload[pp++]) ))

/** Unpack CtlPkt fields from the packet payload.
 *  @return true on success, 0 on failure
 */
bool CtlPkt::unpack() {
	if (payload == 0) return false;

	int pp = 0;
	uint32_t x, y;
	unpackWord(x); type = (CpType) x;
	unpackWord(x); mode = (CpMode) x;
	unpackWord(x); unpackWord(y);
	seqNum = x; seqNum <<= 32; seqNum |= y;

	if (mode == NEG_REPLY) {
		if (paylen > 4*pp && ntohl(payload[pp]) == ERRMSG) {
			pp++;
			int len = ntohl(payload[pp++]);
			errMsg.assign((char*) &payload[pp], len);
		}
		return true;
	}
	RateSpec Rates;
	uint32_t attr = 0, numLinks = 0;
	while (4*pp < paylen) { 
		unpackWord(attr);
		
		switch (attr) {
		case ADR1:	unpackWord(adr1); break;
		case ADR2:	unpackWord(adr2); break;
		case ADR3:	unpackWord(adr3); break;
		case IP1:	unpackWord(ip1); break;
		case IP2:	unpackWord(ip2); break;
		case PORT1:	unpackWord(port1); break;
		case PORT2:	unpackWord(port2); break;
		case NONCE:	uint32_t hi,lo; unpackWord(hi); unpackWord(lo);
				nonce = hi; nonce <<= 32; nonce |= lo; break;
		case RSPEC1:	unpackRspec(rspec1); break;
		case RSPEC2:	unpackRspec(rspec2); break;
		case CORE_FLAG:	unpackWord(coreFlag); break;
		case IFACE:	unpackWord(iface); break;
		case LINK:	unpackWord(link); break;
		case NODE_TYPE:	nodeType =(Forest::ntyp_t) ntohl(payload[pp++]);
				break;
		case COMTREE:	unpackWord(comtree); break;
		case COMTREE_OWNER: unpackWord(comtreeOwner); break;
		case COUNT:	unpackWord(count); break;
		case QUEUE:	unpackWord(queue); break;
		case ZIPCODE:	unpackWord(zipCode); break;		
		case LINKTABLE: //Feng and Doowon
			unpackWord(numLinks);
			// lt = new LinkTable(numLinks);
			// cout << "Num of Links " << numLinks << endl; 
			break;
		case FIRST_LINK_NUM: unpackWord(firstLinkNum); break;
		case NUM_OF_LINK: unpackWord(numOfLinks); break;
		case NEXT_LINK_NUM: unpackWord(nextLinkNum); break;
		case LINKSET: //Doowon
			cout << __FUNCTION__ << " " << link << " " << ip1 << " " << port1 << endl; 
			lt->addEntry(link, ip1, port1, nonce);
			lt->setIface(link, iface);
			lt->setPeerType(link, nodeType);
			lt->setPeerAdr(link, adr1);
			lt->getRates(link).set(rspec1.bitRateUp, rspec1.bitRateDown, rspec1.pktRateUp, rspec1.pktRateDown);
			cout << __FUNCTION__ << " " << lt << endl;
			break;
		default:	return false;
		}
	}

	switch (type) {
	case CLIENT_ADD_COMTREE:
		if ((mode == REQUEST && zipCode == 0) ||
		    (mode == POS_REPLY && comtree == 0))
			return false;
		break;
	case CLIENT_DROP_COMTREE:
		if ((mode == REQUEST && comtree == 0))
			return false;
		break;
	case CLIENT_GET_COMTREE:
		if ((mode == REQUEST && comtree == 0) ||
		    (mode == POS_REPLY &&
		     (comtree == 0 || comtreeOwner == 0 ||
		      !rspec1.isSet() || !rspec2.isSet())))
			return false;
		break;
	case CLIENT_MOD_COMTREE:
		if ((mode == REQUEST && 
		     (!rspec1.isSet() || !rspec2.isSet())) ||
		    (mode == POS_REPLY && comtree == 0))
			return false;
		break;
	case CLIENT_JOIN_COMTREE:
		if (mode == REQUEST && (comtree == 0 || ip1 == 0 || port1 == 0))
			return false;
		break;
	case CLIENT_LEAVE_COMTREE:
		if (mode == REQUEST && (comtree == 0 || ip1 == 0 || port1 == 0))
			return false;
		break;
	case CLIENT_RESIZE_COMTREE:
		if (mode == REQUEST && comtree == 0)
			return false;
		break;
	case CLIENT_GET_LEAF_RATE:
		if ((mode == REQUEST && 
		     (comtree == 0 || adr1 == 0)) ||
		    (mode == POS_REPLY &&
		     (comtree == 0 || adr1 == 0 || !rspec1.isSet())))
			return false;
		break;
	case CLIENT_MOD_LEAF_RATE:
		if ((mode == REQUEST && 
		     (comtree == 0 || adr1 == 0 || !rspec1.isSet())))
			return false;
		break;
	case ADD_IFACE:
		if ((mode == REQUEST && 
		     (iface == 0 || ip1 == 0 || !rspec1.isSet())) ||
		    (mode == POS_REPLY && (ip1 == 0 || port1 == 0)))
			return false;
		break;
	case DROP_IFACE:
		if (mode == REQUEST && iface == 0)
			return false;
		break;
	case GET_IFACE:
		if ((mode == REQUEST && iface == 0) ||
		    (mode == POS_REPLY &&
		     (iface == 0 || ip1 == 0 || port1 == 0 ||
		      !rspec1.isSet() || !rspec2.isSet())))
			return false;
		break;
	case MOD_IFACE:
		if ((mode == REQUEST && 
		     (iface == 0 || !rspec1.isSet())))
			return false;
		break;
	case ADD_LINK:
		if (mode == REQUEST && (nodeType == 0 || iface == 0))
			return false;
		break;
	case DROP_LINK:
		if (mode == REQUEST && link == 0 && adr1 == 0)
			return false;
		break;
	case GET_LINK:
		if ((mode == REQUEST && link == 0) ||
		    (mode == POS_REPLY &&
		     (link == 0 || iface == 0 || nodeType == 0 ||
		      ip1 == 0 || port1 == 0 || adr1 == 0 ||
		      !rspec1.isSet() || !rspec2.isSet())))
			return false;
		break;
	case MOD_LINK:
		if (mode == REQUEST && link == 0)
			return false;
		break;
	case ADD_COMTREE:
		if (mode == REQUEST && comtree == 0)
			return false;
		break;
	case DROP_COMTREE:
		if (mode == REQUEST && comtree == 0)
			return false;
		break;
	case GET_COMTREE:
		if ((mode == REQUEST && comtree == 0) ||
		    (mode == POS_REPLY &&
		     (comtree == 0 || coreFlag == -1 || link == 0 ||
		      count == 0)))
			return false;
		break;
	case MOD_COMTREE:
		if (mode == REQUEST && comtree == 0)
			return false;
		break;
	case ADD_COMTREE_LINK:
		if (mode == REQUEST && (comtree == 0 ||
		    (link == 0	&& (ip1 == 0 || port1 == 0)
				&& (adr1 == 0))))
			return false;
		break;
	case DROP_COMTREE_LINK:
		if (mode == REQUEST && (comtree == 0 ||
		    (link == 0	&& (ip1 == 0 || port1 == 0)
				&& (adr1 == 0))))
			return false;
		break;
	case MOD_COMTREE_LINK:
		if (mode == REQUEST && (comtree == 0 || link == 0))
			return false;
		break;
	case GET_COMTREE_LINK:
		if ((mode == REQUEST &&
		     (comtree == 0 || link == 0)) ||
		    (mode == POS_REPLY &&
		     (comtree == 0 || link == 0 || !rspec1.isSet() ||
		      queue == 0 || adr1 == 0)))
			return false;
		break;
	case ADD_ROUTE:
		if (mode == REQUEST && (comtree == 0 || adr1 == 0 || link == 0))
			return false;
		break;
	case DROP_ROUTE:
		if (mode == REQUEST && (comtree == 0 || adr1 == 0))
			return false;
		break;
	case GET_ROUTE:
		if ((mode == REQUEST &&
		     (comtree == 0 || adr1 == 0)) ||
		    (mode == POS_REPLY &&
		     (comtree == 0 || adr1 == 0 || link == 0)))
			return false;
		break;
	case MOD_ROUTE:
		if (mode == REQUEST && (comtree == 0 || adr1 == 0))
			return false;
		break;
	case ADD_ROUTE_LINK:
		if (mode == REQUEST &&
		    (comtree == 0 || adr1 == 0 || link == 0))
			return false;
		break;
	case DROP_ROUTE_LINK:
		if (mode == REQUEST &&
		    (comtree == 0 || adr1 == 0 || link == 0))
			return false;
		break;
	case NEW_SESSION:
		if ((mode == REQUEST &&
		     (ip1 == 0 || !rspec1.isSet())) ||
		    (mode == POS_REPLY &&
		     (adr1 == 0 || adr2 == 0 || adr3 == 0 ||
		      ip1 == 0 || nonce == 0)))
			return false;
		break;
	case CANCEL_SESSION:
		if (mode == REQUEST && (adr1 == 0 || adr2 == 0))
			return false;
		break;
	case CLIENT_CONNECT:
		if (mode == REQUEST && (adr1 == 0 || adr2 == 0))
			return false;
		break;
	case CLIENT_DISCONNECT:
		if (mode == REQUEST && (adr1 == 0 || adr2 == 0))
			return false;
		break;
	case CONFIG_LEAF:
		if (mode == REQUEST &&
		    (adr1 == 0 || adr2 == 0 || ip1 == 0 || port1 == 0 ||
		     nonce == 0))
			return false;
		break;
	case SET_LEAF_RANGE:
		if (mode == REQUEST && (adr1 == 0 || adr2 == 0)) return false;
		break;
	//feng
	case GET_LINK_SET:
	case BOOT_ROUTER:
	case BOOT_LEAF:
	case BOOT_COMPLETE:
	case BOOT_ABORT:
		break;
	default: return false;
	}
	return true;
}

/** Create a string representing an (attribute,value) pair.
 *  @param attr is an attribute code
 *  @param s is a reference to a string in which result is returned
 *  @return a reference to s
 */
string& CtlPkt::avPair2string(CpAttr attr, string& s) {
	stringstream ss;
	switch (attr) {
	case ADR1:
		if (adr1 != 0) ss << "adr1=" << Forest::fAdr2string(adr1,s);
		break;
	case ADR2:
		if (adr2 != 0) ss << "adr2=" << Forest::fAdr2string(adr2,s);
		break;
	case ADR3:
		if (adr3 != 0) ss << "adr3=" << Forest::fAdr2string(adr3,s);
		break;
	case IP1:
		if (ip1 != 0) ss << "ip1=" << Np4d::ip2string(ip1,s);
		break;
	case IP2:
		if (ip2 != 0) ss << "ip2=" << Np4d::ip2string(ip2,s);
		break;
	case PORT1:
		if (port1 != 0) ss << "port1=" << port1;
		break;
	case PORT2:
		if (port2 != 0) ss << "port2=" << port2;
		break;
	case RSPEC1:
		if (rspec1.isSet()) ss << "rspec1=" << rspec1.toString(s);
		break;
	case RSPEC2:
		if (rspec2.isSet()) ss << "rspec2=" << rspec2.toString(s);
		break;
	case CORE_FLAG:
		if (coreFlag >= 0)
			ss << "coreFlag=" << (coreFlag ? "true" : "false");
		break;
	case IFACE:
		if (iface != 0) ss << "iface=" << iface;
		break;
	case LINK:
		if (link != 0) ss << "link=" << link;
		break;
	case NODE_TYPE:
		if (nodeType != Forest::UNDEF_NODE)
			ss << "nodeType="
			   << Forest::nodeType2string(nodeType,s);
		break;
	case COMTREE:
		if (comtree != 0) ss << "comtree=" << comtree;
		break;
	case COMTREE_OWNER:
		ss << "comtreeOwner=" << Forest::fAdr2string(comtreeOwner,s);
		break;
	case COUNT:
		if (count >= 0) ss << "count=" << count;
		break;
	case QUEUE:
		if (queue != 0) ss << "queue=" << queue;
		break;
	case NONCE:
		if (nonce != 0) ss << "nonce=" << nonce;
		break;
	case ZIPCODE:
		if (zipCode != 0) ss << "zipCode=" << zipCode;
		break;
	case ERRMSG:
		if (errMsg.length() != 0) ss << "errMsg=" << errMsg;
		break;
	default: break;
	}
	s = ss.str();
	return s;
}

string& CtlPkt::typeName(string& s) {
	switch (type) {
	case CLIENT_ADD_COMTREE: s = "client add comtree"; break;
	case CLIENT_DROP_COMTREE: s = "client drop comtree"; break;
	case CLIENT_GET_COMTREE: s = "client get comtree"; break;
	case CLIENT_MOD_COMTREE: s = "client mod comtree"; break;
	case CLIENT_JOIN_COMTREE: s = "client join comtree"; break;
	case CLIENT_LEAVE_COMTREE: s = "client leave comtree"; break;
	case CLIENT_RESIZE_COMTREE: s = "client resize comtree"; break;
	case CLIENT_GET_LEAF_RATE: s = "client get leaf rate"; break;
	case CLIENT_MOD_LEAF_RATE: s = "client mod leaf rate"; break;
	case ADD_IFACE: s = "add iface"; break;
	case DROP_IFACE: s = "drop iface"; break;
	case GET_IFACE: s = "get iface"; break;
	case MOD_IFACE: s = "mod iface"; break;
	case ADD_LINK: s = "add link"; break;
	case DROP_LINK: s = "drop link"; break;
	case GET_LINK: s = "get link"; break;
	case MOD_LINK: s = "mod link"; break;
	case ADD_COMTREE: s = "add comtree"; break;
	case DROP_COMTREE: s = "drop comtree"; break;
	case GET_COMTREE: s = "get comtree"; break;
	case MOD_COMTREE: s = "mod comtree"; break;
	case ADD_COMTREE_LINK: s = "add comtree link"; break;
	case DROP_COMTREE_LINK: s = "drop comtree link"; break;
	case MOD_COMTREE_LINK: s = "mod comtree link"; break;
	case GET_COMTREE_LINK: s = "get comtree link"; break;
	case ADD_ROUTE: s = "add route"; break;
	case DROP_ROUTE: s = "drop route"; break;
	case GET_ROUTE: s = "get route"; break;
	case MOD_ROUTE: s = "mod route"; break;
	case ADD_ROUTE_LINK: s = "add route link"; break;
	case DROP_ROUTE_LINK: s = "drop route link"; break;
	case NEW_SESSION: s = "new session"; break;
	case CANCEL_SESSION: s = "cancel session"; break;
	case CLIENT_CONNECT: s = "client connect"; break;
	case CLIENT_DISCONNECT: s = "client disconnect"; break;
	case CONFIG_LEAF: s = "config leaf"; break;
	case SET_LEAF_RANGE: s = "set leaf range"; break;
	case BOOT_ROUTER: s = "boot router"; break;
	case BOOT_LEAF: s = "boot leaf"; break;
	case BOOT_COMPLETE: s = "boot complete"; break;
	case BOOT_ABORT: s = "boot abort"; break;
	default: break;
	}
	return s;
}

string& CtlPkt::modeName(string& s) {
	switch (mode) {
	case REQUEST: s = "request"; break;
	case POS_REPLY: s = "pos reply"; break;
	case NEG_REPLY: s = "neg reply"; break;
	default: break;
	}
	return s;
}

string& CtlPkt::toString(string& s) {
	stringstream ss;

	//if (payload != 0) unpack();
	ss << typeName(s);
	ss << " (" << modeName(s) << "," << seqNum << "): ";
	if (mode == NEG_REPLY) {
		ss << errMsg << endl;
		s = ss.str();
		return s;
	}
	switch (type) {
	case CLIENT_ADD_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(ZIPCODE,s);
		} else {
			ss << " " << avPair2string(COMTREE,s);
		}
		break;
	case CLIENT_DROP_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
		}
		break;
	case CLIENT_GET_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
		} else {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(COMTREE_OWNER,s);
			ss << " " << avPair2string(RSPEC1,s);
			ss << " " << avPair2string(RSPEC2,s);
		}
		break;
	case CLIENT_MOD_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(RSPEC1,s);
			ss << " " << avPair2string(RSPEC2,s);
		} else {
			ss << " " << avPair2string(COMTREE,s);
		}
		break;
	case CLIENT_JOIN_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(PORT1,s);
		}
		break;
	case CLIENT_LEAVE_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
		}
		break;
	case CLIENT_RESIZE_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
		}
		break;
	case CLIENT_GET_LEAF_RATE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(ADR1,s);
		} else {
			ss << " " << avPair2string(RSPEC1,s);
		}
		break;
	case CLIENT_MOD_LEAF_RATE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(RSPEC1,s);
		}
		break;
	case ADD_IFACE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(IFACE,s);
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(RSPEC1,s);
		} else {
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(PORT1,s);
		}
		break;
	case DROP_IFACE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(IFACE,s);
		}
		break;
	case GET_IFACE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(IFACE,s);
		} else {
			ss << " " << avPair2string(IFACE,s);
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(RSPEC1,s);
			ss << " " << avPair2string(RSPEC2,s);
		}
		break;
	case MOD_IFACE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(IFACE,s);
			ss << " " << avPair2string(RSPEC1,s);
		}
		break;
	case ADD_LINK:
		if (mode == REQUEST) {
			ss << " " << avPair2string(IFACE,s);
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(NODE_TYPE,s);
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(PORT1,s);
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(NONCE,s);
		} else {
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(IP1,s);
		}
		break;
	case DROP_LINK:
		if (mode == REQUEST) {
			if (link != 0) ss << " " << avPair2string(LINK,s);
			if (adr1 != 0) ss << " " << avPair2string(ADR1,s);
		}
		break;
	case GET_LINK:
		if (mode == REQUEST) {
			ss << " " << avPair2string(LINK,s);
		} else {
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(IFACE,s);
			ss << " " << avPair2string(NODE_TYPE,s);
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(PORT1,s);
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(RSPEC1,s);
			ss << " " << avPair2string(RSPEC2,s);
		}
		break;
	case MOD_LINK:
		if (mode == REQUEST) {
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(RSPEC1,s);
		}
		break;
	case ADD_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
		}
		break;
	case DROP_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
		}
		break;
	case GET_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
		} else {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(CORE_FLAG,s);
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(COUNT,s);
		}
		break;
	case MOD_COMTREE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(CORE_FLAG,s);
			ss << " " << avPair2string(LINK,s);
		}
		break;
	case ADD_COMTREE_LINK:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(CORE_FLAG,s);
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(PORT1,s);
			ss << " " << avPair2string(ADR1,s);
		} else {
			ss << " " << avPair2string(LINK,s);
		}
		break;
	case DROP_COMTREE_LINK:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(PORT1,s);
			ss << " " << avPair2string(ADR1,s);
		}
		break;
	case MOD_COMTREE_LINK:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(RSPEC1,s);
		}
		break;
	case GET_COMTREE_LINK:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(LINK,s);
		} else {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(RSPEC1,s);
			ss << " " << avPair2string(QUEUE,s);
			ss << " " << avPair2string(ADR1,s);
		}
		break;
	case ADD_ROUTE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(QUEUE,s);
		}
		break;
	case DROP_ROUTE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(ADR1,s);
		}
		break;
	case GET_ROUTE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(ADR1,s);
		} else {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(LINK,s);
		}
		break;
	case MOD_ROUTE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(LINK,s);
			ss << " " << avPair2string(QUEUE,s);
		}
		break;
	case ADD_ROUTE_LINK:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(LINK,s);
		}
		break;
	case DROP_ROUTE_LINK:
		if (mode == REQUEST) {
			ss << " " << avPair2string(COMTREE,s);
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(LINK,s);
		}
		break;
	case NEW_SESSION:
		if (mode == REQUEST) {
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(RSPEC1,s);
		} else {
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(ADR2,s);
			ss << " " << avPair2string(ADR3,s);
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(PORT1,s);
			ss << " " << avPair2string(NONCE,s);
		}
		break;
	case CANCEL_SESSION:
		if (mode == REQUEST) {
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(ADR2,s);
		} 
		break;
	case CLIENT_CONNECT:
		if (mode == REQUEST) {
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(ADR2,s);
		}
		break;
	case CLIENT_DISCONNECT:
		if (mode == REQUEST) {
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(ADR2,s);
		}
		break;
	case CONFIG_LEAF:
		if (mode == REQUEST) {
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(ADR2,s);
			ss << " " << avPair2string(IP1,s);
			ss << " " << avPair2string(PORT1,s);
			ss << " " << avPair2string(NONCE,s);
		}
		break;
	case SET_LEAF_RANGE:
		if (mode == REQUEST) {
			ss << " " << avPair2string(ADR1,s);
			ss << " " << avPair2string(ADR2,s);
		}
		break;
	//feng
	case GET_LINK_SET:
		break; 
	case BOOT_ROUTER:
	case BOOT_LEAF:
	case BOOT_COMPLETE:
	case BOOT_ABORT:
		break;
	default: break;
	}
	ss << endl;
	s = ss.str();
	return s;
}

} // ends namespace

