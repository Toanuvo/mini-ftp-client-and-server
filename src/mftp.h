#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

#define ERRCHK(n) if(n < 0) {perror("Error"); exit(-1);}
#define ERRCHK_RET(n) if(n < 0) {perror("Error"); return -1;}
#define ERRCHK_VAR(n, onfail) if(n < 0) {perror("Error"); onfail;}

// reads data from fd until it reaches a \n or \0 then terminates the resulting string with \0
int readcmd(char* buffer, int fd, int bufsize);

// send a packet to the connection in the form cmd+msg+\n
// msg must be null terminated for strlen
int sendPacket(int connection, char* msg, char cmd);

// create a dataconnection on the given host
int getDataConnection(int controlFd, char* host);

