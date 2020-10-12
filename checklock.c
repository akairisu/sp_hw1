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
	int file_fd = open("test" , O_WRONLY);
	assert(file_fd != -1);
	flock lock;
	lock.l_type = F_RDLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 0;
	pid_t pid = checklock(file_fd , F_RDLCK);
	printf("%d\n" , (int)pid);
	return 0;
}