""" Avatar for a simple virtual world

usage:
      Avatar myIpAdr cliMgrIpAdr wallsFile comtree userName passWord finTime debug
example:
      python Avatar.py 172.18.68.76 128.252.153.22 walls40 1001 user pass 300 debug

Command line arguments include the ip address of the
avatar's machine, the client manager's IP address,
the walls file, the comtree to be used in the packet,
a user name and password and the number of
seconds to run before terminating.
"""

"""
Implementation notes

Multiple threads
- substrate handles all socket IO and multiplexes data from next level
- core thread generates/consumes reports and sends/receives subscriptions
  - just do a 20 ms loop, rather than bother with time
- control thread sends/receives control packets; one at a time;
  when core wants to join or leave a comtree, it sends a request to
  the control thread and then waits for response; control thread handles
  retransmissions;

"""

import sys
from socket import *
from Core import *
from Substrate import *
from Packet import *
from Util import *
#from vworld2 import *
import vworld2

def login() :
	cmSock = socket(AF_INET, SOCK_STREAM);
	cmSock.connect((cliMgrIpAdr,30140))

	s = uname + " " + pword + " " + str(sub.myAdr[1]) + " 0 noproxy"
	blen = 4+len(s)
	buf = struct.pack("!I" + str(blen) + "s",blen,s);
	cmSock.sendall(buf)
	
	buf = cmSock.recv(4)
	while len(buf) < 4 : buf += cmSock.recv(4-len(buf))
	tuple = struct.unpack("!I",buf)
	if tuple[0] == -1 :
		sys.stderr.write("Avatar.login: negative reply from " \
				 "client manager");
		return False

	global rtrFadr
	rtrFadr = tuple[0]

	buf = cmSock.recv(12)
	while len(buf) < 12 : buf += cmSock.recv(12-len(buf))
	tuple = struct.unpack("!III",buf)

	global myFadr, rtrIp, comtCtlFadr
	myFadr = tuple[0]
	rtrIp = tuple[1]
	comtCtlFadr = tuple[2]

	print	"avatar address:", fadr2string(myFadr), \
       		" router address:", fadr2string(rtrFadr), \
       		" comtree controller address:", fadr2string(comtCtlFadr);
	return True


if len(sys.argv) < 8 :
	sys.stderr.write("usage: Avatar myIpAdr cliMgrIpAdr walls " + \
			 "comtree uname pword finTime")
        sys.exit(1)

myIpAdr = sys.argv[1]; cliMgrIpAdr = sys.argv[2]
wallsFile = sys.argv[3]; myComtree = int(sys.argv[4])
uname = sys.argv[5]; pword = sys.argv[6]
finTime = int(sys.argv[7])

debug = 0
if len(sys.argv) > 8 :
	if sys.argv[8] == "debug" : debug = 1
	elif sys.argv[8] == "debugg" : debug = 2
	elif sys.argv[8] == "debuggg" : debug = 3

world = WallsWorld() 
if not world.init(wallsFile) :
	sys.stderr.write("cannot initialize world from walls file\n");
	sys.exit(1)

bitRate = 200; pktRate = 300
print "debug=", debug
sub = Substrate(myIpAdr,bitRate,pktRate,debug)

if not login() :
	sys.stderr.write("cannot login");
	sys.exit(1)

# init panda3D
vw2 = vworld2.World()

sub.init(rtrIp)
core = Core(sub,myIpAdr,myFadr,rtrFadr,comtCtlFadr,myComtree,finTime,world,vw2)

# and start them running
try : sub.start(); core.start();
except : sys.exit(1)

while True :
	sys.stdout.write(": ")
	line = sys.stdin.readline()
	if line[0] == "q" : break;
	elif line[0] == "a" :
		if core.speed == STOPPED : core.direction -= 90
		else : core.direction -= 10
	elif line[0] == "d" :
		if core.speed == STOPPED : core.direction += 90
		else : core.direction += 10
	elif line[0] == "s" :
		if core.speed == SLOW : core.speed = STOPPED
		elif core.speed == MEDIUM : core.speed = SLOW
		elif core.speed == FAST : core.speed = MEDIUM
	elif line[0] == "w" : 
		if core.speed == STOPPED : core.speed = SLOW
		elif core.speed == SLOW : core.speed = MEDIUM
		elif core.speed == MEDIUM : core.speed = FAST
	if core.direction < 0 : core.direction += 360
core.stop(); core.join()
sub.stop(); sub.join
sys.exit(0)