#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <assert.h>

typedef struct{
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	pid_t l_pid;
}flock;

int lock_reg(int fd , int cmd , int type , off_t offset , int whence , off_t len){
	flock lock;
	lock.l_type = type;
	lock.l_whence = whence;
	lock.l_start = offset;
	lock.l_len = len;
	lock.l_pid = getpid();
	return(fcntl(fd , cmd , &lock));
}

pid_t checklock(int fd , int type){
	flock lock;
	lock.l_type = type;
	lock.l_start = sizeof(int);
	lock.l_whence = SEEK_SET;
	lock.l_len = sizeof(int);
	if(fcntl(fd , F_GETLK , &lock) < 0){
		printf("fcntl error\n");
	}
	else{
		printf("%d\n" , lock.l_type);
	}
	if(lock.l_type == F_UNLCK){
		printf("unlocked\n");
		return 0;
	}
	else return lock.l_pid;
}

int main(void){
	int file_fd;
	pid_t pid;
	printf("RD : %d\n" , F_RDLCK);
	printf("WR : %d\n" , F_WRLCK);
	printf("UN : %d\n" , F_UNLCK);
	file_fd = open("test" , O_WRONLY);
	assert(file_fd != -1);
	flock lock;
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = sizeof(int);
	lock.l_len = sizeof(int);
	lock.l_pid = getpid();
	fcntl(file_fd , F_SETLK , &lock);
	int a;
	while(scanf("%d" , &a) != EOF){
		if(a == 1){
			lock.l_type = F_UNLCK;
			fcntl(file_fd , F_SETLK , &lock);
			printf("123\n");
		}
		if(a == 2){
			close(file_fd);
		}
		continue;
	}
	return 0;
}