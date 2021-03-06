/** @file ComtreeTable.h
 *
 *  @author Jon Turner
 *  @date 2011
 *  This is open source software licensed under the Apache 2.0 license.
 *  See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#ifndef COMTREETABLE_H
#define COMTREETABLE_H

#include <set>
#include <vector>
#include "Forest.h"
#include "IdMap.h"
#include "LinkTable.h"

namespace forest {

typedef int pktx;


/** Class that implements a table of information on comtrees.
 *
 *  Table entries are accessed using a "comtree index", which
 *  can be obtained using the getComtIndex() method.
 *
 *  Information about the use of a link in a comtree can be
 *  accessed using a comtree link number. For example, using
 *  the comtree link number, you can get the queue identifier that
 *  is used for packets in a given comtree that are sent on
 *  on the link. Comtree link numbers can be obtained using
 *  the getComtLink() method.
 */
class ComtreeTable {
public:
		ComtreeTable(int,int,LinkTable*);
		~ComtreeTable();

	// predicates
	bool	validComtree(comt_t) const;		
	bool	validComtIndex(int) const;		
	bool	validComtLink(int) const;			
	bool	checkEntry(int) const;
	bool	inCore(int) const;
	bool	isLink(int, int) const;
	bool	isRtrLink(int, int) const;
	bool	isRtrLink(int) const;
	bool	isCoreLink(int, int) const;
	bool	isCoreLink(int) const;
	//feng
	bool 	isLocked(int) const;
	// iteration methods
	int	firstComtIndex() const;
	int	nextComtIndex(int) const;

	// access routines 
	int	getComtIndex(comt_t) const;		
	comt_t	getComtree(int) const;
	int	getLink(int) const;
	int	getPlink(int) const;
	int	getLinkCount(int) const;
	int	getComtLink(int, int) const;
	int	getPCLink(int) const;
	int	getLinkQ(int) const;
	fAdr_t	getDest(int) const;
	pktx	getPrunePkt(int) const;
	//feng
	RateSpec& getUpperBoundRates(int) const;
	RateSpec& getRates(int) const;
	set<int>& getLinks(int) const;
	set<int>& getRtrLinks(int) const;
	set<int>& getCoreLinks(int) const;
	set<int>& getRteSet(int) const;
	//feng
	vector<pktx>& getPktNums(int) const;

	// add/remove/modify table entries
	int	addEntry(comt_t);
	bool	removeEntry(int);
	bool 	addLink(int, int, bool, bool);
	bool 	removeLink(int, int);
	void	setCoreFlag(int, bool);
	void	setPlink(int, int);
	void	setLock(int, int);
	void    setPrunePkt(int, pktx);
	void 	addRequest(int, pktx);
	void	removeRequest(int, pktx);
	void 	setUpperBoundRates(int, RateSpec&);
	void	setLinkQ(int, int);
	void	registerRte(int,int);
	void	deregisterRte(int,int);
	//feng
	void 	cleanPktNums(int);
	
	// input/output of table contents 
	bool 	read(istream&);
	string&	toString(string&) const;
	string&	entry2string(int, string&) const;
private:
	int	maxCtx;			///< largest comtree index
	int	maxComtLink;		///< largest comtLink
	struct TblEntry { 		///< comtree table entry
	int	comt;			///< comtree for this table entry
	int	plnk;			///< parent link in comtree
	int 	lock;		        ///< lock bit
	pktx 	ppkt;			///< prune packet number 
	int	pCLnk;			///< corresponding cLnk value
	bool	cFlag;			///< true if this router is in core
	//feng
	RateSpec upperBoundRates;		///< upper bound rate for access link rate
	set<int> *comtLinks;		///< list of comtLink #s in comtree
	set<int> *rtrLinks;		///< comtLinks to other routers
	set<int> *coreLinks;		///< comtLinks to core routers
	//feng
	vector<pktx> *pktNums;		///< packet Numbers of join/leave requests
	};
	TblEntry *tbl;
	IdMap *comtMap;			///< maps comtree numbers to indices

	struct ComtLinkInfo {		
	int	ctx;			///< index of comtree for this comtLink
	int	lnk;			///< actual link # for this comtLink
	fAdr_t	dest;			///< if non-zero, allowed dest address
	int	qnum;			///< queue number used by comtree
	RateSpec rates;			///< rate spec for link (up=in,down=out)
	set<int> *rteSet;		///< set of routes that map to this link
	};
	ComtLinkInfo *clTbl;		///< cLnkTbl[cl] has info on comtLink cl
	IdMap *clMap;			///< maps (comtree,link)->comtLink

	LinkTable *lt;

	/** helper functions */
	uint64_t key(comt_t) const;
	uint64_t key(comt_t, int) const;
	bool 	readEntry(istream&);
	void	readLinks(istream&, set<int>&);	
	string&	links2string(int, string&) const;
};

/** Determine if the table has an entry for a given comtree.
 *  @param comt is a comtree number
 *  @return true if table contains an entry for comt, else false.
 */
inline bool ComtreeTable::validComtree(comt_t comt) const {
	return comtMap->validKey(key(comt));
}

/** Determine if a comtree index is being used in this table.
 *  @param cts is a comtree index
 *  @return true if the table contains an entry matching ctx, else false.
 */
inline bool ComtreeTable::validComtIndex(int ctx) const {
	return comtMap->validId(ctx);
}

/** Determine if a comtree link number is in use in this table.
 *  @param cLnk is a comtree link number
 *  @return true if cLnk is a valid comtree link number for this table,
 *  else false
 */
inline bool ComtreeTable::validComtLink(int cLnk) const {
	return clMap->validId(cLnk);
}

/** Determine if "this node" is in the core of the comtree.
 *  @param ctx is a comtree index
 *  @return true if the router is in the core, else false
 */
inline bool ComtreeTable::inCore(int ctx) const { return tbl[ctx].cFlag; }

/** Determine if a given link is part of a given comtree.
 *  @param entry is the comtree index
 *  @param lnk is the link number
 *  @return true if the specified link is part of the comtree
 */
inline bool ComtreeTable::isLink(int ctx, int lnk) const {
	if (!validComtIndex(ctx)) return false;
	return clMap->validKey(key(tbl[ctx].comt,lnk));
}

/** Determine if a given link connects to another router.
 *  @param ctx is the comtree index
 *  @param lnk is the link number
 *  @return true if the specified link connects to another router
 */
inline bool ComtreeTable::isRtrLink(int ctx, int lnk) const {
	if (!validComtIndex(ctx)) return false;
	return isRtrLink(clMap->getId(key(tbl[ctx].comt,lnk)));
}

/** Determine if a given comtree link connects to another router.
 *  @param cLnk is the comtree index
 *  @return true if the specified link connects to another router
 */
inline bool ComtreeTable::isRtrLink(int cLnk) const {
	if (cLnk == 0) return false;
	set<int>& rl = *tbl[clTbl[cLnk].ctx].rtrLinks;
	return (rl.find(cLnk) != rl.end());
}

/** Determine if a given link connects to a core node.
 *  @param ctx is the comtree index
 *  @param lnk is the link number
 *  @return true if the peer node for the link is in the comtree core
 */
inline bool ComtreeTable::isCoreLink(int ctx, int lnk) const {
	if (!validComtIndex(ctx)) return false;
	return isCoreLink(clMap->getId(key(tbl[ctx].comt,lnk)));
}

/** Determine if a given comtree link connects to a core node.
 *  @param cLnk is the comtree link number
 *  @return true if the peer node for the link is in the comtree core
 */
inline bool ComtreeTable::isCoreLink(int cLnk) const {
	if (cLnk == 0) return false;
	set<int>& cl = *tbl[clTbl[cLnk].ctx].coreLinks;
	return (cl.find(cLnk) != cl.end());
}

/** Determine if a given comtree is locked.
 *  @param ctx is the comtree number
 *  @return true if comtree is locked
 */
inline bool ComtreeTable::isLocked(int ctx) const {
        if (!validComtIndex(ctx)) return false;
        return (tbl[ctx].lock == 1);
}

inline pktx ComtreeTable::getPrunePkt(int ctx) const {
        if (!validComtIndex(ctx)) return 0;
        return tbl[ctx].ppkt;
}	
/** Get the first comtree index.
 *  This method is used to iterate through all the comtrees.
 *  The order of the comtree indices is arbitrary.
 *  @return the first comtree index or 0, if no comtrees
 *  have been defined
 */
inline int ComtreeTable::firstComtIndex() const {
	return comtMap->firstId();
}

/** Get the next comtree index.
 *  This method is used to iterate through all the comtrees.
 *  The order of the comtree indices is arbitrary.
 *  @param ctx is a comtree index
 *  @return the next comtree index following ctx, or 0 if there
 *  is no next index
 */
inline int ComtreeTable::nextComtIndex(int ctx) const {
	return comtMap->nextId(ctx);
}

/** Get the comtree number for a given table entry.
 *  @param ctx is the comtree index
 *  @return the comtree number
 */
inline comt_t ComtreeTable::getComtree(int ctx) const {
	return tbl[ctx].comt;
}

/** Get the comtree index, based on the comtree number.
 *  @param comt is the comtree number
 *  @return the comtree index
 */
inline int ComtreeTable::getComtIndex(comt_t comt) const {
	return comtMap->getId(key(comt));
}

/** Get the number of links that belong to this comtree.
 *  @param ctx is the comtree index
 *  @return the number of links in the comtree
 */
inline int ComtreeTable::getLinkCount(int ctx) const {
	return tbl[ctx].comtLinks->size();
}

/** Get the comtree link number for a given (comtree, link) pair.
 *  @param comt is a comtree number
 *  @param lnk is a link number
 *  @return the comtree link number for the given pair, or 0 if there
 *  is no comtree link for the given pair
 */
inline int ComtreeTable::getComtLink(int comt, int lnk) const {
	int cLnk = clMap->getId(key(comt,lnk));
	return cLnk;
}

/** Get the parent link for a comtree.
 *  @param ctx is the comtree index
 *  @return the link leading to the parent (in the comtree) of this router;
 *  returns 0 if the router is the root of the comtree
 */
inline int ComtreeTable::getPlink(int ctx) const { return tbl[ctx].plnk; }

/** Get the comtree link number for the parent link in a comtree.
 *  @param ctx is the comtree index
 *  @return the comtree link number for the link leading to the parent
 *  (in the comtree) of this router;
 *  returns 0 if the router is the root of the comtree
 */
inline int ComtreeTable::getPCLink(int ctx) const { return tbl[ctx].pCLnk; }

//feng
/** Get the rate spec for a given comtree link.
 *  @param cLnk is a comtree link number
 *  @return a reference to the rate spec for cLnk
 */
inline RateSpec& ComtreeTable::getUpperBoundRates(int ctx) const {
        return tbl[ctx].upperBoundRates;
}

/** Get the link number for a given comtree link.
 *  @param cLnk is a comtree link number
 *  @return the number of the associated link
 */
inline int ComtreeTable::getLink(int cLnk) const {
	return (cLnk != 0 ? clTbl[cLnk].lnk : 0);
}

/** Get the queue identifier for a given comtree link.
 *  @param cLnk is a comtree link number
 *  @return the queue identifier assigned to cLnk
 */
inline int ComtreeTable::getLinkQ(int cLnk) const {
	return clTbl[cLnk].qnum;
}

/** Get the allowed destination for packets received on a given comtree link.
 *  @param cLnk is a comtree link number
 *  @return the forest address of the designated "allowed destination"
 *  for this link
 */
inline fAdr_t ComtreeTable::getDest(int cLnk) const {
	return clTbl[cLnk].dest;
}

/** Get the rate spec for a given comtree link.
 *  @param cLnk is a comtree link number
 *  @return a reference to the rate spec for cLnk
 */
inline RateSpec& ComtreeTable::getRates(int cLnk) const {
	return clTbl[cLnk].rates;
}

/** Get a reference to the set of comtree links for a comtree.
 *  This method is provided to allow the client program
 *  to iterate through all the links in the comtree.
 *  It must not be used to modify the set of links.
 *  @param ctx is a comtree index
 *  @return a reference to a set of integers, each of
 *  which is a comtree link number
 */
inline set<int>& ComtreeTable::getLinks(int ctx) const {
	return *(tbl[ctx].comtLinks);
}

/** Get a reference to the set of comtree links leading to another router.
 *  This method is provided to allow the client program
 *  to iterate through all the "router links" in the comtree.
 *  It must not be used to modify the set of links.
 *  @param ctx is a comtree index
 *  @return a reference to a set of integers, each of
 *  which is a comtree link number
 */
inline set<int>& ComtreeTable::getRtrLinks(int ctx) const {
	return *(tbl[ctx].rtrLinks);
}

/** Get a reference to the set of comtree links leading to a core router.
 *  This method is provided to allow the client program
 *  to iterate through all the "core links" in the comtree.
 *  It must not be used to modify the set of links.
 *  @param ctx is a comtree index
 *  @return a reference to a set of integers, each of
 *  which is a comtree link number
 */
inline set<int>& ComtreeTable::getCoreLinks(int ctx) const {
	return *(tbl[ctx].coreLinks);
}

/** Get a reference to the set of comtree links leading to a core router.
 *  This method is provided to allow the client program
 *  to iterate through all the "core links" in the comtree.
 *  It must not be used to modify the set of links.
 *  @param ctx is a comtree index
 *  @return a reference to a set of integers, each of
 *  which is a comtree link number
 */
inline vector<pktx>& ComtreeTable::getPktNums(int ctx) const {
        return *(tbl[ctx].pktNums);
}

inline void ComtreeTable::cleanPktNums(int ctx) {
	tbl[ctx].pktNums->clear();
}

/** Get a reference to the set of routes registered with a comtree link.
 *  This method is provided to allow the client program
 *  to iterate through all the routes that use a comtree link.
 *  It must not be used to modify the set of routes.
 *  @param cLnk is a comtree link number
 *  @return a reference to a set of integers, each of
 *  which is a route index.
 */
inline set<int>& ComtreeTable::getRteSet(int cLnk) const {
	return *(clTbl[cLnk].rteSet);
}

/** Set the parent link for a given table entry.
 *  @param ctx is the comtree index
 *  @param plink is the number of the link to this router's parent
 *  in the comtree
 */
inline void ComtreeTable::setPlink(int ctx, int plink) {
	if (!validComtIndex(ctx)) return;
	if (plink == 0) {
		tbl[ctx].plnk = 0; tbl[ctx].pCLnk = 0;
		return;
	}
	int cLnk = clMap->getId(key(tbl[ctx].comt,plink));
	if (!validComtLink(cLnk)) return;
	if (!isRtrLink(ctx,plink)) return;
	tbl[ctx].plnk = plink; tbl[ctx].pCLnk =cLnk;
}

inline void ComtreeTable::setUpperBoundRates(int ctx, RateSpec& rs) {
	if (validComtIndex(ctx)) tbl[ctx].upperBoundRates = rs;
}

//feng
/** Set the lock bit for a comtree
 *  @param ctx is the comtree index
 *  @param lock is the lock bit, 1 means the comtree is locked
 */
inline void ComtreeTable::setLock(int ctx, int lock) {
        if (!validComtIndex(ctx)) return;
        if (tbl[ctx].lock != lock) {
                tbl[ctx].lock = lock;
                return;
        }
}

/** Set the prune request packet number for a comtree
 *  @param ctx is the comtree index
 */
inline void ComtreeTable::setPrunePkt(int ctx, pktx ppkt) {
        if (validComtIndex(ctx)) tbl[ctx].ppkt = ppkt;
}

/** Set the core flag for a given table entry.
 *  @param ctx is the comtree index
 *  @param f is the new value of the core flag for this entry
 */
inline void ComtreeTable::setCoreFlag(int ctx, bool f) {
	if (validComtIndex(ctx)) tbl[ctx].cFlag = f;
}

/** Set the queue number for a comtree link.
 *  @param cLnk is a comtree link number
 *  @param q is a queue number
 */
inline void ComtreeTable::setLinkQ(int cLnk, int q) {
	if (validComtLink(cLnk)) clTbl[cLnk].qnum = q;
}

/** Set the incoming bit rate for a comtree link.
 *  @param cLnk is a comtree link number
 *  @param br is a bit rate
inline void ComtreeTable::setInBitRate(int cLnk, int br) {
	if (validComtLink(cLnk)) clTbl[cLnk].inBitRate = br;
}
 */

/** Set the incoming packet rate for a comtree link.
 *  @param cLnk is a comtree link number
 *  @param pr is a packet rate
inline void ComtreeTable::setInPktRate(int cLnk, int pr) {
	if (validComtLink(cLnk)) clTbl[cLnk].inPktRate = pr;
}
 */

/** Set the outgoing bit rate for a comtree link.
 *  @param cLnk is a comtree link number
 *  @param br is a bit rate
inline void ComtreeTable::setOutBitRate(int cLnk, int br) {
	if (validComtLink(cLnk)) clTbl[cLnk].outBitRate = br;
}
 */

/** Register a route with a given comtree link.
 *  @param cLnk is a valid comtree link number
 *  @param rtx is an integer that is presumed to be a route index
 */
inline void ComtreeTable::registerRte(int cLnk, int rtx) {
	if (validComtLink(cLnk)) clTbl[cLnk].rteSet->insert(rtx);
}

/** Deegister a route with a given comtree link.
 *  @param cLnk is a valid comtree link number
 *  @param rtx is an integer that is presumed to be a route index
 */
inline void ComtreeTable::deregisterRte(int cLnk, int rtx) {
	if (validComtLink(cLnk)) clTbl[cLnk].rteSet->erase(rtx);
}

/** Register a route with a given comtree link.
 *  @param cLnk is a valid comtree link number
 *  @param rtx is an integer that is presumed to be a route index
 */
inline void ComtreeTable::addRequest(int ctx, pktx px) {
        if (validComtIndex(ctx)) {
		vector<pktx>::iterator pn;	
		for (pn = tbl[ctx].pktNums->begin(); pn != tbl[ctx].pktNums->end(); pn++) {
                	pktx curpx = *pn;
			if (curpx == px)
				return;
		}
		tbl[ctx].pktNums->push_back(px);
	}
}

/** Deegister a route with a given comtree link.
 *  @param cLnk is a valid comtree link number
 *  @param rtx is an integer that is presumed to be a route index
 */
inline void ComtreeTable::removeRequest(int ctx, pktx px) {
	//return;
        //if (validComtIndex(ctx)) tbl[ctx].pktNums->erase(px);
}

/** Set the outgoing packet rate for a comtree link.
 *  @param cLnk is a comtree link number
 *  @param pr is a packet rate
inline void ComtreeTable::setOutPktRate(int cLnk, int pr) {
	if (validComtLink(cLnk)) clTbl[cLnk].outPktRate = pr;
}
 */

/** Compute key for use with comtMap.
 *  @param comt is a comtree number
 *  @return a 64 bit hash key
 */
inline uint64_t ComtreeTable::key(comt_t comt) const {
        return (uint64_t(comt) << 32) | comt;
}

/** Compute key for use with clMap.
 *  @param comt is a comtree number
 *  @param lnk is a link number
 *  @return a 64 bit hash key
 */
inline uint64_t ComtreeTable::key(comt_t comt, int lnk) const {
        return (uint64_t(comt) << 32) | lnk;
}

} // ends namespace


#endif
