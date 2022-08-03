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
LIBS 	  = -lportaudio -lsndfile -lsamplerate -lncurses
CFLAGS	= $(INCPATH) -Wall -Wno-unused -O2
BINDIR	= .

PACKAGE = resampleaudio

TARGETS = resampleaudio pa_fuzz pa_devs pa_read_write_wire paex_saw curhello curwin1 curhell2 timewarp-file

##############################################################################

all: $(TARGETS)

resampleaudio: resampleaudio.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)

pa_fuzz: pa_devs.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)

pa_devs: pa_devs.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)

pa_read_write_wire: pa_read_write_wire.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)
	
paex_saw: paex_saw.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)
	
curhello: curhello.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)
		
curwin1: curwin1.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)
	
curhell2: curhell2.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)

timewarp-file: timewarp-file.o
	$(CC) $(LIBPATH) -o $(BINDIR)/$@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $*.c

clean:
	rm -f core *.o *~ $(EXECUTABLES)

dist: clean
	cd .. && tar -cvzf $(PACKAGE)-${shell date +%Y%m%d}.tgz $(PACKAGE)/*
