SHELL = /bin/bash
CC = gcc
CFLAGS += -Wall -g -pedantic -std=c99 -pthread
INCLUDE = -I . -I ./utils/include
LIBS = -lpthread -lm

OBJCLIENT =	obj/parsingclient.o	\
		obj/api.o
		
OBJSERVER = 	obj/configserver.o  	\
		obj/loggingserver.o 	\
		obj/signalhserver.o  	\
		obj/workerserver.o  	\
		obj/filestorage.o 	\
		obj/boundedqueue.o 	\
		obj/icl_hash.o

.PHONY: all clean cleanall test1 test2 test3

all:	server client

server: $(OBJSERVER)
	$(CC) $(CFLAGS) $(INCLUDE) $(OBJSERVER) src/servermain.c $(LIBS) -o bin/$@
	
client: $(OBJCLIENT) lib/libapi.so
	$(CC) $(CFLAGS) $(INCLUDE) $(OBJCLIENT) src/clientmain.c $(LIBS) -o bin/$@ -Wl,-rpath="$(PWD)/lib" -L ./lib -lapi

#libreria dinamica dell'interfaccia api del client
lib/libapi.so: obj/api.o
	$(CC) -shared -o $@ $^
	
obj/%.o: src/%.c utils/include/%.h
	$(CC) $(CFLAGS) $(INCLUDE) -c -fPIC $< -o $@ $(LIBS)

test1: 
	$(MAKE) -s cleanall
	$(MAKE) -s all
	@chmod +x script/test1.sh
	./script/test1.sh
	@chmod +x script/statistiche.sh
	./script/statistiche.sh loggingt1.log
	
test2: 
	$(MAKE) -s cleanall
	$(MAKE) -s all 
	@chmod +x script/test2.sh
	@chmod +x script/statistiche.sh
	./script/test2.sh
	
test3: 
	$(MAKE) -s cleanall
	$(MAKE) -s all
	@chmod +x script/test3.sh
	./script/test3.sh
	sleep 1
	@chmod +x script/statistiche.sh
	./script/statistiche.sh loggingt3.log
	
clean:
	-rm -f obj/*.o lib/*.so bin/* *~ */*~
	
cleanall:
	-rm -f -r */*~ obj/*.o lib/*.so bin/* logging*.log test/*~ espulsi* letti* test/serverout3.txt
