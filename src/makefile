myclient: mftp.c
	gcc mftp.c -Wall -o myclient

myserve: mftpserve.c
	gcc mftpserve.c -Wall -o myserve

runc: myclient
	./myclient 50000 localhost

all: clean myserve myclient

verify_compile: all clean

clean:
	rm -f myclient myserve

