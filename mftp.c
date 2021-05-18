#include "mftp.h"

// By Kevin Zavadlov, cs 360, final project

int putfile(char* filename, int controlFd, char* host);

int debug = 0;

int readcmd(char* buffer, int fd, int bufsize){
    int amt;
    int total = 0;
    int chunksize = 4;

    // read from server
    while((amt = read(fd, &buffer[total], chunksize)) > 0){
        total += amt;
        if(buffer[total-1] == '\n' || buffer[total-1] == '\0'){
            buffer[total-1] = '\0';
            break;
        }
        if(total + chunksize > bufsize){
            printf("max read buffer reached\n");
            printf("PARTIAL MSG: %s\n", buffer);
            return -1;
        }
    }

    // error checking and val
    ERRCHK_RET(amt)
    int ret = (total == 0) ? -1 : 0;
    return ret;
}

// reads command with a variable buffer size
// returned buffer must be freed by the caller
char* readcmd_v(int fd){
    int bufsize = 8;
    int chunksize = 8;
    char* buffer = malloc(sizeof(char)*bufsize);

    int amt;
    int total = 0;

    // read msg from server
    while((amt = read(fd, &buffer[total], chunksize)) > 0){
        total += amt;
        if(buffer[total-1] == '\n' || buffer[total-1] == '\0'){
            buffer[total-1] = '\0';
            break;
        }
        if(total + chunksize > bufsize){
            bufsize *= 2;
            buffer = realloc(buffer, bufsize);
        }
    }
    
    if(total == 0 || amt < 0){
        free(buffer);

        perror("possible read Err: ");
        if(amt < 0) printf("read ERR on fd %d", fd);
        if(total == 0) printf("read nothing from server. exiting\n");
        exit(-1);
    }
    return buffer;
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

// creates a connection on the given port and host
int createConnection(char* port,char* host){

    // setup type of connection
    int socketfd;
    struct addrinfo hints, *actualdata;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;

    // create connection
    int err = getaddrinfo(host, port, &hints, &actualdata);
    if(err != 0){
        fprintf(stderr, "Error: %s\n", gai_strerror(err));
        return -1;
    }
    socketfd = socket(actualdata->ai_family, actualdata->ai_socktype, 0);
    freeaddrinfo(actualdata);
    ERRCHK_RET(socketfd)
    ERRCHK_RET(connect(socketfd, actualdata->ai_addr, actualdata->ai_addrlen))

    if(debug) printf("connected on host %s with port %s\n", host, port);
    return socketfd;
}

int getDataConnection(int controlFd, char* host){
    for(int i = 0; i<2; i++){
        // ask for dataconnection
        ERRCHK_RET(write(controlFd, (char*){"D\n"}, 2))

        // get resonse of "A"+"portnum"
        if(debug) printf("awaiting server response\n");
        char* response = readcmd_v(controlFd);
        if(response[0] == 'E'){
            printf("recieved server Err response: %s\n", response);
            free(response);
            continue;
        } else if(response[0] == 'A'){
            if(debug) printf("recieved server response: %s\n", response);
        } else {
            printf("Unknown respose recieved from server\n");
            return -1;
        }
        
        // attempt to create connection on the given port
        int connectfd = createConnection(response+1, host);
        free(response);
        return connectfd;
    }
    return -1;

}

// prints error response and returns -1 or 0
int processBasicResponse(int controlFd){
    if(debug) printf("awaiting server response\n");

    // read response from server
    char* response = readcmd_v(controlFd);

    // process response
    int ret = 0;
    switch (response[0]){
    case 'A':
        if(debug) printf("recieved server response: %s\n", response);
        break;
    case 'E':
        printf("recieved server ERR response: %s\n", response);
        ret = -1;
        break;
    default:
        printf("recieved unknown response from server: %s\n", response);
        ret = -1;
        break;
    }

    free(response);
    return ret;
}



// attempt to retrieve the remote file and create it locally
// host is nessesary to pass to connection
int getfile(char* filename, int controlFd, char* host){
    // create file if it doesnt exist and error check
    char buf[PATH_MAX];
    ERRCHK_RET(access(getcwd(buf, PATH_MAX), R_OK |W_OK))
    int fd = open(filename, O_CREAT | O_EXCL | O_RDWR, 0700);
    ERRCHK_RET(fd)

    // get data connection
    int dataConnection = getDataConnection(controlFd, host);
    if(dataConnection < 0){
        printf("data connection failed\n");
        close(dataConnection);
        close(fd);
        return -1;
    } 

    ERRCHK_RET(sendPacket(controlFd, filename, 'G'))
    // do server handshake
    if(processBasicResponse(controlFd) < 0){
        close(fd);
        unlink(filename);
        close(dataConnection);
        return -1;
    }

    // r/w data from server
    char writebuf[256];
    int amtread = 0;
    if(debug) printf("getting file from server\n");
    while ((amtread = read(dataConnection, writebuf, 256)) > 0){
        if(write(fd, writebuf, amtread) < 0){
            close(fd);
            perror("");
            close(dataConnection);
            ERRCHK(unlink(filename))
            exit(-1);
        }
    }
    close(fd);
    close(dataConnection);
    ERRCHK(amtread)
    return 0;
}

// attmept to put a local file on the server
int putfile(char* filename, int controlFd, char* host){
    struct stat area, *s = &area;
    ERRCHK_RET(access(filename, R_OK))
    ERRCHK_RET(stat(filename, s))
    mode_t m = s->st_mode;
    
    if(!(S_ISREG (m))){
        printf("Error: not a regular file\n");
        return -1;
    }

    int fd = open(filename, O_RDONLY);
    ERRCHK_RET(fd)

    // do server handshake
    int dataConnection = getDataConnection(controlFd, host);
    if(dataConnection < 0){
        printf("data connection failed\n");
        close(fd);
        return -1;
    } 

    // ask server to put file
    ERRCHK_RET(sendPacket(controlFd, filename, 'P'))
    if(processBasicResponse(controlFd) < 0){
        close(dataConnection);
        close(fd);
        return -1;
    }

    // write file to server
    char writebuf[256];
    int amtread = 0;
    printf("writing file to server\n");
    while ((amtread = read(fd, writebuf, 256)) > 0){
        if(write(dataConnection, writebuf, amtread) < 0){
            close(fd);
            close(dataConnection);    
            perror("");
            return -1;
        }
    }
    
    close(dataConnection);
    close(fd);
    ERRCHK_RET(amtread)
    return 0;
}

// have the server transmit a file to me and display its contents using more
int showfile(int controlFd, char* filename, char* host){
    // establish dataconnection
    int dataconnection = getDataConnection(controlFd, host);
    if(dataconnection < 0){
        printf("data connection failed\n");
        return -1;
    } 

    // get the file and do handshake
    ERRCHK_RET(sendPacket(controlFd, filename, 'G'))
    if(processBasicResponse(controlFd) < 0){
        close(dataconnection);
        return -1;
    }

    // fork wait and recieve data 
    int childid = fork();
    ERRCHK(childid)
    if(childid){ 
        if(debug) printf("forking more and waiting\n");
        wait(0);

        if(debug) printf("data display and more cmd finished\n");
        close(dataconnection);
        return 0;
    
    } else {
        dup2(dataconnection, 0);        
        execlp("more", "more", "-20",  (char* )NULL);
        perror("exec err");
        exit(-1);
    }
}

// do a local ls and display the results to the user through more
int dols(){
    
    // setup fork
    int childid = fork();

    ERRCHK(childid)
    if(childid){
        if(debug) printf("client parent waiting on child process for ls and more\n");
        wait(&childid);
        if(debug) printf("finished ls and more\n");
        return 0;

    } else {
        // setup pipe
        int pipefd[2];
        ERRCHK(pipe(pipefd))

        int pid = fork();
        ERRCHK(pid)
        if(pid){ 
            // pipe read side into more
            close(pipefd[1]); 
            ERRCHK(dup2(pipefd[0], fileno(stdin)))

            wait(&pid);
        
            if(debug) printf("child process starting more\n");
            execlp("more", "more", "-20", (char* )NULL);
            perror("exec more Err:");
            exit(-1);
        } else {
            // pipe ls into pipe write side
            if(debug) printf("child process starting ls\n");
            close(pipefd[0]);
            ERRCHK(dup2(pipefd[1], fileno(stdout)))
            execlp("ls", "ls", "-la", (char* )NULL);
            perror("exec ls Err:");
            exit(-1);
        }
    }
}


// runs the functionality of the client reads, processes, and executes commands
void runClient(int socketfd, char* myhost){
    while(1){
        printf("my-mftp-client> ");
        fflush(stdout);
        int readbufsize = 10+PATH_MAX;
        char buffer[readbufsize];
        readcmd((char* )buffer, 1, readbufsize); 


        // parse and validate commands
        char* cmd = strtok(buffer, " ");        
        char* cmdargs = strtok(NULL, " ");
        if(strtok(NULL, " ") != NULL){ // try to parse str a 3rd time for 3rd arg
            printf("err to many arguments\n");
            continue;
        } 
        if(cmd == NULL){ // check for empty command
            continue;
        }
        if(debug) printf("cmd = %s\n", cmd);
        if(cmdargs) if(debug) printf("cmd args = %s\n", cmdargs);

        if(strcmp(cmd, "exit") == 0){
            write(socketfd, (char*){"Q\n"}, 2);
            if(processBasicResponse(socketfd) < 0){
                printf("exit failed\n");
                continue;
            }
            printf("exiting normally\n");
            break;
        } else if(strcmp(cmd, "cd") == 0){
            if(cmdargs == NULL){
                printf("command error: expecting an arg\n");
            }
            // check we have read perms
            ERRCHK_VAR(access(cmdargs, R_OK|X_OK),continue)

            struct stat area, *s = &area;
            // populate stat struct
            ERRCHK_VAR(stat(cmdargs, s), continue)
            
            mode_t m = s->st_mode;
            // check if path is a directory
            if(!(S_ISDIR (m))){
                printf("Err not a directory\n");
                continue;
            }

            // do cd
            ERRCHK_VAR(chdir(cmdargs), continue)
            if(debug){
                char wd[PATH_MAX];
                getcwd(wd, PATH_MAX);
                printf("cwd is now:%s\n", wd);
            }
        } else if(strcmp(cmd, "rcd") == 0){
            if(cmdargs == NULL){
                printf("command error: expecting an arg\n");
            }
            ERRCHK_VAR(sendPacket(socketfd, cmdargs, 'C'),continue)
            processBasicResponse(socketfd);
        } else if(strcmp(cmd, "show") == 0){
            if(cmdargs == NULL){
                printf("command error: expecting an arg\n");
            }

            if(showfile(socketfd, cmdargs, myhost) < 0){
                printf("show failed\n");
            }
        } else if(strcmp(cmd, "ls") == 0){
            dols();

        } else if(strcmp(cmd, "rls") == 0){
            // setup data connection
            if(debug) printf("attempting data connection\n");
            int dataconnection = getDataConnection(socketfd, myhost);
            if(dataconnection < 0){
                printf("data connection failed\n");
                continue;
            } 

            // do server handshake
            ERRCHK_VAR(write(socketfd, (char*){"L\n"}, 2), continue);
            if(processBasicResponse(socketfd) < 0){
                close(dataconnection);
                continue;
            }

            // process remote ls
            int childid = fork();
            if(childid){ 
                if(debug) printf("forking more and waiting\n");
                wait(0);
                if(debug) printf("data display and more cmd finished\n");
                close(dataconnection);
            } else {
                if(debug) printf("displaying data from server\n");
                dup2(dataconnection, 0);
                execlp("more", "more", "-20",  (char* )NULL);
                perror("exec err");
                exit(-1);  
            }

        } else if(strcmp(cmd, "put") == 0){
            if(cmdargs == NULL){
                printf("command error: expecting an arg\n");
            }
            // do put file
            if(putfile(cmdargs, socketfd, myhost) < 0){
                printf("put failed\n");
            } else {
                if(debug) printf("put succeced and finished\n");
            }

        } else if(strcmp(cmd, "get") == 0){
            if(cmdargs == NULL){
                printf("command error: expecting an arg\n");
            }

            // parse filename from given pathname
            char *last, *filename = strtok(cmdargs, "/");
            while((last = strtok(NULL, "/")) != NULL) filename = last;

            // do get file
            if(getfile(filename, socketfd, myhost) < 0){
                printf("get failed\n");
            } else {
                if(debug) printf("get succeced and finished\n");
            }

        } else {
            printf("unknown cmd: %s\n", cmd);
        }
    }
}

int main(int argc, char* argv[]){

    // proccess commands
    int optionalArgsOffset = 0;
    if(strcmp(argv[1], "-d") == 0){
        optionalArgsOffset++;
        debug = 1;
    }    
    if(argc < 3+optionalArgsOffset){
        printf("err not enough arguments\n");
        return -1;
    }

    // setup connection
    char* connectedHost = argv[2+optionalArgsOffset];
    int socketfd = createConnection(argv[1+optionalArgsOffset], connectedHost);
    if(socketfd < 0) return -1;
    if(debug) printf("connected on fd %d\n", socketfd);

    // run client until exit is given
    runClient(socketfd, connectedHost);
    close(socketfd);
    return 0;
        
}

