/** @file ClientTable.h
 *
 *  @author Jon Turner
 *  @date 2011
 *  This is open source software licensed under the Apache 2.0 license.
 *  See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#ifndef CLIENTTABLE_H
#define CLIENTTABLE_H

#include <map>
#include "Forest.h"
#include "RateSpec.h"
#include "UiClist.h"
#include "UiSetPair.h"
#include "IdMap.h"

namespace forest {

/** Class that implements a table of information about users.
 *
 *  Table entries are accessed using a "client index", which
 *  can be obtained using the getClient() method. This also
 *  locks the client's table entry to permit exclusive access
 *  to the data for that client and its sessions.
 *  Other methods also lock table entries.
 */
class ClientTable {
public:
		ClientTable(int,int);
		~ClientTable();
	bool	init();

	enum sessionState { NUL_STATE, IDLE, PENDING, SUSPENDED, CONNECTED };
	enum privileges { NUL_PRIV, LIMITED, STANDARD, ADMIN, ROOT };

	// global parameters
	const RateSpec& getDefRates() const;
	const RateSpec& getTotalRates() const;

	bool	validClient(int) const;

	// iteration methods
	int	firstClient();
	int	nextClient(int);
	int	firstSession(int) const;
	int	nextSession(int, int) const;

	// access routines for client info
	bool	isLocked(int) const;
	int	getClient(const string&);		
	void	releaseClient(int);
	int	getSession(fAdr_t);		
	const string& getPassword(int) const;
	const string& getClientName(int) const;
	const string& getRealName(int) const;
	const string& getEmail(int) const;
	privileges getPrivileges(int) const;
	RateSpec& getDefRates(int) const;
	RateSpec& getTotalRates(int) const;
	RateSpec& getAvailRates(int) const;
	int	getNumSess(int) const;		
	bool	checkPassword(int,string&) const;

	// for session info
	int	getNumClients() const;		
	int	getMaxClients() const;		
	int	getMaxClx() const;		
	int	getClientIndex(int) const;		
	fAdr_t	getClientAdr(int) const;
	ipa_t	getClientIp(int) const;
	fAdr_t	getRouterAdr(int) const;
	sessionState getState(int) const;
	time_t	getStartTime(int) const;
	RateSpec& getSessRates(int) const;

	// add/remove/modify table entries
	int	addClient(string&, string&, privileges, int=0);
	void	removeClient(int);
	int	addSession(fAdr_t, fAdr_t, int);
	void	removeSession(int);

	void	setClientName(int, const string&);
	void	setPassword(int, const string&);
	void	setPrivileges(int, privileges);
	void	setRealName(int, const string&);
	void	setEmail(int, const string&);
	void	setClientIndex(int,int);		
	//void	setClientAdr(int,fAdr_t);
	void	setClientIp(int,ipa_t);
	void	setRouterAdr(int,fAdr_t);
	void	setState(int,sessionState);
	void	setStartTime(int, time_t);

	// input/output of table contents 
	//bool 	readRecord(NetBuffer&, int);
	bool 	readEntry(istream&, int=0);
	bool 	read(istream&);
	string&	toString(string&, bool=false);
	string&	client2string(int, string&, bool=false) const;
	string&	session2string(int, string&) const;
	void 	write(ostream&, bool=false);

	// locking/unlocking the internal maps
	// (client name-to-index, client address-to-session)
	void	lockMap();
	void	unlockMap();
private:
	int	maxCli;			///< max number of clients
	int	maxSess;		///< max number of active sessions
	int	maxClx;			///< largest defined clx

	struct Session {
	fAdr_t	cliAdr;			///< address of user in session
	ipa_t	cliIp;			///< client's login IP
	fAdr_t	rtrAdr;			///< address of router user connects to
	int	clx;			///< client index for user of session
	sessionState state;
	time_t	start;			///< start time of session
					///< (in seconds  since the epoch)
	RateSpec rates;			///< rates used for this session
	};
	Session *svec;			///< vector of session structs
	UiClist *sessLists;		///< circular lists of session indexes

	struct Client { 		///< client table entry
	string	cname;			///< client's login name
	string	pwd;			///< password
	privileges priv;		///< client privileges
	string	realName;		///< real world name
	string	email;			///< email address
	RateSpec defRates;		///< default rate spec for this user
	RateSpec totalRates;		///< total allowed for all sessions
	RateSpec availRates;		///< amount of unallocated capacity
	int	numSess;		///< number of active sessions
	int	firstSess;		///< index of first session in list
	bool	busyBit;		///< set for a busy client
	pthread_cond_t busyCond;	///< used to wait for a busy client
	};

	RateSpec defRates;		///< initial default rates
	RateSpec totalRates;		///< initial total rates
	
	Client *cvec;			///< vector of client structs

	static const int RECORD_SIZE = 256; ///< # of bytes per record
	fstream	clientFile;		///< file stream for client file

	UiSetPair *clients;		///< active and free client indexes
	IdMap *sessMap;			///< maps address to a session index
        map<string, int> *nameMap;	///< maps client name to client index

	pthread_mutex_t mapLock;	///< must hold during add/remove ops
					///< and while locking client

	/** helper functions */
	uint64_t key(fAdr_t) const;
	bool 	readEntry(istream&);
	int	fileSize();

};

inline bool ClientTable::validClient(int clx) const {
	return clients->isIn(clx);
}

inline int ClientTable::getNumClients() const {
	return clients->getNumIn();
}

inline int ClientTable::getMaxClients() const { return maxCli; }
inline int ClientTable::getMaxClx() const { return maxClx; }

/** Get the intial default rate spec for new clients.
 *  @return a const reference to the initial default rate spec
 */
inline const RateSpec& ClientTable::getDefRates() const {
	return defRates;
}

/** Get the intial total rate spec for new clients.
 *  @return a const reference to the initial total rate spec
 */
inline const RateSpec& ClientTable::getTotalRates() const {
	return totalRates;
}

inline bool ClientTable::isLocked(int clx) const {
	return cvec[clx].busyBit;
}

/** Get the first session index for a client.
 *  This method is used to iterate through the active sessions of a client.
 *  The order of the sessions indices is arbitrary.
 *  The caller should already hold a lock for the client.
 *  @param clx is a client index
 *  @return the first session index for clx, or 0 if there
 *  is no session
 */
inline int ClientTable::firstSession(int clx) const {
	return cvec[clx].firstSess;
}

/** Get the next session index for a client.
 *  This method is used to iterate through the active sessions of a client.
 *  The order of the sessions indices is arbitrary.
 *  The caller should already hold a lock for the client.
 *  @param sess is a session index
 *  @param clx is a client index
 *  @return the next session index following sess in the list for clx,
 *  or 0 if there is no next index
 */
inline int ClientTable::nextSession(int sess, int clx) const {
	return (sessLists->suc(sess) == cvec[clx].firstSess ?
		0 : sessLists->suc(sess));
}

/** Get a client's password.
 *  The caller should already hold a lock for the client.
 *  @param clx is a valid client index
 *  @return a const reference to the password string.
 */
inline const string& ClientTable::getPassword(int clx) const {
	return cvec[clx].pwd;
}

/** Get a client's name.
 *  @param clx is a valid client index
 *  @return a const reference to the client name string
 */
inline const string& ClientTable::getClientName(int clx) const {
	return cvec[clx].cname;
}

/** Get the number of sessions for a client.
 *  @param clx is a valid client index
 *  @return a the number of active or pending sessions for this client.
 */
inline int ClientTable::getNumSess(int clx) const {
	return cvec[clx].numSess;
}

/** Get a client's password.
 *  @param clx is a valid client index
 *  @param pwd is a reference to a string containing the client's password
 *  @return true if the provided password is consistent with the stored value
 */
inline bool ClientTable::checkPassword(int clx, string& pwd) const {
	return cvec[clx].pwd == pwd;
}

/** Get a client's real world name.
 *  @param clx is a valid client index
 *  @return a const reference to the client's realName string
 */
inline const string& ClientTable::getRealName(int clx) const {
	return cvec[clx].realName;
}

/** Get a client's email address.
 *  @param clx is a valid client index
 *  @return a const reference to the client's email string
 */
inline const string& ClientTable::getEmail(int clx) const {
	return cvec[clx].email;
}

/** Get a client's privileges.
 *  @param clx is a valid client index
 *  @return the client's privileges.
 */
inline ClientTable::privileges ClientTable::getPrivileges(int clx) const {
	return cvec[clx].priv;
}

/** Get the default rate spec for a client.
 *  @param clx is a client index
 *  @return a non-const reference to the rate spec for clx
 */
inline RateSpec& ClientTable::getDefRates(int clx) const {
	return cvec[clx].defRates;
}

/** Get the available rate spec for a client.
 *  @param clx is a client index
 *  @return a non-const reference to the rate spec for clx
 */
inline RateSpec& ClientTable::getAvailRates(int clx) const {
	return cvec[clx].availRates;
}

/** Get the total rate spec for a client.
 *  @param clx is a client index
 *  @return a non-const reference to the total rate spec for clx
 */
inline RateSpec& ClientTable::getTotalRates(int clx) const {
	return cvec[clx].totalRates;
}

/** Get the client address for a session.
 *  @param sess is a session index
 *  @return the Forest address of the client in this session
 */
inline fAdr_t ClientTable::getClientAdr(int sess) const {
	return svec[sess].cliAdr;
}

/** Get the client address for a session.
 *  @param sess is a session index
 *  @return the Forest address of the client in this session
 */
inline ipa_t ClientTable::getClientIp(int sess) const {
	return svec[sess].cliIp;
}

/** Get the index of a client for a given session.
 *  @param sess is the session index
 *  @return the client index, or 0 if not a valid session
 */
inline int ClientTable::getClientIndex(int sess) const {
	return svec[sess].clx;
}

/** Get the address of the client's router for a session.
 *  @param sess is a session index
 *  @return the Forest address of the router used by the client in this session
 */
inline fAdr_t ClientTable::getRouterAdr(int sess) const {
	return svec[sess].rtrAdr;
}

/** Get the address of the client's router for a session.
 *  @param sess is a session index
 *  @return the time this session started (in seconds since the epoch)
 */
inline time_t ClientTable::getStartTime(int sess) const {
	return svec[sess].start;
}

/** Get the rate spec for a session.
 *  @param sess is a session index
 *  @return a non-const reference to the rate spec for the session
 */
inline RateSpec& ClientTable::getSessRates(int sess) const {
	return svec[sess].rates;
}

/** Set a client's name.
 *  @param clx is a valid client index
 *  @return a const reference to the client name string
 */
inline void ClientTable::setClientName(int clx, const string& cname) {
	cvec[clx].cname = cname;
}

/** Set a client's password.
 *  @param clx is a valid client index
 *  @return a const reference to the password string
 */
inline void ClientTable::setPassword(int clx, const string& pwd) {
	cvec[clx].pwd = pwd;
}

/** Set a client's real world name.
 *  @param clx is a valid client index
 *  @return a const reference to the client's realName string
 */
inline void ClientTable::setRealName(int clx, const string& realName) {
	cvec[clx].realName = realName;
}

/** Set a client's email address.
 *  @param clx is a valid client index
 *  @param email is the client's new email address
 */
inline void ClientTable::setEmail(int clx, const string& email) {
	cvec[clx].email = email;
}

/** Set a client's privileges.
 *  @param clx is a valid client index
 *  @param priv is the client's new privileges
 */
inline void ClientTable::setPrivileges(int clx, ClientTable::privileges priv) {
	cvec[clx].priv = priv;
}

/** Set the client address for a session.
 *  @param sess is a session index
inline void ClientTable::setClientAdr(int sess, fAdr_t cadr) {
	svec[sess].cliAdr = cadr;
}
 */

/** Set the client IP address for a session.
 *  @param sess is a session index
 */
inline void ClientTable::setClientIp(int sess, ipa_t ipa) {
	svec[sess].cliIp = ipa;
}

/** Set the address of the client's router for a session.
 *  @param sess is a session index
 */
inline void ClientTable::setRouterAdr(int sess, fAdr_t radr) {
	svec[sess].rtrAdr = radr;
}

/** Set the state for a session.
 *  @param sess is a session index
 */
inline void ClientTable::setState(int sess, sessionState state) {
	svec[sess].state = state;
}

/** Set the start time for a session.
 *  @param sess is a session index
 */
inline void ClientTable::setStartTime(int sess, time_t t) {
	svec[sess].start = t;
}

/** Compute key for use with sessMap.
 *  @param cliAdr is a client address
 *  @return a 64 bit hash key
 */
inline uint64_t ClientTable::key(fAdr_t cliAdr) const {
        return (uint64_t(cliAdr) << 32) | cliAdr;
}

/** Lock the client table.
 *  This method is meant primarily for internal use.
 *  Applications should normally just lock single clients using the
 *  methods provided for that purpose. Use extreme caution when using
 *  this method directly.
 */
inline void ClientTable::lockMap() { pthread_mutex_lock(&mapLock); }

/** Unlock the client table.
 */
inline void ClientTable::unlockMap() { pthread_mutex_unlock(&mapLock); }

} // ends namespace


#endif
