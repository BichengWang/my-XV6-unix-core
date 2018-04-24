#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int sys_fork(void) {
	return fork();
}

int sys_exit(void) {
	exit();
	return 0;  // not reached
}

int sys_wait(void) {
	return wait();
}

int sys_kill(void) {
	int pid;

	if (argint(0, &pid) < 0)
		return -1;
	return kill(pid);
}

int sys_getpid(void) {
	return myproc()->pid;
}

int sys_sbrk(void) {
	int addr;
	int n;

	if (argint(0, &n) < 0)
		return -1;
	addr = myproc()->sz;
	if (growproc(n) < 0)
		return -1;
	return addr;
}

int sys_sleep(void) {
	int n;
	uint ticks0;

	if (argint(0, &n) < 0)
		return -1;
	acquire(&tickslock);
	ticks0 = ticks;
	while (ticks - ticks0 < n) {
		if (myproc()->killed) {
			release(&tickslock);
			return -1;
		}
		sleep(&ticks, &tickslock);
	}
	release(&tickslock);
	return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int sys_uptime(void) {
	uint xticks;

	acquire(&tickslock);
	xticks = ticks;
	release(&tickslock);
	return xticks;
}

int sys_dump(void) {
	int pid;
	char * addr;
	char * buffer;
	uint buffersize;
	if (argint(0, &pid) < 0 || argptr(1, &addr, sizeof(addr)) < 0
			|| argptr(2, &buffer, sizeof(buffer)) < 0
			|| argint(3, (int*) &buffersize) < 0) {
		panic("sys_dump: args error");
		return -1;
	}
	return dump(pid, addr, buffer, buffersize);
}

int sys_ps(void) {
	return ps();
}
// sys_clone
int sys_thread_create(void) {
	void (*fcn)(void*), *arg, *stack;
	argptr(0, (void*) &fcn, sizeof(void (*)(void *)));
	argptr(1, (void*) &arg, sizeof(void *));
	argptr(2, (void*) &stack, sizeof(void *));
	return thread_create(fcn, arg, stack);
}

// sys_join
int sys_thread_join(void) {
	return thread_join();
}

int sys_thread_exit(void) {
	return thread_exit();
}

int sys_mysleep(void) {
	void * arg1;
	void * arg2;
	argptr(0, (void*) &arg1, sizeof(void *));
	argptr(1, (void*) &arg2, sizeof(void *));
	return mysleep(arg1, arg2);
}

int sys_mywakeup(void) {
	void * arg1;
	argptr(0, (void*) &arg1, sizeof(void *));
	return mywakeup(arg1);
}
