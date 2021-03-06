/** @file IoProcessor.h
 *
 *  @author Jon Turner
 *  @date 2011
 *  This is open source software licensed under the Apache 2.0 license.
 *  See http://www.apache.org/licenses/LICENSE-2.0 for details.
 */

#ifndef IOPROCESSOR_H
#define IOPROCESSOR_H

#include "Forest.h"
#include "IfaceTable.h"
#include "LinkTable.h"
#include "PacketStore.h"
#include "StatsModule.h"

namespace forest {


class IoProcessor {
public:
		IoProcessor(int, ipp_t, IfaceTable*, LinkTable*,
			    PacketStore*, StatsModule*);
		~IoProcessor();

	bool	setup(int);
	bool	ready(int);
	bool	setupBootSock(ipa_t, ipa_t);
	void	closeBootSock();

	int	receive();	
	void	send(int,int);

private:
	ipp_t	bootIp;			///< IP address used to boot router
	ipa_t	nmIp;			///< IP address used by netMgr
	int	bootSock;		///< associated socket
	ipp_t	portNum;		///< port# for iface sockets

	int	maxIface;		///< largest interface number
	int	maxSockNum;		///< largest socket num opened by ioProc
	fd_set	*sockets;		///< file descr set for open sockets
	int	cIf;			///< number of "current interface"
	int	nRdy;			///< number of ready sockets
	int	*sock;			///< sock[i] is socket for iface i

	IfaceTable *ift;		///< pointer to interface table
	LinkTable *lt;			///< pointer to link table
	PacketStore *ps;		///< pointer to packet store
	StatsModule *sm;		///< pointer to statistics module
};

inline bool IoProcessor::ready(int iface) { return sock[iface] > 0; }

} // ends namespace


#endif
