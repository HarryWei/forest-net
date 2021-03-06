/** @file IfaceTable.h 
 *
 *  @author Jon Turner
 *  @date 2011
 *  This is open source software licensed under the Apache 2.0 license.
 *  See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#include "IfaceTable.h"

namespace forest {

/** Constructor for IfaceTable, allocates space and initializes private data.
 */
IfaceTable::IfaceTable(int maxIf1) : maxIf(maxIf1) {
	ift = new Entry[maxIf+1];
	ifaces = new ListPair(maxIf);
	defaultIf = 0;
}

/** Destructor for IfaceTable, deletes allocated space */
IfaceTable::~IfaceTable() { delete [] ift; delete ifaces; }

/** Allocate and initialize a new interface table entry.
 *  @param iface is an interface number for an unused interface
 *  @param ipa is the IP address to be associated with the interface
 *  @param ipp is the port number to be associated with the interface
 *  @param rs is the rate spec for the interface (up corresponds to in)
 */
bool IfaceTable::addEntry(int iface, ipa_t ipa, ipp_t ipp, RateSpec& rs) {
	if (!ifaces->isOut(iface)) return false;
	if (ifaces->firstIn() == 0) // this is the first iface
		defaultIf = iface;
	ifaces->swap(iface);
	Entry& e = getEntry(iface);
	e.ipa = ipa; e.port = ipp;
	e.rates = e.availRates = rs;
	return true;
}

/** Remove an interface from the table.
 *  @param iface is the number of the interface to be removed
 *  No action is taken if the specified interface number is not valid
 */
void IfaceTable::removeEntry(int iface) {
	if (ifaces->isIn(iface)) ifaces->swap(iface);
	if (iface == defaultIf) defaultIf = 0;
}

/** Read a table entry from an input stream and add it to the table.
 *  The input stream is assumed to be positioned at the start of
 *  an interface table entry. An entry is consists of an interface
 *  number, an IP address followed by a colon and port number,
 *  a maximum bit rate (in Kb/s) and a maximum packet rate (in p/s).
 *  Each field is separated by one or more spaces.
 *  Comments in the input stream are ignored. A comment starts with
 *  a # sign and continues to the end of the line. Non-blank lines that do
 *  not start with a comment are expected to contain a complete entry.
 *  If the input is formatted incorrectly, or the interface number
 *  specified in the input is already in use, the operation will fail.
 *  @param in is an open input stream
 *  @return the number of the new interface or 0, if the operation failed
 */ 
int IfaceTable::readEntry(istream& in) {
	int ifnum; ipa_t ipa; int port; RateSpec rs;

	Util::skipBlank(in);
	if (!Util::readInt(in,ifnum) || !Np4d::readIpAdr(in,ipa) ||
	    !Util::readInt(in,port) || !rs.read(in)) {
		return 0;
	}
	Util::nextLine(in);

	if (!addEntry(ifnum,ipa,port,rs)) return 0;
	return ifnum;
}

/** Read interface table entries from the input.
 *  The first line must contain an
 *  integer, giving the number of entries to be read. The input may
 *  include blank lines and comment lines (any text starting with '#').
 *  Each entry must be on a line by itself (possibly with a trailing comment).
 *  If the operation fails, a message is sent to cerr, identifying the
 *  interface that triggered the failure
 *  @param in is an open input stream
 *  @return true on success, false on failure
 */
bool IfaceTable::read(istream& in) {
	int num;
 	Util::skipBlank(in);
        if (!Util::readInt(in,num)) return false;
        Util::nextLine(in);
	for (int i = 1; i <= num; i++) {
		if (readEntry(in) == 0) {
			cerr << "IfaceTable::read: Error in "
			     << i << "-th entry read from input\n";
			return false;
		}
	}
	return true;
}

/** Create a string representation of an entry.
 *  @param iface is the number of the interface to be written
 *  @return the string
 */
string IfaceTable::entry2string(int iface) const {
	stringstream ss;
	ss << setw(5) << iface << "   " << Np4d::ip2string(ift[iface].ipa)
	   << ":" << ift[iface].port << " "
	   << ift[iface].rates.toString() << endl;
	return ss.str();
}

/** Create a string representation of the interface table.
 *  The output format matches the format expected by the read method.
 *  @return the string
 */
string IfaceTable::toString() const {
	stringstream ss;
	ss << ifaces->getNumIn() << endl;
	ss << "# iface  ipAddress:port      bitRate  pktRate\n";
	for (int i = firstIface(); i != 0; i = nextIface(i)) 
		ss << entry2string(i);
	return ss.str();
}

} // ends namespace

