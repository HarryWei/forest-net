SHELL := /bin/bash
ARCH := -m$(shell getconf LONG_BIT)
FROOT = ../
AROOT = ${FROOT}/../../grafalgo/cpp
INCLUDES := -I ${FROOT}/include -I ${AROOT}/include
FLIB := ${FROOT}/lib/lib-forest.a
LIBS := ${FLIB} ${AROOT}/lib/lib-ds.a ${AROOT}/lib/lib-util.a
BIN := ~/bin
WARN := -Wall
CXXFLAGS := ${WARN} ${ARCH} ${INCLUDES} -pthread -O2 -std=c++0x 
JAVAC := javac
IDIR := ${FROOT}/include

HFILES = ${IDIR}/NetInfo.h ${IDIR}/ComtInfo.h ${IDIR}/CpHandler.h
OFILES = NetInfo.o ComtInfo.o CpHandler.o Substrate.o ClientTable.o AdminTable.o
XFILES = NetMgr ClientMgr ComtCtl BuildRtables PhotoServer #threadExample Console 

${OFILES} : ${HFILES}

.cpp.o:
	${CXX} ${CXXFLAGS} -c $<

all : ${FLIB} ${XFILES}
	cp ${XFILES} ${BIN}

${FLIB} : ${OFILES}
	ar -ru ${FLIB} ${OFILES}

NetMgr : NetMgr.o ${LIBS}
	${CXX} ${CXXFLAGS} $< ${LIBS} -lpthread -o $@

foo : foo.o ${LIBS}
	${CXX} ${CXXFLAGS} $< ${LIBS} -lpthread -o $@

threadExample : threadExample.o ${LIBS}
	${CXX} ${CXXFLAGS} $< ${LIBS} -v -o $@

Console : Console.o ${LIBS}
	${CXX} ${CXXFLAGS} $< ${LIBS} -o $@

ClientMgr : ClientMgr.o ${LIBS}
	${CXX} ${CXXFLAGS} $< ${LIBS} -lpthread -o $@

ComtCtl : ComtCtl.o ${LIBS}
	${CXX} ${CXXFLAGS} $< ${LIBS} -lpthread -o $@

PhotoServer : PhotoServer.o ${LIBS}
	${CXX} ${CXXFLAGS} $< ${LIBS} -lpthread -o $@	
	
BuildRtables : BuildRtables.o ${LIBS}
	${CXX} ${CXXFLAGS} $< ${LIBS} -o $@

clean :
	rm -f *.o ${XFILES}
