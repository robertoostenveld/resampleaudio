##############################################################################
#
# GNU'ish Makefile
#
# (c) Robert Oostenveld
#
##############################################################################

CC			= gcc
INCPATH	= -I. -I/usr/local/include
LIBPATH = -L/usr/local/lib
LIBS 	  = -lportaudio -lsamplerate
CFLAGS	= $(INCPATH) -Wall -Wno-unused -O2
BINDIR	= .

PACKAGE = resampleaudio

TARGETS = resampleaudio

##############################################################################

all: $(TARGETS)

resampleaudio: resampleaudio.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $*.c

clean:
	rm -f core *.o *~ $(TARGETS)

dist: clean
	cd .. && tar --exclude-vcs -cvzf $(PACKAGE)-${shell date +%Y%m%d}.tgz $(PACKAGE)/*
