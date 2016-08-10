# Makefile for COMP 150IDS End-to-End assignment
#
#    Copyright 2012 - Noah Mendelsohn
#
# The targets below are for samples that may be helpful, but:
#
#   YOUR MAIN JOB IN THIS ASSIGNMENT IS TO CREATE TWO
#   NEW PROGRAMS: fileclient and fileserver
#
#   YOU WILL MODIFY THIS MAKEFILE TO BUILD THEM TOO
#
# Useful sample program targets:
#
#
#    nastyfiletest - demonstrates the c150nastyfile class that you
#                    MUST use to read and write your files
#
#    sha1test -   sample code for computing SHA1 hash
#
#    makedatafile -  sometimes when testing file copy programs
#                    it's useful to have files in which it's
#                    relatively easy to spot changes.
#                    This program generates sample data files.
#
#  Maintenance targets:
#
#    Make sure these clean up and build your code too
#
#    clean       - clean out all compiled object and executable files
#    all         - (default target) make sure everything's compiled
#

# Do all C++ compies with g++
CPP = g++
CPPFLAGS = -g -Wall -Werror -I$(C150LIB)

# Where the COMP 150 shared utilities live, including c150ids.a and userports.csv
# Note that environment variable COMP150IDS must be set for this to work!

C150LIB = $(COMP150IDS)/files/c150Utils/
C150AR = $(C150LIB)c150ids.a

LDFLAGS =
INCLUDES = $(C150LIB)c150dgmsocket.h $(C150LIB)c150nastydgmsocket.h $(C150LIB)c150network.h $(C150LIB)c150exceptions.h $(C150LIB)c150debug.h $(C150LIB)c150utility.h

all: filecopy.o nastyfiletest makedatafile sha1test client server

#
# Build Client
#
client: client.o filecopy.o $(C150AR) $(INCLUDES)
	$(CPP) -o client  $(CPPFLAGS) filecopy.o client.o $(C150AR) -lssl
#
# Build Server
#
server: server.o filecopy.o $(C150AR) $(INCLUDES)
	$(CPP) -o server  $(CPPFLAGS) filecopy.o server.o $(C150AR) -lssl

#
# Build the nastyfiletest sample
#
nastyfiletest: nastyfiletest.o  $(C150AR) $(INCLUDES)
	$(CPP) -o nastyfiletest  $(CPPFLAGS) nastyfiletest.o $(C150AR)

#
# Build the sha1test
#
sha1test: sha1test.o
	$(CPP) -o sha1test sha1test.o -lssl

#
# Build the makedatafile
#
makedatafile: makedatafile.o
	$(CPP) -o makedatafile makedatafile.o


#
# To get any .o, compile the corresponding .cpp
#
%.o:%.cpp  $(INCLUDES)
	$(CPP) -c  $(CPPFLAGS) $<


#
# Delete all compiled code in preparation
# for forcing complete rebuild#

clean:
	 rm -f nastyfiletest sha1test makedatafile client server *.o GRADELOG* core.* vgcore.*


