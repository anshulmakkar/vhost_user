CC=gcc
CFLAGS=-Wall -g
LDFLAGS= -lrdmacm -libverbs -lpthread

all: vhost_user 

vhost_user: vhost_user.o vhost_user_transport.o 
	$(CC) $(CFLAGS) -o vhost_user vhost_user.o vhost_user_transport.o $(LDFLAGS)	

clean:
	rm -f vhost_user vhost_user.o vhost_user_transpiort.o
