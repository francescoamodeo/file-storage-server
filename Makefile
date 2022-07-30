INCDIR		= ./includes
SDIR		= ./src
SSRVDIR		= ./src/server
SCLIDIR		= ./src/client
SHDIR		= ./scripts
ODIR		= ./obj
OSRVDIR		= ./obj/server
OCLIDIR		= ./obj/client
LIBDIR		= ./lib
BINDIR		= ./bin
LOGSDIR		= ./logs

CC			= gcc
CFLAGS	    += -std=c99 -g -Wall -pedantic -D_POSIX_C_SOURCE=200809L
ARFLAGS     = rvs
LDFLAGS 	= -L ./lib
LDLIBS		= -lfilestorage
LIBS        = -lpthread
INCSERVER	= -I $(INCDIR) -I $(INCDIR)/utils -I $(INCDIR)/server
INCCLIENT	= -I $(INCDIR) -I $(INCDIR)/utils -I $(INCDIR)/client

OBJSERVER	= $(addprefix $(OSRVDIR)/, manager.o storage.o worker.o icl_hash.o list.o threadpool.o)
OBJCLIENT	= $(addprefix $(OCLIDIR)/, client.o queue.o)
OBJAPI		= $(addprefix $(ODIR)/, filestorage.o)
LIBAPI 		= $(addprefix $(LIBDIR)/, libfilestorage.a)

TARGETS		= client server

.PHONY: all clean cleanall test1 test2 test3

all : $(TARGETS)

client : $(OBJCLIENT) $(LIBAPI) | $(BINDIR)
	$(CC) $(CFLAGS) $(OBJCLIENT) -o $(BINDIR)/$@ $(LDFLAGS) $(LDLIBS)

$(OBJCLIENT) : $(OCLIDIR)/%.o : $(SCLIDIR)/%.c | $(OCLIDIR)
	$(CC) $(CFLAGS) $(INCCLIENT) $< -c -o $@

$(LIBAPI) : $(OBJAPI) | $(LIBDIR)
	$(AR) $(ARFLAGS) $@ $<

$(OBJAPI): $(ODIR)/%.o : $(SDIR)/%.c  | $(ODIR)
	$(CC) $(CFLAGS) $(INCCLIENT) $< -c -o $@

server : $(OBJSERVER) | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $(BINDIR)/$@ $(LIBS)

$(OBJSERVER) : $(OSRVDIR)/%.o : $(SSRVDIR)/%.c | $(OSRVDIR)
	$(CC) $(CFLAGS) $(INCSERVER) $^ -c -o $@

$(BINDIR) :
	mkdir -p $(BINDIR)

$(ODIR) :
	mkdir -p $(ODIR)

$(OSRVDIR) :
	mkdir -p $(OSRVDIR)

$(OCLIDIR) :
	mkdir -p $(OCLIDIR)

$(LIBDIR) :
	mkdir -p $(LIBDIR)

clean	 :
	-rm -rf $(BINDIR)

cleanall : clean
	-rm -rf $(ODIR) $(LIBDIR) $(LOGSDIR)

test1	:
	chmod +x $(SHDIR)/test1.sh && $(SHDIR)/test1.sh
	chmod +x $(SHDIR)/statistiche.sh && $(SHDIR)/statistiche.sh $(LOGSDIR)/log.txt

test2	:
	chmod +x $(SHDIR)/test2.sh && $(SHDIR)/test2.sh
	chmod +x $(SHDIR)/statistiche.sh && $(SHDIR)/statistiche.sh $(LOGSDIR)/log.txt

test3	:
	chmod +x $(SHDIR)/test3.sh && chmod +x $(SHDIR)/start_clients.sh && $(SHDIR)/test3.sh
	chmod +x $(SHDIR)/statistiche.sh && $(SHDIR)/statistiche.sh $(LOGSDIR)/log.txt