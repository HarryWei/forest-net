/** @file AdminTable.h
 *
 *  @author Jon Turner
 *  @date 2011
 *  This is open source software licensed under the Apache 2.0 license.
 *  See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#ifndef CLIENTTABLE_H
#define CLIENTTABLE_H

#include <mutex>
#include <condition_variable>
#include "Forest.h"
#include "RateSpec.h"
#include "ClistSet.h"
#include "ListPair.h"
#include "Hash.h"
#include "HashSet.h"
#include "HashMap.h"

using std::mutex;
using std::condition_variable;

namespace forest {

/** Class that implements a table of information about Forest administrators.
 *
 *  Table entries are accessed using an "admin index", which
 *  can be obtained using the getAdmin() method. This also
 *  locks the admin's table entry to permit exclusive access
 *  to the data for that admin.
 *  Other methods also lock table entries.
 */
class AdminTable {
public:
		AdminTable(int);
		~AdminTable();
	bool	init();

	bool	validAdmin(int) const;

	// iteration methods
	int	firstAdmin();
	int	nextAdmin(int);

	// access routines for admin info
	bool	isLocked(int) const;
	int	getAdmin(const string&);		
	void	releaseAdmin(int);
	string	getPassword(int) const;
	string	getAdminName(int) const;
	string	getRealName(int) const;
	string	getEmail(int) const;
	bool	checkPassword(int,string&) const;

	int	getNumAdmins() const;		
	int	getMaxAdmins() const;		
	int	getMaxAdx() const;		

	// add/remove/modify table entries
	int	addAdmin(string&, string&, int=0);
	void	removeAdmin(int);

	void	setAdminName(int, const string&);
	void	setPassword(int, const string&);
	void	setRealName(int, const string&);
	void	setEmail(int, const string&);

	// input/output of table contents 
	bool 	readEntry(istream&, int=0);
	bool 	read(istream&);
	string	toString();
	string	admin2string(int);
	void 	write(ostream&);

	// locking/unlocking the internal maps
	// (admin name-to-index, admin address-to-session)
	void	lockMap();
	void	unlockMap();
private:
	int	maxAdm;			///< max number of admins
	int	maxAdx;			///< largest defined adx

	struct Admin { 			///< admin table entry
	string	aname;			///< admin's login name
	string	pwd;			///< password
	string	realName;		///< real world name
	string	email;			///< email address
	bool	busyBit;		///< set for a busy entry
	condition_variable busyCond;	///< used to wait for a busy entry
	string	toString() const {
		return aname + ", " + pwd + ", \"" + realName + "\", "
			+ email + "\n";
	}
	friend	ostream& operator<<(ostream& out, const Admin& ad) {
		return out << ad.toString();
	}
	};

	static const int RECORD_SIZE = 128; ///< # of bytes per record
	fstream	adminFile;		///< file stream for admin file

	Admin	*avec;		///< vector of table entries
        HashSet<string, Hash::string>
		*nameMap;	///< maps admin name to index in avec

	mutex mapLock;			///< must hold during add/remove ops
					///< and while locking admin

	/** helper functions */
	bool 	readEntry(istream&);
	int	fileSize();
};

inline bool AdminTable::validAdmin(int adx) const {
	return nameMap->valid(adx);
}

inline int AdminTable::getNumAdmins() const {
	return nameMap->size();
}

inline int AdminTable::getMaxAdmins() const { return maxAdm; }

inline int AdminTable::getMaxAdx() const { return maxAdx; }

inline bool AdminTable::isLocked(int adx) const {
	return avec[adx].busyBit;
}

/** Get an admin's password.
 *  The caller should already hold a lock for the admin.
 *  @param adx is a valid admin index
 *  @return a const reference to the password string.
 */
inline string AdminTable::getPassword(int adx) const {
	return avec[adx].pwd;
}

/** Get an admin's name.
 *  @param adx is a valid admin index
 *  @return a const reference to the admin name string
 */
inline string AdminTable::getAdminName(int adx) const {
	return avec[adx].aname;
}

/** Check an admin's password.
 *  @param adx is a valid admin index
 *  @param pwd is a reference to a string containing the admin's password
 *  @return true if the provided password is consistent with the stored value
 */
inline bool AdminTable::checkPassword(int adx, string& pwd) const {
	return avec[adx].pwd == pwd;
}

/** Get an admin's real world name.
 *  @param adx is a valid admin index
 *  @return a const reference to the admin's realName string
 */
inline string AdminTable::getRealName(int adx) const {
	return avec[adx].realName;
}

/** Get an admin's email address.
 *  @param adx is a valid admin index
 *  @return a const reference to the admin's email string
 */
inline string AdminTable::getEmail(int adx) const {
	return avec[adx].email;
}

/** Set an admin's name.
 *  @param adx is a valid admin index
 *  @param aname is the admin's name string
 */
inline void AdminTable::setAdminName(int adx, const string& aname) {
	avec[adx].aname = aname;
}

/** Set an admin's password.
 *  @param adx is a valid admin index
 *  @return a const reference to the password string
 */
inline void AdminTable::setPassword(int adx, const string& pwd) {
	avec[adx].pwd = pwd;
}

/** Set an admin's real world name.
 *  @param adx is a valid admin index
 *  @return a const reference to the admin's realName string
 */
inline void AdminTable::setRealName(int adx, const string& realName) {
	avec[adx].realName = realName;
}

/** Set an admin's email address.
 *  @param adx is a valid admin index
 *  @param email is the admin's new email address
 */
inline void AdminTable::setEmail(int adx, const string& email) {
	avec[adx].email = email;
}

/** Lock the admin table.
 *  This method is meant primarily for internal use.
 *  Applications should normally just lock single admins using the
 *  methods provided for that purpose. Use extreme caution when using
 *  this method directly.
 */
inline void AdminTable::lockMap() { mapLock.lock(); }

/** Unlock the admin table.
 */
inline void AdminTable::unlockMap() { mapLock.unlock(); }

inline string AdminTable::admin2string(int adx) {
	return avec[adx].toString();
}

} // ends namespace


#endif
