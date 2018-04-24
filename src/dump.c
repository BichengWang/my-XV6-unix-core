#include "types.h"
#include "stat.h"
#include "user.h"
#include "syscall.h"

#define PGSIZE 4096

/**
 * My dump function, dump memory from kernel stack
 */
int myprint(char * buffer, uint sz, uint guardP){
	// print
	printf(1, "print starting:\nData and text region:\n");
	uint i = 0;
	for (; i < sz;i += 16) {
		if(i == guardP){
			printf(1, "Below is heap region\n Above is Guard page region\n");
		}
		if(i == guardP + PGSIZE){
			printf(1, "Stack region:\n");
		}
	//	if(i == guardP - PGSIZE){
	//		printf(1, "Heap region\n");
	//	}
		printf(1, "%p: ", i);
		printf(1, "\t0x%p ", *(uint *)(buffer + i));
		printf(1, "\t0x%p ", *(uint *)(buffer + i + 4));
		printf(1, "\t0x%p ", *(uint *)(buffer + i + 8));
		printf(1, "\t0x%p\n", *(uint *)(buffer + i + 12));
	}
	return 0;
}

int mydumper() {
	/* Fork a new process to play with */
	/* We don't have a good way to list all pids in the system
	 so forking a new process works for testing */
	
	uint buffersize = (uint) sbrk(0);
	int pid = fork();
	if (pid == 0) {
		/* child spins and yields */
		// childing is in process
		while (1) {
			// but will yield to parent for dump
			sleep(15);
		}
	}else{
	printf(1,"parent mydump");
	
	/* parent dumps memory of the child */
	char * buffer = (char*) malloc(buffersize + sizeof(uint));
	uint addr = 0x0;
	memset(buffer, 0, buffersize + sizeof(uint));
	printf(1, "mydump: start dump");
	if (dump(pid, (char*) &addr, buffer, buffersize) != 0) {
		printf(1, "dump: fail to dump");
	}
//	printf(1, "dump: print start");
	uint guardP;
	memmove(&guardP, buffer + buffersize, sizeof(uint));
	myprint(buffer, buffersize, guardP);
	free(buffer);
	}
	return 0;
}

int main(int argc, char *argv[]) {
	mydumper();
	exit();
}
