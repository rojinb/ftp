#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include "dir.h"
#include "usage.h"

#define MAXDATASIZE 512 
#define IMAGE 2
#define ASCII 1

#define BACKLOG 10 // how many pending connections queue will hold

int logged_in = 0;
int not_closed = 1;
char* owd;
int is_passive_mode = 0;
int psv_sockfd = 0;
int psv_port = -1;
int test_sockfd;
int type = 1;


// closes the connection to given socket file descriptor
void end_connection(int sock) {
    logged_in = 0;
    not_closed = 0;
    is_passive_mode = 0;
    close(sock);
}

void handle_username(int sock, char* param, int size){
    char* msg;
    // if already logged in, sned 500
    if (logged_in == 1) {
        msg = "500 Unknown command\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    }

    // if not already logged in, check if username is right (ie cs317)
    else if (size < 1 || strcmp(param, "cs317\r\n") !=0)  {
        msg = "530 this ftp server only accepts cs317\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    } else {
        msg = "230 login successful\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        } else {
            logged_in = 1;
        }
    }
}

void handle_type(int sock, char* param, int size){
    char* msg;
    // must first be logged in
    if (logged_in==0) {
        msg = "530 login required\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    } 
    // only accept types I and A (i and a)
    else if (size != 3 || (strcmp(param, "I\r\n") !=0 && strcmp(param, "A\r\n") !=0 &&
        strcmp(param, "i\r\n") !=0 && strcmp(param, "a\r\n") !=0)) {
        msg = "500 Unrecognized type command\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }

    } 
    // provided type is correct; so now set the type and send success msg
    else {
        if (strcmp(param, "I\r\n") !=0 || strcmp(param, "i\r\n") !=0) {
            type = IMAGE;
        } else {
            type = ASCII;
        }
        msg = "200 switching type\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    }
}

void handle_mode(int sock, char* param, int size){
    char* msg;
    // must first be logged in
    if (logged_in==0) {
        msg = "530 login required\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    } 
    // only accept mode S (s); otherwise send 500
    else if (size != 3 || (strcmp(param, "s\r\n") !=0 && strcmp(param, "S\r\n") !=0))  {
        msg = "500 Unrecognized transfer mode\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }

    } else {
        msg = "200 Mode set to S\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    }
}

void handle_cwd(int sock, char* param, int size){
    char* msg;
    // must be logged in to use cwd
    if (logged_in==0) {
        msg = "530 login required\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    } 
    // check that param is not empty
    else if (size < 1)  {
        msg = "500 Usage\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    } else {
        // check for ./ and ../; must send 501 
        char substr3[4]; // check for ../
        strncpy( substr3, param, 3);
        substr3[4]='\0';

        char substr2[3];  // check for ./
        strncpy( substr2, param, 2);
        substr2[3]='\0';
        // do not accept path starting with ./ or ../
        if (strcmp(substr2, "./")==0 || strcmp(substr3, "../")==0) {
            msg = "501 bad path\r\n";
            if (send(sock, msg, strlen(msg), 0) == -1){
                perror("send");
                end_connection(sock);
            }
        } else {
            // do the actual dir change
            // strip /r/n
            char* path = param;
            path[strlen(path)-2] = '\0';

            // try to switch dir; if failed, means maybe path does not exist, send 550
            if (chdir(path) == -1) {
                msg = "550 Failed to change directory.\r\n";
                if (send(sock, msg, strlen(msg), 0) == -1){
                    perror("send");
                    end_connection(sock);
                }
            } else {
                // if changed dir successfully; sned 250
                msg = "250 Directory successfully changed.\r\n";
                if (send(sock, msg, strlen(msg), 0) == -1){
                    perror("send");
                    end_connection(sock);
                }
            }
        } 
    }
}

void handle_stru(int sock, char* param, int size) {
    char* msg;
    // must be logged in to use stru
    if (logged_in==0) {
        msg = "530 login required\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    } 
    // check param is correct, ie F or f
    else if (size != 3 || (strcmp(param, "f\r\n") !=0 && strcmp(param, "F\r\n") !=0))  {
        msg = "501 Unrecognized file structure\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }

    } else {
        msg = "200 Structure set to F\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    }
}


void handle_cdup(int sock) { 
    char* msg;
    // must be logged in to use cdup
    if (logged_in==0) {
        msg = "530 login required\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
        return;
    }

    // get currect working directory
    char pathname[512] = "";
    char* cwd;
    cwd = getcwd( pathname, 512);

    // only cdup if the current working directory is not servers directory (owd)
    if (strcmp(cwd, owd) == 0) {
        msg = "550 No access\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
        return;
    }
    // not inside servers directory so try to change to parent dir
    if (chdir("../") == -1) {
        // for some readon can't go to parent dir so send 550
        msg = "550 Failed to change to parent directory.\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    } else {
        // changed to parent dir fine (chdir did not return -1) so send 200
        msg = "200 Directory successfully changed.\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
    }
}

int accept_pasv_connection(int sock){
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    // in passive mode
    //timeout
    // add file descriptor to the fd set
    fd_set fd_set;
    FD_ZERO(&fd_set);
    int rc;
    FD_SET(test_sockfd, &fd_set);

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    printf("before select%s\n", "");
    rc = select (test_sockfd+1, &fd_set, NULL, NULL, &timeout);
    printf("rc = %d\n", rc);


    if (rc > 0) {// process an accept
        sin_size = sizeof their_addr;
        psv_sockfd = accept(test_sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (psv_sockfd == -1) {
            perror("accept");
        }
        close(test_sockfd);
        return 0;
    } else {
        if (send(sock, "425 Failed to establish data connection.\r\n", 42, 0) == -1){
            perror("send");
            end_connection(sock);
            close(test_sockfd);
        }
        return -1;
    }

}


void handle_retr(int sock, char* param, int size) {
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    char* msg;
    // must be logged in to use this command
    if (logged_in==0) {
        if (send(sock, "530 login required\r\n", 20, 0) == -1){
            perror("send");
            end_connection(sock);
        }
        return;
    }
    // must be in passive mode to use this command
    if (is_passive_mode == 0) {
        msg = "425 Use PASV first.\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1) {
            perror("send");
            end_connection(sock);
        }
        return;
    }
    // get current working directory
    char pathname[512] = "";
    char* cwd;
    cwd = getcwd( pathname, 512);

    // must be inside servers directory to retrieve a file
    // ie the file must be inside servers directory (cwd must be same as owd)
    if (strcmp(cwd, owd) != 0) {
        msg = "550 Can only access files inside server's directory\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
        return;
    }

    if (accept_pasv_connection(sock) == -1) { // timed out
        return;
    }
    // strip \r\n from the command parameter to get filename
    char* file = param;
    file[strlen(file)-2] = '\0';

    // check if file exists (and has regular file type) in currrent working directory
    int file_size;
    if ((file_size = getFile(cwd, file)) == -1) {
        // file does not exist in this directory (where server runs from)
        msg = "550 Failed to open file.\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
        return;
    }

    // read file in buffer and send
    msg = "150 Opening mode data connection\r\n";
    if (send(sock, msg, strlen(msg), 0) == -1){
        perror("send");
        end_connection(sock);
    }
    
    char buffer[file_size+100];
    FILE * filp;
    // open file based on type
    if (type==IMAGE) {
        filp = fopen(file, "rb");    
    } else {filp = fopen(file, "r");}

    // if file could not be opened, send error
    if (filp == NULL) {
        msg = "550 Failed to open file.\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
        return;
    }
    // read the file into buffer
    int bytes_read = fread(buffer, sizeof(char), (file_size+100), filp);

    // send buffer content over pasv connection(use passive socket file descp)
    if (send(psv_sockfd, buffer, bytes_read, 0) == -1) {
        perror("send");
        end_connection(sock);
    }

    // close the data connection to pasv (we are done)
    is_passive_mode = 0;
    close(psv_sockfd);
    psv_sockfd = 0;

    msg = "226 File send ok.\r\n";
    if (send(sock, msg, strlen(msg), 0) == -1) {
        perror("send");
        end_connection(sock);
    }
}



void handle_nlst(int sock) {
    
    char* msg;
    // must be loged in to use this command
    if (logged_in==0) {
        msg = "530 login required\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1) {
            perror("send");
            end_connection(sock);
        }
        return;
    }
    // not in passive mode; send 425
    if (is_passive_mode == 0) {
        msg = "425 Use PASV first.\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1) {
            perror("send");
            end_connection(sock);
        }
        return;

    } 
    // get current working dir
    char pathname[512] = "";
    char* cwd;
    cwd = getcwd( pathname, 512);

    // can only use nlist if cwd is same as servers directory
    if (strcmp(cwd, owd) != 0) {
        msg = "550 Can only access server's directory\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1){
            perror("send");
            end_connection(sock);
        }
        return;
    }

    if (accept_pasv_connection(sock) == -1) { // timed out
        return;
    }
    
    msg = "150 Here comes the directory listing.\r\n";
    if (send(sock, msg, strlen(msg), 0) == -1) {
        perror("send");
        end_connection(sock);
    }
    // call listFiles function with passive socket

    if (listFiles(psv_sockfd, ".") == -1) {
        msg = "550 Failed to open directory.\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1) {
            perror("send");
            end_connection(sock);
        }
    } else {
        msg = "226 Directory send OK.\r\n";
        if (send(sock, msg, strlen(msg), 0) == -1) {
            perror("send");
            end_connection(sock);
        }
    }
    // Close passive connection
    is_passive_mode = 0;
    close(psv_sockfd);
    psv_sockfd = 0;

}

/**
 * Get ip where client connected to
 * @param sock Commander socket connection
 * @param ip Result ip array (length must be 4 or greater)
 * result IP array e.g. {127,0,0,1}
 */
void getip(int sock, int *ip)
{
  socklen_t addr_size = sizeof(struct sockaddr_in);
  struct sockaddr_in addr;
  getsockname(sock, (struct sockaddr *)&addr, &addr_size);
 
 // check after this
  char* host = inet_ntoa(addr.sin_addr);
  sscanf(host,"%d.%d.%d.%d",&ip[0],&ip[1],&ip[2],&ip[3]);
}

// taked command and changes it to upper case and stores it in new_buf
void uppercase(char* cmd, int size, char* new_buf){
    int i = 0;
    for(i = 0; i < size; i++){
        new_buf[i] = toupper(cmd[i]);
    }
}

int createNewSocket(char* port) {
    // some of the code taken from tutorial 9 beej's guide to network programming
    printf("trying to creaate socket on port %s\n", port);

    //create socket on provided port and return sockfd
    // else .. 
    struct addrinfo hints, *servinfo, *p;
    int socketfd;
    int yes=1;
    memset(&hints, 0, sizeof hints);

    // variables used in send/recv from client:
    int rv;
    
    
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;


    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {  
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); 
        return -1;
    }


    for(p = servinfo; p != NULL; p = p->ai_next) {     
        if ((socketfd = socket(p->ai_family, p->ai_socktype,
            p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &yes, 
            sizeof(int)) == -1) {
                perror("setsockopt");
                return -1;
        }
        if (bind(socketfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(socketfd);
            perror("server: bind");
            continue;
        }
        struct sockaddr_in temp_addr;
        socklen_t length = sizeof(temp_addr);

        getsockname(socketfd, (struct sockaddr *)&temp_addr, &length);

        psv_port = ntohs(temp_addr.sin_port);

        //psv_port = sa_addr->sin_port;
        printf("sin_port is %d\n", psv_port);

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return -1;
    }

    if (listen(socketfd, 1) == -1) { // we only need 1 connection
        perror("listen");
        //exit(1);
        return -1;
    }

    return socketfd;

}

/* calls handlers for commands with 1 parameter*/
void handle_cmd1(int sock, char* cmd, char* param) {
    if (strcmp(cmd, "USER") == 0){
        handle_username(sock, param, strlen(param));
    } else if (strcmp(cmd, "TYPE") == 0){
        handle_type(sock, param, strlen(param));
    } else if (strcmp(cmd, "MODE") == 0) {
        handle_mode(sock, param, strlen(param));
    } else if (strcmp(cmd, "CWD") == 0) {
        handle_cwd(sock, param, strlen(param));
    } else if (strcmp(cmd, "STRU") == 0) {
        handle_stru(sock, param, strlen(param));
    } else if (strcmp(cmd, "RETR") == 0) {
        handle_retr(sock, param, strlen(param));
    } else if (strcmp(cmd, "NLST") == 0) {
        if (send(sock, "501 too many parameters to NLST.\r\n", 22, 0) == -1){
            perror("send");
            end_connection(sock);
        }
    } else {
        if (send(sock, "500 Unknown command.\r\n", 22, 0) == -1){
            perror("send");
            end_connection(sock);
        }
    }
}

/* calls handlers for commands with 0 parameter*/
void handle_cmd0(int sock, char* cmd) {
    // we know we have no arguments

    char* msg;
    int ip[4];
    if (strcmp("QUIT\r\n", cmd) == 0) {
        // send goodbye message, return error if una
        if (send(sock, "221 Goodbye\r\n", 13, 0) == -1){
            perror("send");
        }
        end_connection(sock);

    }
    else if (strcmp("CDUP\r\n", cmd) == 0) {
        handle_cdup(sock);
    }
    else if (strcmp("PASV\r\n", cmd) == 0) {
        // must be logged in to setup pasv
        if (logged_in==0) {
            msg = "530 login required\r\n";
            if (send(sock, msg, strlen(msg), 0) == -1) {
                perror("send");
                end_connection(sock);
            }
        }

        else {
            int i;
            char port[100];

            test_sockfd = createNewSocket("0"); // createNewSocket sets global psv_port

            // convert psv_port to p1 and p2
            int p1;
            int p2;

            p1 = psv_port/256; // integer division
            p2 = psv_port - (p1*256);

            // get host ip
            getip(sock, ip);

            char passiveMessage[100];
            snprintf(passiveMessage, 100, "227 Entering Passive Mode (%d, %d, %d, %d, %d, %d)", ip[0], ip[1], ip[2], ip[3], p1, p2);
            strcat(passiveMessage,".\r\n");

            if (send(sock, passiveMessage, strlen(passiveMessage), 0) == -1) {
                perror("send");
                end_connection(sock);
            }

            // wait for connection
            if (listen(test_sockfd, 1) == -1) {
                    perror("listen");
                    exit(1);
            }
            is_passive_mode = 1;
            
        }

    } else if (strcmp("NLST\r\n", cmd) == 0) {
        handle_nlst(sock);

    } else {
        if (send(sock, "500 Unknown command.\r\n", 22, 0) == -1){
            perror("send");
            end_connection(sock);
        }
    }
}



int main(int argc, char **argv) {
    int i;
    
    // Check the command line arguments
    if (argc != 2) {
      usage(argv[0]);
      return -1;
    }

    char *port = argv[1];
    int sockfd, new_fd; 

    // try to create socket, and exit if failed
    sockfd = createNewSocket(port);
    if (sockfd == -1) {
        perror("listen");
        exit(-1);
    }

    struct sockaddr_storage their_addr; 
    socklen_t sin_size;
    sin_size = sizeof their_addr;

    char s[INET6_ADDRSTRLEN];
    int rec_bytes;
    char buf[MAXDATASIZE];

    // get current working directory to set original working dir for server
    char pathname[512] = "";
    owd = getcwd( pathname, 512);

    while(1) {

        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);

        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        // make sure start at original directory
        chdir(owd);

        // send 220; we have successfully established a connection
        if (send(new_fd, "220\r\n", 5, 0) == -1){
            perror("send");
            end_connection(new_fd);
        }

        not_closed = 1;
        // accept and act on commands from client until client closes connection
        // or calls quit which closes connection
        while (not_closed) {
            // recv a command from client
            if ((rec_bytes=recv(new_fd, buf, MAXDATASIZE, 0)) == -1) {
                perror("recv");
                end_connection(new_fd);

            } 
            // act on commands recvd
            else if (rec_bytes>0) {
                buf[rec_bytes] = '\0'; 

                //printf("The message recvd is %s\n", buf);

                // split ccommand recv into arrays to separate cmd and its parameter
                char *pc[50];
                char* pi;
                int i = 0;
                pi = strtok(buf, " ");
                pc[i] = pi;
                while(pi!=NULL){
                    i++;
                    pi = strtok(NULL, " ");
                    pc[i] = pi;

                }
                
                // non of the commands supported by this server can have more than 1 parameter
                if (i > 2) {
                    char* msg = "501 too many parameters\r\n";
                    if (send(new_fd, msg, strlen(msg), 0) == -1){
                        perror("send");
                        end_connection(new_fd);
                    }
                } else {
                    // make pc[0] upper case; pc[0] is the command
                    int cmd_size = strlen(pc[0]);
                    char cmd[cmd_size];
                    uppercase(pc[0], cmd_size, cmd);

                    // check which cmd pc[0] is
                    // handle cmd with/ without its arguement pc[1]
                    if (i == 2) {
                        handle_cmd1(new_fd, cmd, pc[1]);
                    } else if (i == 1) {
                        handle_cmd0(new_fd, cmd);
                    } 
                }

            } else {
                // recv command size not > 0 so we know
                // client closed on us, just close the file descriptor for this
                // client and break out of the send/recv cmd loop and wait for new client
                end_connection(new_fd);
            }
        }
        
    }
    close(sockfd);
    
    return 0;

}
