#include <sys/types.h>
#include <signal.h>
#include "mftp.h"

// By Kevin Zavadlov, cs 360, final project

int debug = 0;

#define CONNECTION_QUEUE_SIZE 4
#define MY_PORT 50000

int readcmd(char* buffer, int fd, int bufsize){
    int amt;
    int total = 0;
    int chunksize = 4;
    
    //read from client
    while((amt = read(fd, &buffer[total], chunksize)) > 0){
        total += amt;
        if(buffer[total-1] == '\n' || buffer[total-1] == '\0'){
            buffer[total-1] = '\0';
            break;
        }

        if(total + chunksize > bufsize){
            printf("max read buffer reached\n");
            return -1;
        }
    }

    // error checking and val
    ERRCHK_RET(amt)
    int ret = (total == 0) ? -1 : 0;
    return ret;
}

int sendPacket(int connection, char* msg, char cmd){
    printf("sending packet '%s' with cmd '%c', to connection %d\n", msg, cmd, connection);
    int msglen = strlen(msg);
    char* packet = malloc(sizeof(char)*(msglen + 2));
    if(packet == NULL) return -1;
    packet[0] = cmd;
    strcpy(packet+1, msg);
    packet[msglen+1] = '\n';
    int ret = write(connection, packet, msglen+2);
    free(packet);
    return ret;
}

// prints error msgs and 
void exitchild(int controlFd){
    if(debug){
        printf("Child: Quitting\n");
        printf("Child: sending positive acknowledgement\n");
    }
    write(controlFd, (char*){"A\n"}, 2);
    close(controlFd);
    printf("Child: exiting normally\n");
    exit(0);
}

// create a connection on the given port and any address
int createConnection(int port, char* host){
    // create socket
    int connectfd = socket(AF_INET, SOCK_STREAM, 0);
    ERRCHK_RET(connectfd)
    ERRCHK_RET(setsockopt(connectfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)))

    // bind socket to anyaddress and the specified port
    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(port);
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);	

    ERRCHK_RET(bind(connectfd,  (struct sockaddr *)&servAddr, sizeof(struct sockaddr_in)))
    return connectfd;
}

int getDataConnection(int controlFd, char* host){
    // create connection on arbitrary port and listen on it
    int listendatafd = createConnection(0, NULL);
    if(listendatafd < 0){
        sendPacket(controlFd, strerror(errno), 'E'); 
        return -1;
    }
    if (listen(listendatafd, 1) < 0){
        sendPacket(controlFd, strerror(errno), 'E'); 
        return -1;
    }
    
    // get port from sockaddr
    struct sockaddr_in getsockport;
    memset(&getsockport, 0, sizeof(getsockport)); 
    unsigned int socklen = sizeof(getsockport);
    if(debug) printf("Child: getting socket\n");
    int nameErr =  getsockname(listendatafd, (struct sockaddr *)&getsockport, &socklen);
    if(nameErr < 0){
        sendPacket(controlFd, strerror(errno), 'E'); 
        return -1;
    }
    int dataport = ntohs(getsockport.sin_port);
    if(debug) printf("Child: listenting on port %d\n", dataport);
    
    // send port to client
    char portstr[8];
    sprintf(portstr, "%d", dataport);
    sendPacket(controlFd, portstr, 'A');

    // accept connection from client
    struct sockaddr_in dataclientAddr;
    unsigned int dlength = sizeof(struct sockaddr_in);
    int datafd = accept(listendatafd, (struct sockaddr *) &dataclientAddr, &dlength);

    ERRCHK_RET(datafd);
    printf("Child: accepted dataconnection on fd %d\n", datafd);
    return datafd;
}

int rcd(int controlFd, char* dir){
    struct stat area, *s = &area;

    // populate stat struct
    if(stat(dir, s) < 0) {
        sendPacket(controlFd, strerror(errno), 'E'); 
        return -1;
    }
    
    mode_t m = s->st_mode;
    // check if path is a directory
    if(!(S_ISDIR (m))){
        sendPacket(controlFd, "not a directory", 'E'); 
        return -1;
    }

    // check we have read perms
    if(access(dir, R_OK|X_OK) < 0) {
        sendPacket(controlFd, strerror(errno), 'E'); 
        return -1;
    }

    // cd into given directory and process errors
    if(chdir(dir) < 0){
        sendPacket(controlFd, strerror(errno), 'E');
        return -1;
    } else {
        if(debug) printf("Child: sending positive acknowledgement\n");
        ERRCHK_RET(write(controlFd, (char*){"A\n"}, 2)) 
        return 0;
    }
}

// send a file to the client through the given dataconnection
int getfile(char* filename, int controlFd, int dataConnection){
    struct stat area, *s = &area;
    int fd;
    if(debug) printf("Child: file to get: %s\n", filename);

    // check we have read perms
    if(access(filename, R_OK) < 0) {
        sendPacket(controlFd, strerror(errno), 'E'); 
        close(dataConnection);
        return -1;
    }

    // populate stat struct
    if(stat(filename, s) < 0) {
        sendPacket(controlFd, strerror(errno), 'E'); 
        close(dataConnection);
        return -1;
    }
    
    mode_t m = s->st_mode;
    // check if path is a file
    if(!(S_ISREG (m))){
        sendPacket(controlFd, "not a regular file", 'E'); 
        close(dataConnection); 
        return -1;
    }

    // open file for reading
    if((fd = open(filename, O_RDONLY)) < 0) {
        sendPacket(controlFd, strerror(errno), 'E'); 
        close(dataConnection); 
        return -1;
    }

    // write file to dataconnection
    ERRCHK_RET(write(controlFd, (char*){"A\n"}, 2))
    if(debug) printf("Child: sending positive acknowledgement\n");
    char writebuf[256];
    int amtread = 0;
    printf("Child: writing file to client\n");
    while ((amtread = read(fd, writebuf, 256)) > 0){
        if(write(dataConnection, writebuf, amtread) < 0){
            perror("");
            close(fd);
            close(dataConnection);    
            return -1;
        }
    }
    
    close(dataConnection);
    close(fd);
    if(amtread < 0) {
        return -1;
    }
    return 0;
}

// recieve a file from the client and create it here
int putfile(char* filename, int controlFd, int dataConnection){
    char buf[PATH_MAX];

    printf("filename: %s\n", filename);
    // check permissions
    if(access(getcwd(buf, PATH_MAX), W_OK | R_OK) < 0){
        sendPacket(controlFd, strerror(errno), 'E'); 
        close(dataConnection);
        return -1;
    }

    // open file exclusivly by creating it
    int fd = open(filename, O_CREAT | O_EXCL | O_RDWR, 0700);
    if(fd < 0){
        sendPacket(controlFd, strerror(errno), 'E'); 
        close(dataConnection);
        return -1;
    }

    // read file from client and write it to the created file
    ERRCHK_RET(write(controlFd, (char*){"A\n"}, 2))
    char writebuf[256];
    int amtread = 0;
    printf("recieving file from client\n");
    while ((amtread = read(dataConnection, writebuf, 256)) > 0){
        if(write(fd, writebuf, amtread) < 0){
            perror("");
            close(fd);
            close(dataConnection);
            unlink(filename); 
            return -1;
        }
    }
    close(fd);
    close(dataConnection);
    if(amtread < 0) {
        return -1;
    }
    return 0;
}

// parent exit handler to kill any children
void parentExit(){
    if(debug) printf("killing children\n");
    kill(0, SIGINT);
    exit(-1);
}


// handle all commands from the specified connection until it closes
void runChild(int controlFd, struct sockaddr_in clientAddr){
    // setup
    pid_t mypid = getpid();
    int datafd = 0;

    // get hostname the connection is on
    char hostName[NI_MAXHOST];
    char servIP[NI_MAXSERV];
    int hostEntry = getnameinfo((struct sockaddr*)&clientAddr, sizeof(clientAddr), 
                                hostName, sizeof(hostName),
                                servIP, sizeof(servIP), 
                                NI_NUMERICSERV);
    if(hostEntry != 0){
        fprintf(stderr, "getnameinfo ERR: %s\n", gai_strerror(hostEntry));
    }

    // set sigint handler to default
    signal(SIGINT, SIG_DFL);

    if(debug) printf("Child %d: started\n", mypid);
    printf("Child %d: Connection accepted from host %s\n",mypid, hostName);
    if(debug) printf("Child %d: Connection fd is %d\n",mypid, controlFd);

    // process commands until exited
    while(1){
        char buffer[PATH_MAX+10];
        if(readcmd(buffer, controlFd, 256) < 0){
            printf("Child %d: unexpected EOF, exiting\n", mypid);
            close(controlFd);
            exit(-1);
        }

        // process commands
        switch(buffer[0]){ 
            case 'Q':
                if(datafd){
                    printf("ERR data connection still open\n");
                    close(datafd);
                } 

                exitchild(controlFd);
                break;  
            case 'C':
                if(rcd(controlFd, buffer+1) < 0){
                    printf("Child %d: ERR Failed to change current dir to %s\n", mypid, buffer+1);
                } else {
                    if(debug) printf("Child %d: changed current dir to %s\n", mypid, buffer+1);
                }
                
                break;
            case 'D':
            {
                if(datafd){
                    ERRCHK_VAR(sendPacket(controlFd, "data connection already established", 'E'), continue)
                } else {
                    datafd = getDataConnection(controlFd, NULL);
                    if(datafd < 0){
                        datafd = 0;
                        printf("Child: data connection failed to start");
                    }

                }

                break;
            }
            
            case 'L':
            {
                if(!datafd){
                    sendPacket(controlFd, "Data connection not established",'E');
                } else {

                    int childid = fork();

                    if(childid<0){
                        sendPacket(controlFd, strerror(errno), 'E');
                        close(datafd);
                        datafd = 0;
                        continue;
                    }
                    if(childid){
                        ERRCHK_VAR(write(controlFd, (char*){"A\n"}, 2), continue)   
                        wait(0);
                        printf("finished ls\n");
                        close(datafd);
                        datafd = 0;

                    } else {
                        if(debug) printf("child: starting local ls\n");
                        dup2(datafd, fileno(stdout));
                        execlp("ls", "ls", "-la", (char* )NULL);

                        perror("exec ls Err:");
                        exit(-1);
                    }
                }
                break;
            }
            case 'G':
            {
                if(!datafd){
                    sendPacket(controlFd, "Data connection not established",'E');
                } else {
                    if(debug) printf("Child %d: sending acknowledgement\n",mypid);
                    
                    if(getfile(buffer+1, controlFd, datafd) == 0){
                        printf("Child %d: Succesfully wrote file to client\n",mypid);
    
                    } else {
                        printf("Child %d: file write to client failed\n",mypid);
                        
                    }
                    datafd = 0;
                }

                break;
            }
            case 'P':
            {
                if(!datafd){
                    sendPacket(controlFd, "Data connection not established",'E');
                } else {
                    // parse filename from given pathname
                    char* tmp = buffer + 1;
                    char* filename = strtok(tmp, "/");
                    char* last;
                    while((last = strtok(NULL, "/")) != NULL) filename = last;
                    if(debug) printf("Child %d: sending acknowledgement\n",mypid);
                    if(putfile(filename, controlFd, datafd) == 0){
                        printf("Child %d: Succesfully wrote file to client\n",mypid);
                        datafd = 0;
                    } else {
                        printf("Child %d: file read from client failed\n",mypid);
                        datafd = 0;
                    }
                }
                break;
            }
            default: 
                printf("Child: unknown cmd: %c in cmdstr: %s\n", buffer[0], buffer);
        }
    }
    close(controlFd);
    printf("FELL OUT OF HANDLE CMD LOOP THIS SHOULD BE UNREACHABLE\n");
    exit(-1);
}


// the functionality of the server parent
void runParent(int listenfd){
    while(1){
        
        unsigned int length = sizeof(struct sockaddr_in);
        struct sockaddr_in clientAddr;

        // wait for a client to connect and save client address
        int connectfd = accept(listenfd, (struct sockaddr *) &clientAddr, &length);
        if(connectfd < 0){
            perror("Error");
            printf("Error in main server process...exiting\n");
            parentExit();
        }

		int childid = fork();
        if(childid){
            // proccess exited children
            if(debug) printf("parent: created child process with id %d\n", childid);
            int exitstat, exitedid;
            while ((exitedid = waitpid(-1, &exitstat, WNOHANG)) > 0)
            {
                printf("parent: detected termination of child process %d  with exit code %d\n", exitedid, exitstat);
            }
            close(connectfd);
        } else{
            runChild(connectfd, clientAddr);
        }          
	}
}


int main(int argv, char* argc[]){

    // process arguments
	if(argv == 2){
        if(strcmp(argc[1], "-d")==0){
            debug = 1;
        } else {
            printf("unknown argument: %s\n", argc[1]);
            return -1;
        }
	}

    // create a connection to listen on
	int listenfd = createConnection(MY_PORT, NULL);
    if(listenfd < 0){
        printf("connection creation failed\n");
        exit(-1);
    }
    ERRCHK_RET(listen(listenfd, CONNECTION_QUEUE_SIZE))

    // attach sigint handler
    signal(SIGINT, parentExit);

    // run parent indefinetly
    runParent(listenfd);
}



