#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <assert.h>

#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
	int balance;
	int id;
} account;

typedef struct{
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	pid_t l_pid;
}flock;

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.
	int item;
    int wait_for_write;  // used by handle_read to know if the header is read or not.
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";

// Forwards

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

static int handle_read(request* reqP);
// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error

void write_to_client(char* s,int fd){
	char buf[512];
	sprintf(buf , s);
	write(fd , buf , strlen(buf));
	return;
}

pid_t checklock(int fd , int number , int type){
	flock lock;
	lock.l_type = type;
	lock.l_start = sizeof(int) * (number * 2 - 1);
	lock.l_whence = SEEK_SET;
	lock.l_len = sizeof(int);
	if(fcntl(fd , F_GETLK , &lock) < 0){
		fprintf(stderr , "fcntl error\n");
	}
	if(lock.l_type == F_UNLCK){
		return 0;
	}
	else return lock.l_pid;
}

void save(int index , int amount , account accounts[] , int fd){
	int file_fd = open("account_list" , O_RDWR);
    assert(file_fd != -1);
    for(int i = 1 ; i <= 20 ; i++){
		read(file_fd , &accounts[i].id , sizeof(int));
		read(file_fd , &accounts[i].balance , sizeof(int));
	}
	if(amount < 0){
		write_to_client("Operation failed.\n" , fd);
		close(file_fd);
		return;
	}
	amount += accounts[index].balance;
	lseek(file_fd , sizeof(account) * (index - 1) , SEEK_SET);
	lseek(file_fd , sizeof(int) , SEEK_CUR);
	write(file_fd , &amount , sizeof(int));
	close(file_fd);
	return;
}

void withdraw(int index , int amount , account accounts[] , int fd){
	int file_fd = open("account_list" , O_RDWR);
    assert(file_fd != -1);
    for(int i = 1 ; i <= 20 ; i++){
		read(file_fd , &accounts[i].id , sizeof(int));
		read(file_fd , &accounts[i].balance , sizeof(int));
	}
	if((amount < 0) || (amount > accounts[index].balance)){
		write_to_client("Operation failed.\n" , fd);
		close(file_fd);
		return;
	}
	amount = accounts[index].balance - amount;
	lseek(file_fd , sizeof(account) * (index - 1) , SEEK_SET);
	lseek(file_fd , sizeof(int) , SEEK_CUR);
	write(file_fd , &amount , sizeof(int));
	close(file_fd);
	return;
}

void transfer(int index , int index2 , int amount , account accounts[] , int fd){
	int file_fd = open("account_list" , O_RDWR);
    assert(file_fd != -1);
    for(int i = 1 ; i <= 20 ; i++){
		read(file_fd , &accounts[i].id , sizeof(int));
		read(file_fd , &accounts[i].balance , sizeof(int));
	}
	if((amount < 0) || (amount > accounts[index].balance)){
		write_to_client("Operation failed.\n" , fd);
		close(file_fd);
		return;
	}
	accounts[index].balance -=amount;
	accounts[index2].balance += amount;
	lseek(file_fd , sizeof(account) * (index - 1) , SEEK_SET);
	lseek(file_fd , sizeof(int) , SEEK_CUR);
	write(file_fd , &accounts[index].balance , sizeof(int));
	lseek(file_fd , sizeof(account) * (index2 - 1) , SEEK_SET);
	lseek(file_fd , sizeof(int) , SEEK_CUR);
	write(file_fd , &accounts[index2].balance , sizeof(int));
	close(file_fd);
	return;
}

void balance(int index , int amount , account accounts[] , int fd){
	int file_fd = open("account_list" , O_RDWR);
    assert(file_fd != -1);
    for(int i = 1 ; i <= 20 ; i++){
		read(file_fd , &accounts[i].id , sizeof(int));
		read(file_fd , &accounts[i].balance , sizeof(int));
	}
	if(amount < 0){
		write_to_client("Operation failed.\n" , fd);
		close(file_fd);
		return;
	}
	lseek(file_fd , sizeof(account) * (index - 1) , SEEK_SET);
	lseek(file_fd , sizeof(int) , SEEK_CUR);
	write(file_fd , &amount , sizeof(int));
	close(file_fd);
	return;
}

int main(int argc, char** argv) {
    int ret;

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    // Get file descripter table size and initize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

    fd_set readset;
    struct timeval tv;
    int checkstatus[maxfd];
    int writelock[21];
    for(int i = 1 ; i < 21 ; i++){
    	writelock[i] = 0;
    }
    for(int i = 0 ; i < maxfd ; i++){
    	checkstatus[i] = 0;
    }
#ifdef READ_SERVER
    file_fd = open("account_list" , O_RDONLY);
    assert(file_fd != -1);
#else
    file_fd = open("account_list" , O_RDWR);
    assert(file_fd != -1);
#endif
	account accounts[21];
	for(int i = 1 ; i <= 20 ; i++){
		read(file_fd , &accounts[i].id , sizeof(int));
		read(file_fd , &accounts[i].balance , sizeof(int));
	}
	close(file_fd);
    while (1) {
        // TODO: Add IO multiplexing
        
        // Check new connection
        clilen = sizeof(cliaddr);
        FD_ZERO(&readset);
        for(int i = 0; i <= maxfd ; i++){
        	if(requestP[i].conn_fd != -1){
        		FD_SET(requestP[i].conn_fd , &readset);
        	}
        }
    	FD_SET(svr.listen_fd , &readset);
    	tv.tv_sec = 1;
    	tv.tv_usec = 0;
    	int fdnum = select(maxfd , &readset , NULL , NULL , &tv);
    	if(fdnum <= 0){
    		continue;
    	}
    	if(FD_ISSET(svr.listen_fd , &readset)){
    		conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
    		if (conn_fd < 0) {
            	if (errno == EINTR || errno == EAGAIN) continue;  // try again
            	if (errno == ENFILE) {
                	(void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                continue;
            	}
            	ERR_EXIT("accept")
        	}
        	requestP[conn_fd].conn_fd = conn_fd;
        	strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
        	fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
        	continue;
    	}
#ifdef READ_SERVER
    	for(int i = 0; i < maxfd ; i++){
    		if((requestP[i].conn_fd != -1) && (FD_ISSET(requestP[i].conn_fd , &readset))){
    			ret = handle_read(&requestP[i]); // parse data from client to requestP[conn_fd].buf
				if (ret < 0) {
					fprintf(stderr, "bad request from %s\n", requestP[i].host);
						continue;
					}
				else{
					int index = atoi(requestP[i].buf);
					file_fd = open("account_list" , O_RDWR);
    				assert(file_fd != -1);
    				pid_t pid = checklock(file_fd , index , F_WRLCK);
    				close(file_fd);
					if(writelock[index] == 0 && pid == 0){
						file_fd = open("account_list" , O_RDONLY);
    					assert(file_fd != -1);
    					for(int i = 1 ; i <= 20 ; i++){
							read(file_fd , &accounts[i].id , sizeof(int));
							read(file_fd , &accounts[i].balance , sizeof(int));
						}
						close(file_fd);
						sprintf(buf , "%d %d\n" , index , accounts[index].balance);
						write(requestP[i].conn_fd , buf , strlen(buf));
					}
					else{
						sprintf(buf , "This account is locked.\n");
						write(requestP[i].conn_fd , buf , strlen(buf));
					}
					close(requestP[conn_fd].conn_fd);
					free_request(&requestP[conn_fd]);
					continue;
				}
    		}
    	}
#else	
    	int flag = 0;
    	for(int i = 0; i < maxfd ; i++){
    		if((requestP[i].conn_fd != -1) && (FD_ISSET(requestP[i].conn_fd , &readset))){
    			flag++;
    			ret = handle_read(&requestP[i]); // parse data from client to requestP[conn_fd].buf
    			if(checkstatus[requestP[i].conn_fd] == 0){
    				if (ret < 0) {
					fprintf(stderr, "bad request from %s\n", requestP[i].host);
						continue;
					}
					else{
						int index = atoi(requestP[i].buf);
						file_fd = open("account_list" , O_RDWR);
    					assert(file_fd != -1);
    					pid_t pid = checklock(file_fd , index , F_WRLCK);
						if(writelock[index] == 0 && pid == 0){
							sprintf(buf , "This account is modifiable.\n");
							write(requestP[i].conn_fd , buf , strlen(buf));
							checkstatus[requestP[i].conn_fd] = index;
							flock lock;
							lock.l_type = F_WRLCK;
							lock.l_whence = SEEK_SET;
							lock.l_start = sizeof(int) * (index * 2 - 1);
							lock.l_len = sizeof(int);
							lock.l_pid = getpid();
							fcntl(file_fd , F_SETLK , &lock);				
							writelock[index] = 1;
						}
						else{
							sprintf(buf , "This account is locked.\n");
							write(requestP[i].conn_fd , buf , strlen(buf));
							close(requestP[conn_fd].conn_fd);
							free_request(&requestP[conn_fd]);
						}
						continue;
					}
    			}
    			else{
    				int index = checkstatus[requestP[i].conn_fd];
    				char *start = requestP[i].buf;
    				start = strtok(start , " ");
    				if(start[0] == 's'){
    					start = strtok(NULL , "\n");
    					int amount = atoi(start);
    					save(index , amount , accounts , requestP[i].conn_fd);
    					checkstatus[requestP[i].conn_fd] = 0;
    					writelock[index] = 0;
    				}
    				else if(start[0] == 'w'){
    					start = strtok(NULL , "\n");
    					int amount = atoi(start);
    					withdraw(index , amount , accounts , requestP[i].conn_fd);
    					checkstatus[requestP[i].conn_fd] = 0;
    					writelock[index] = 0;
    				}
    				else if(start[0] == 't'){
    					start = strtok(NULL , " ");
    					int index2 = atoi(start);
    					start = strtok(NULL , "\n");
    					int amount = atoi(start);
    					transfer(index , index2 , amount , accounts , requestP[i].conn_fd);
    					checkstatus[requestP[i].conn_fd] = 0;
    					writelock[index] = 0;
    				}
    				else{
    					start = strtok(NULL , "\n");
    					int amount = atoi(start);
    					balance(index , amount , accounts , requestP[i].conn_fd);
    					checkstatus[requestP[i].conn_fd] = 0;
    					writelock[index] = 0;
    				}
    					file_fd = open("account_list" , O_RDWR);
    					assert(file_fd != -1);
    					flock lock;
						lock.l_type = F_UNLCK;
						lock.l_whence = SEEK_SET;
						lock.l_start = sizeof(int) * (index * 2 - 1);
						lock.l_len = sizeof(int);
						lock.l_pid = getpid();
						fcntl(file_fd , F_SETLK , &lock);
						close(file_fd);
    					close(requestP[conn_fd].conn_fd);
						free_request(&requestP[conn_fd]);
    					continue;
    			}
    		}
    	}
    	if(flag > 0){
    		continue;
    	}
#endif
/*		ret = handle_read(&requestP[conn_fd]); // parse data from client to requestP[conn_fd].buf
		if (ret < 0) {
			fprintf(stderr, "bad request from %s\n", requestP[conn_fd].host);
			continue;
		}*/

#ifdef READ_SERVER
		sprintf(buf,"%s : %s\n",accept_read_header,requestP[conn_fd].buf);
		write(requestP[conn_fd].conn_fd, buf, strlen(buf));
#else
		sprintf(buf,"%s : %s\n",accept_write_header,requestP[conn_fd].buf);
		write(requestP[conn_fd].conn_fd, buf, strlen(buf));
#endif

	/*	close(requestP[conn_fd].conn_fd);
		free_request(&requestP[conn_fd]);*/
    }
    free(requestP);
    return 0;
}


// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void* e_malloc(size_t size);


static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->item = 0;
    reqP->wait_for_write = 0;
}

static void free_request(request* reqP) {
    /*if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }*/
    init_request(reqP);
}

// return 0: socket ended, request done.
// return 1: success, message (without header) got this time is in reqP->buf with reqP->buf_len bytes. read more until got <= 0.
// It's guaranteed that the header would be correctly set after the first read.
// error code:
// -1: client connection error
static int handle_read(request* reqP) {
    int r;
    char buf[512];

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
	char* p1 = strstr(buf, "\015\012");
	int newline_len = 2;
	// be careful that in Windows, line ends with \015\012
	if (p1 == NULL) {
		p1 = strstr(buf, "\012");
		newline_len = 1;
		if (p1 == NULL) {
			ERR_EXIT("this really should not happen...");
		}
	}
	size_t len = p1 - buf + 1;
	memmove(reqP->buf, buf, len);
	reqP->buf[len - 1] = '\0';
	reqP->buf_len = len-1;
    return 1;
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }
}

static void* e_malloc(size_t size) {
    void* ptr;

    ptr = malloc(size);
    if (ptr == NULL) ERR_EXIT("out of memory");
    return ptr;
}

