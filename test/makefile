all: rftp_sender rftp_receiver

rftp_sender: rftp_sender.c
	gcc -pthread -o rftp_sender rftp_sender.c

rftp_receiver: rftp_receiver.c
	gcc -pthread -o rftp_receiver rftp_receiver.c

clean:
	rm -f rftp_sender rftp_receiver ./output/*
