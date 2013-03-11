EXECUTABLE_NAME=svnwcrev

CC=$(CXX)
CXX=g++

CPPFLAGS=-I$(SUBVERSION_INCLUDE) -I$(APR_INCLUDE)
CXXFLAGS=-g3

LDLIBS=-lpthread -L$(LIBRARIES) -lsvn_client-1 -lsvn_wc-1 -lsvn_subr-1 -lapr-1

objects=src/status.o src/SVNWcRev.o

include config.mk
include default.mk
