#include "types.h"
#include "stat.h"
#include "user.h"
#include "syscall.h"

#define PGSIZE 4096

/**************** By Bicheng Wang ****************/

struct uproc {
  uint sz;                     // Size of process memory (bytes)
//  pde_t* pgdir;                // Page table
//  char *kstack;                // Bottom of kernel stack for this process
//  enum procstate state;        // Process state
//  int pid;                     // Process ID
//  struct proc *parent;         // Parent process
//  // why need we use trapframe? context switch in trap
//  struct trapframe *tf;        // Trap frame for current syscall
//  struct context *context;     // swtch() here to run process
//  void *chan;                  // If non-zero, sleeping on chan
//  int killed;                  // If non-zero, have been killed
//  struct file *ofile[NOFILE];  // Open files
//  struct inode *cwd;           // Current directory
//  char name[16];               // Process name (debugging)
};

//int getprocinfo(int pid, struct proc *up){
//	struct proc * p;
//	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
//		if(p->pid == pid){
//			break;
//		}
//	}
//	if(p->pid != pid){
//		panic("getprocinfo: fail to get right pid");
//		return -1;
//	}
//	up = (struct uproc * ) p;
//	return 0;
//}

/**
 * process name
 * process id, parent process id, size of process memory, process state, whether process is waiting on a channel, and whether it's been killed.
 */
//int printProInfo(struct proc * p){
//	static char *states[] = { [UNUSED] "unused", [EMBRYO] "embryo", [SLEEPING
//				] "sleep ", [RUNNABLE] "runble", [RUNNING] "run   ", [ZOMBIE
//				] "zombie" };
//	int i;
//	struct proc *up;
//	char *state;
//	uint pc[10];
//
//	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
//		if (p->state == UNUSED)
//			continue;
//		if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
//			state = states[p->state];
//		else
//			state = "???";
//		cprintf("%d %s %s", p->pid, state, p->name);
//		if (p->state == SLEEPING) {
//			getcallerpcs((uint*) p->context->ebp + 2, pc);
//			for (i = 0; i < 10 && pc[i] != 0; i++)
//				cprintf(" %p", pc[i]);
//		}
//		cprintf("\n");
//	}
//	cprintf("%s\t%d\t%s\t%d\t", p->name, p->pid, p->parent->pid, p->sz, p->state);
//	return 0;
//}

int myps() {
//	struct uproc * up;
//	for (struct proc p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
//		if(getprocinfo(p->pid, up) != 0){
//			panic("ps: fail to get process information");
//			return -1;
//		}
//		if(printProInfo(up)!=0){
//			panic("ps: fail to print process information");
//			return -1;
//		}
//	}
	return 0;
}

int main(int argc, char *argv[]) {
	myps();
	exit();
}
