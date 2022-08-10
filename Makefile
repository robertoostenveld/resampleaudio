##############################################################################
#
# GNU'ish Makefile
#
# (c) Robert Oostenveld
#
##############################################################################

CC			= gcc
CXX     = g++
INCPATH	= -I . -I /usr/local/include
LIBPATH = -L /usr/local/lib
LIBS 	  = -lportaudio -lsamplerate -llsl
CFLAGS	= $(INCPATH) -Wall -Wno-unused -O2
CXXFLAGS= $(INCPATH) -Wall -Wno-unused -O2 -std=c++14
BINDIR	= .

PACKAGE = resampleaudio

TARGETS = resampleaudio resamplelsl

##############################################################################

all: $(TARGETS)

GetAllStreams: GetAllStreams.o
	$(CXX) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)

resampleaudio: resampleaudio.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)

resamplelsl: resamplelsl.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $*.cpp

%.o: %.c
	$(CC) $(CFLAGS) -c $*.c

clean:
	rm -f core *.o *~ $(TARGETS)

dist: clean
	cd .. && tar --exclude-vcs -cvzf $(PACKAGE)-${shell date +%Y%m%d}.tgz $(PACKAGE)/*
