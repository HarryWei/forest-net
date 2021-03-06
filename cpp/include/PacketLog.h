/** @file PacketLog.h
 *
 *  @author Jon Turner
 *  @date 2011
 *  This is open source software licensed under the Apache 2.0 license.
 *  See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#ifndef PACKETLOG_H
#define PACKETLOG_H

#include <thread>
#include <mutex>

using std::thread;
using std::mutex;
using std::recursive_mutex;

#include "Forest.h"
#include "Util.h"
#include "PacketStore.h"
#include "Packet.h"
#include "CtlPkt.h"
#include "PacketFilter.h"
#include "ListPair.h"

namespace forest {

typedef int fltx;	// filter index

class PacketLog {
public:
		PacketLog(PacketStore*);
		~PacketLog();

	fltx	firstFilter();
	fltx	nextFilter(fltx);

	bool	validFilter(fltx);

	void	enable(fltx);
	void	disable(fltx);
	fltx	addFilter();
	void	dropFilter(fltx);
	PacketFilter& getFilter(fltx);
	void	turnOnLogging(bool);
	void	enableLocalLog(bool);

	int	size();
	void 	log(int,int,bool,uint64_t);	
	int	extract(int, string&);
	void	write(ostream&);
	void	purge();

private:
	static const int MAX_EVENTS=10000;	///< max # of event records
	static const int MAX_FILTERS=100;	///< max # of filters

	bool	logOn;			///< turns on capture of packets
	bool	logLocal;		///< if true, dump events to cout
	uint64_t dumpTime;		///< next time to dump events to cout
	int	numOut;			///< number of packets sent to cout
	int	numDataOut;		///< number of data packets sent to cout

	static const int OUT_LIMIT = 50000; ///< max # pkt records to cout
	static const int DATA_OUT_LIMIT = 10000; ///< max # data pkts to cout

	struct EventStruct {
	pktx px;			///< index of some packet
        int sendFlag;			///< true for outgoing packets
	int link;			///< link used by packet
	uint64_t time;			///< time packet was logged
        };
	EventStruct *evec;

	int	eventCount;		///< count of # of events recorded
	int	firstEvent;		///< oldest event in evec
	int	lastEvent;		///< newest event in evec

	PacketFilter *fvec;		///< table of filters
	ListPair *filters;		///< in-use/free filter indexes

	PacketStore *ps;

	void 	loggit(int,int,bool,uint64_t);	
	bool	match(fltx, pktx, int, bool) const;
	static string nstime2string(uint64_t);

	recursive_mutex mtx;
};

inline int PacketLog::size() {
	unique_lock<recursive_mutex> lck(mtx);
	return eventCount;
}

inline int PacketLog::firstFilter() {
	unique_lock<recursive_mutex> lck(mtx);
	return filters->firstIn();
}

inline int PacketLog::nextFilter(int f) {
	unique_lock<recursive_mutex> lck(mtx);
	return filters->nextIn(f);
}

inline bool PacketLog::validFilter(int f) {
	unique_lock<recursive_mutex> lck(mtx);
	return filters->isIn(f);
}

inline void PacketLog::enable(fltx f) {
	unique_lock<recursive_mutex> lck(mtx);
	fvec[f].on = true;
}
inline void PacketLog::disable(fltx f) {
	unique_lock<recursive_mutex> lck(mtx);
	fvec[f].on = false;
}

inline void PacketLog::log(pktx px, int lnk, bool sendFlag, uint64_t now) {
	if (logOn) loggit(px,lnk,sendFlag,now);
}

inline bool PacketLog::match(fltx f, pktx px, int lnk, bool sendFlag) const {
	Packet& p = ps->getPacket(px);
	if (fvec[f].on && 
	    (fvec[f].lnk == 0 || fvec[f].lnk == lnk) &&
	    (fvec[f].comt == 0 || fvec[f].comt == p.comtree) &&
	    (fvec[f].srcAdr == 0 || fvec[f].srcAdr == p.srcAdr) &&
	    (fvec[f].dstAdr == 0 || fvec[f].dstAdr == p.dstAdr) &&
	    (fvec[f].type == Forest::UNDEF_PKT || fvec[f].type == p.type) &&
	    ((fvec[f].in && !sendFlag) || (fvec[f].out && sendFlag))) {
		if (p.type != Forest::CLIENT_SIG && p.type != Forest::NET_SIG)
			return true;
		CtlPkt::CpType cpt = (CtlPkt::CpType) ntohl(p.payload()[0]);
	    	return (fvec[f].cpType == CtlPkt::UNDEF_CPTYPE ||
		        fvec[f].cpType == cpt);
	}
	return false;
}

/** Get a reference to a packet filter.
 *  @param f is the index of some valid filter
 *  @return a reference to the PacketFilter object for f
 */
inline PacketFilter& PacketLog::getFilter(fltx f) {
	return fvec[f];
}

/** Enable or disable logging of packets.
 *  Whenever logging is enabled, purge any left-over packets.
 */
inline void PacketLog::turnOnLogging(bool on) {
	logOn = on; if (logOn) purge();
}

/** Enable or disable local logging of packets.
 *  Whenever local logging is enabled, clear counts of packets sent to cout,
 *  allowing a new batch of records to be logged to a file.
 */
inline void PacketLog::enableLocalLog(bool on) {
	logLocal = on; if (logLocal) { numOut = numDataOut = 0; }
}

/** Create a string representation of a time value based on a ns time value.
 *  The returned string gives the time as seconds and fractions of a second.
 *  @param t is the ns time
 *  @return the string
 */
inline string PacketLog::nstime2string(uint64_t t) {
	uint64_t sec = t/1000000000;
	uint64_t frac = (t/1000)%1000000;
	
	stringstream ss;
	ss << sec << "." << setfill('0') << setw(6) << frac;
	return ss.str();
}

} // ends namespace

#endif
