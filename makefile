CC = gcc
CFLAGS = -o

all: sender receiver sw_server sw_client sr_server sr_client threads

sender: sender.c common.h
	$(CC) sender.c $(CFLAGS) sender

receiver: receiver.c common.h 
	$(CC) receiver.c $(CFLAGS) receiver

sw_server: sw_server/server.c
	$(CC) server/server.c $(CFLAGS) server/server

sw_client: sw_client.c
	$(CC) client.c $(CFLAGS) client

sr_server: server/server.c
	$(CC) server/server.c $(CFLAGS) server/server

sr_client: client.c
	$(CC) client.c $(CFLAGS) client
	
threads: pthreads.c serve.c client.c
	$(CC) pthreads.c $(CFLAGS) pthreads
	$(CC) server.c $(CFLAGS) server
	$(CC) client.c $(CFLAGS) client

clean:
	rm -f sender receiver pthreads *.o
	rm -f sw_server/server sw_client
	rm -f sw_server/server_log.log sw_client_log.log
	rm -f sr_server/server sr_client
	rm -f sr_server/server_log.log sr_client_log.log
	rm -f *.jpg *.pdf *.txt *.bin
	rm -f *.doc *.mp3 *.mp4 *.png
	rm -f *.ppt *.wav *.xls
	rm -rf output/*
