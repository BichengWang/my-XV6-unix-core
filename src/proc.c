#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "lock.h"

struct {
	struct spinlock lock;
	struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void) {
	initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid() {
	return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void) {
	int apicid, i;

	if (readeflags() & FL_IF)
		panic("mycpu called with interrupts enabled\n");

	apicid = lapicid();
	// APIC IDs are not guaranteed to be contiguous. Maybe we should have
	// a reverse map, or reserve a register to store &cpus[i].
	for (i = 0; i < ncpu; ++i) {
		if (cpus[i].apicid == apicid)
			return &cpus[i];
	}
	panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
	struct cpu *c;
	struct proc *p;
	pushcli();
	c = mycpu();
	p = c->proc;
	popcli();
	return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void) {
	struct proc *p;
	char *sp;

	acquire(&ptable.lock);

	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		if (p->state == UNUSED)
			goto found;

	release(&ptable.lock);
	return 0;

	found: p->state = EMBRYO;
	p->pid = nextpid++;

	release(&ptable.lock);

	// Allocate kernel stack.
	if ((p->kstack = kalloc()) == 0) {
		p->state = UNUSED;
		return 0;
	}

	// allocate one page as kernel stack
	sp = p->kstack + KSTACKSIZE;

	// Leave room for trap frame.
	// allocate space for process's total trap frame size
	sp -= sizeof *p->tf;
	// let the pointer point to this address
	p->tf = (struct trapframe*) sp;

	// Set up new context to start executing at forkret,
	// which returns to trapret.
	sp -= 4;
	*(uint*) sp = (uint) trapret;

	// allocate space for total context size
	sp -= sizeof *p->context;
	// let the pointer point to this address
	p->context = (struct context*) sp;
	memset(p->context, 0, sizeof *p->context);

	// important: eip should point to the next instruction of trap
	// directly execute from the trap return enter to go out of kernel level
	// so we don't need to set other kernel call in stack, but can just directly return start from alltraps
	p->context->eip = (uint) forkret;

	return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void) {
	struct proc *p;
	// _binary_initcode_start is from initcode.S and less than one page
	extern char _binary_initcode_start[], _binary_initcode_size[];

	// allocate a unused process from process table
	// the process has already had kstack, context, and so on
	// this process's eip point to alltraps return, so we can directly return to user level from alltraps
	p = allocproc();

	initproc = p;
	// setup pgdir
	if ((p->pgdir = setupkvm()) == 0)
		panic("userinit: out of memory?");

	// initial user virtual memory, and copy source code into this page
	// exec() is the first user level system call
	inituvm(p->pgdir, _binary_initcode_start, (int) _binary_initcode_size);

	// initial user process size
	p->sz = PGSIZE;

	// initial trap frame
	memset(p->tf, 0, sizeof(*p->tf));
	p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
	p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
	p->tf->es = p->tf->ds;
	p->tf->ss = p->tf->ds;
	p->tf->eflags = FL_IF;
	p->tf->esp = PGSIZE;
	p->tf->eip = 0;  // beginning of initcode.S

	safestrcpy(p->name, "initcode", sizeof(p->name));

	// linked current directory at root as inode
	p->cwd = namei("/");

	// this assignment to p->state lets other cores
	// run this process. the acquire forces the above
	// writes to be visible, and the lock is also needed
	// because the assignment might not be atomic.
	acquire(&ptable.lock);

	p->state = RUNNABLE; // set process runnable and waiting for scheduling

	release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n) {
	uint sz;
	struct proc *curproc = myproc();

	sz = curproc->sz;
	if (n > 0) {
		if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
			return -1;
	} else if (n < 0) {
		if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
			return -1;
	}
	curproc->sz = sz;
	switchuvm(curproc);
	return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void) {
	int i, pid;
	struct proc *np;
	struct proc *curproc = myproc();

	// Allocate process.
	if ((np = allocproc()) == 0) {
		return -1;
	}

	// Copy process state from proc.
	if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0) {
		kfree(np->kstack);
		np->kstack = 0;
		np->state = UNUSED;
		return -1;
	}
	np->sz = curproc->sz;
	np->parent = curproc;
	*np->tf = *curproc->tf;

	// Clear %eax so that fork returns 0 in the child.
	np->tf->eax = 0;

	// copy open files
	for (i = 0; i < NOFILE; i++)
		if (curproc->ofile[i])
			np->ofile[i] = filedup(curproc->ofile[i]);
	np->cwd = idup(curproc->cwd);

	safestrcpy(np->name, curproc->name, sizeof(curproc->name));

	pid = np->pid;

	acquire(&ptable.lock);

	np->state = RUNNABLE;

	release(&ptable.lock);

	return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void) {
	struct proc *curproc = myproc();
	struct proc *p;
	int fd;

	if (curproc == initproc)
		panic("init exiting");

	// Close all open files.
	for (fd = 0; fd < NOFILE; fd++) {
		if (curproc->ofile[fd]) {
			fileclose(curproc->ofile[fd]);
			curproc->ofile[fd] = 0;
		}
	}

	begin_op();
	iput(curproc->cwd);
	end_op();
	curproc->cwd = 0;

	acquire(&ptable.lock);

	// Parent might be sleeping in wait().
	wakeup1(curproc->parent);

	// Pass abandoned children to init.
	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
		if (p->parent == curproc) {
			p->parent = initproc;
			if (p->state == ZOMBIE)
				wakeup1(initproc);
		}
	}

	// Jump into the scheduler, never to return.
	curproc->state = ZOMBIE;
	sched();
	panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void) {
	struct proc *p;
	int havekids, pid;
	struct proc *curproc = myproc();

	acquire(&ptable.lock);
	for (;;) {
		// Scan through table looking for exited children.
		havekids = 0;
		for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
			if (p->parent != curproc)
				continue;
			havekids = 1;
			if (p->state == ZOMBIE) {
				// Found one.
				pid = p->pid;
				kfree(p->kstack);
				p->kstack = 0;
				freevm(p->pgdir);
				p->pid = 0;
				p->parent = 0;
				p->name[0] = 0;
				p->killed = 0;
				p->state = UNUSED;
				release(&ptable.lock);
				return pid;
			}
		}

		// No point waiting if we don't have any children.
		if (!havekids || curproc->killed) {
			release(&ptable.lock);
			return -1;
		}

		// Wait for children to exit.  (See wakeup1 call in proc_exit.)
		sleep(curproc, &ptable.lock);  //DOC: wait-sleep
	}
}

// CPU schedule start
//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void scheduler(void) {
	struct proc *p;
	struct cpu *c = mycpu();
	c->proc = 0; // initial cpu process

	for (;;) {
		// Enable interrupts on this processor.
		sti();

		// Loop over process table looking for process to run.
		acquire(&ptable.lock);
		for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
			if (p->state != RUNNABLE)
				continue;

			// Switch to chosen process.  It is the process's job
			// to release ptable.lock and then reacquire it
			// before jumping back to us.
			c->proc = p; // link process to cpu
			switchuvm(p); // switch to process's user virtual memory
			p->state = RUNNING;

			// swtich current cpu process with new process
			// And execute new process
			// Only when new process yield or finish, the instruction strea  ssaaassaaaassssssssaaam will go to the following
			swtch(&(c->scheduler), p->context);

			// TODO: ?? why need to switch kvm again?
			switchkvm();

			// Process is done running for now.
			// It should have changed its p->state before coming back.
			c->proc = 0;
		}
		release(&ptable.lock);

	}
}

// General sched
// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void) {
	int intena;
	struct proc *p = myproc();

	// need to holding ptable lock at current processing
	if (!holding(&ptable.lock))
		panic("sched ptable.lock");
	if (mycpu()->ncli != 1)
		panic("sched locks");
	if (p->state == RUNNING)
		panic("sched running");
	if (readeflags() & FL_IF)
		panic("sched interruptible");
	intena = mycpu()->intena;
	// context switch is just switch the pointer of current context with scheduler first context
	// TODO: switch process to cpu scheduler process, and let cpu scheduler process using scheduler to find the next RUNNABLE process?
	swtch(&p->context, mycpu()->scheduler);
	mycpu()->intena = intena;
}

// context switch
// Give up the CPU for one scheduling round.
void yield(void) {
	acquire(&ptable.lock);  //DOC: yieldlock
	myproc()->state = RUNNABLE;
	sched();
	release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void) {
	static int first = 1;
	// Still holding ptable.lock from scheduler.
	release(&ptable.lock);

	if (first) {
		// Some initialization functions must be run in the context
		// of a regular process (e.g., they call sleep), and thus cannot
		// be run from main().
		first = 0;
		iinit(ROOTDEV);
		initlog(ROOTDEV);
	}

	// Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// sleep without lock, but other time always has lock
void sleep(void *chan, struct spinlock *lk) {
	struct proc *p = myproc();

	if (p == 0)
		panic("sleep");

	if (lk == 0)
		panic("sleep without lk");

	// Must acquire ptable.lock in order to
	// change p->state and then call sched.
	// Once we hold ptable.lock, we can be
	// guaranteed that we won't miss any wakeup
	// because wakeup also need to require ptable.lock to wakeup others,
	// so it's okay to release lk.
	if (lk != &ptable.lock) {  //DOC: sleeplock0

		// waiting there
		acquire(&ptable.lock);  //DOC: sleeplock1
		// let the other continue, but the other would waiting at wakeup acquire ptable.lock point
		release(lk);
	}
	// setup channel signal
	p->chan = chan;
	// Go to sleep.
	p->state = SLEEPING;
	// sleep at that time
	sched();

	// after process is waked up, but ptable.lock hasn't release

	// Tidy up.
	// clean channel signal
	p->chan = 0;

	// Reacquire original lock.
	if (lk != &ptable.lock) {  //DOC: sleeplock2
		// let the other continue
		release(&ptable.lock);
		// waiting there
		acquire(lk);
	}
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void wakeup1(void *chan) {
	struct proc *p;

	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		if (p->state == SLEEPING && p->chan == chan)
			p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan) {
	acquire(&ptable.lock);
	wakeup1(chan);
	release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid) {
	struct proc *p;

	acquire(&ptable.lock);
	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
		if (p->pid == pid) {
			p->killed = 1;
			// Wake process from sleep if necessary.
			if (p->state == SLEEPING)
				p->state = RUNNABLE;
			release(&ptable.lock);
			return 0;
		}
	}
	release(&ptable.lock);
	return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void) {
	static char *states[] = { [UNUSED] "unused", [EMBRYO] "embryo", [SLEEPING
			] "sleep ", [RUNNABLE] "runble", [RUNNING] "run   ", [ZOMBIE
			] "zombie" };
	int i;
	struct proc *p;
	char *state;
	uint pc[10];

	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
		if (p->state == UNUSED)
			continue;
		if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
			state = states[p->state];
		else
			state = "???";
		cprintf("%d %s %s", p->pid, state, p->name);
		if (p->state == SLEEPING) {
			getcallerpcs((uint*) p->context->ebp + 2, pc);
			for (i = 0; i < 10 && pc[i] != 0; i++)
				cprintf(" %p", pc[i]);
		}
		cprintf("\n");
	}
}

#define PGSIZE 4096
#define min(a, b) ((a) < (b) ? (a) : (b))
int mywritebuffer(char * buffer, char * src, uint off, uint n) {
	memmove((char *) buffer + off, src, n);
	return n;
}

/**
 * system print function
 */
int myprint(char * buffer, uint addr, uint n) {
	uint start = 0;
	for (; start < n; start += 16) {
		cprintf("%p: ", addr + start);
		cprintf("%p ", *((char*) buffer + start));
		cprintf("%p ", *((char*) buffer + start + 4));
		cprintf("%p ", *((char*) buffer + start + 8));
		cprintf("%p\n", *((char*) buffer + start + 12));
	}
	return 0;
}

/**
 * copy buffer from stack
 */
int mycopybuffer(pde_t *pgdir, char * addr, char * buffer, uint offset,
		uint buffersize) {
	int guardFlag = 0;
	uint i, pa, n;
	pte_t * pte;
	for (i = 0; i < buffersize; i += PGSIZE) {
		if ((pte = walkpgdir(pgdir, addr + i, 0)) == 0) {
			panic("mycopybuffer: fail to walkpgdir");
		}
		pa = PTE_ADDR(*pte);
		if (buffersize - i < PGSIZE) {
			n = buffersize - i;
		} else {
			n = PGSIZE;
		}
		if (guardFlag == 0 && (*pte & PTE_U) == 0) {
			guardFlag++;
			memmove((char*) buffer + buffersize, &i, sizeof(uint));
		}
//		if(guardFlag == 1 && (*pte & PTE_U) != 0){
//			guardFlag++;
//			memmove((char*) buffer + buffersize - sizeof(uint), &i, sizeof(uint));
//		}
		if (mywritebuffer(buffer, P2V(pa), i, n) != n) {
			return -1;
		}
		//myprint(buffer + i, (uint)(*addr + i), n);
	}
	return 0;
}

/**
 * dump memory for gdb
 */
int dump(int pid, char * addr, char * buffer, uint buffersize) {
	struct proc *p;
	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
		if (p->pid == pid) {
			//	uint i = 0x0;
			//	for(i = 0; i < buffersize; i+=PGSIZE){
			//		pte_t * pte;
			//		pte = walkpgdir(p->pgdir, addr + i, 0);
			//		memmove((char*) buffer + i, (char*) P2V(PTE_ADDR(*pte)), PGSIZE);
			//	}
			break;
		}
	}
	if (p->pid != pid) {
		panic("dump: fail to get pid");
	}
	if (mycopybuffer(p->pgdir, addr, buffer, 0x00, buffersize) != 0) {
		panic("dump: fail to copy");
	}

	return 0;
}

int ps() {
	panic("ps exec");
	return 0;
}

// thread
int thread_create(void (*fcn)(void *), void * arg, void * stack) {
	if ((uint) stack == 0) {
		return -1;
	}

	int i, pid;
	struct proc *np;

	struct proc *curproc = myproc();
	// Allocate process.
	if ((np = allocproc()) == 0)
		return -1;

	np->pgdir = curproc->pgdir;
	np->sz = curproc->sz;
	np->parent = curproc;
	*np->tf = *curproc->tf;

	// Mark this proc as a thread
	np->isThread = 1;

	// Clear %eax so that fork returns 0 in the child.
	np->tf->eax = 0;

	// set function
	np->tf->eip = (int) fcn;
	// size of stack
	//        uint stack_size = *(uint*) curproc->tf->ebp - curproc->tf->esp;
	// size above ebp
	//       uint ebp_top = *(uint*) curproc->tf->ebp - curproc->tf->ebp;
	np->tf->esp = (int) stack + 4096;
	np->tf->esp -= 4;
	*((int*) (np->tf->esp)) = (int) arg;
	np->tf->esp -= 4;
	*((int*) (np->tf->esp)) = 0xffffffff;

//        np->tf->ebp = (uint) stack - ebp_top;

	// copy stack to child, (from old esp to new esp)
//        memmove((void*) np->tf->esp, (const void *) curproc->tf->esp, stack_size);

	for (i = 0; i < NOFILE; i++)
		if (curproc->ofile[i])
			np->ofile[i] = filedup(curproc->ofile[i]);
	np->cwd = idup(curproc->cwd);

	safestrcpy(np->name, curproc->name, sizeof(curproc->name));
	pid = np->pid;

	acquire(&ptable.lock);
	np->state = RUNNABLE;
	release(&ptable.lock);
	return pid;
}

int thread_join(void) {
	struct proc *p;
	int havekids, pid;
	struct proc *curproc = myproc();

	acquire(&ptable.lock);
	for (;;) {
		// Scan through table looking for exited children.
		havekids = 0;
		for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
			if (p->isThread == 0 || p->parent != curproc)
				continue;
			havekids = 1;
			if (p->state == ZOMBIE) {
				// Found one.
				pid = p->pid;
				//kfree(p->kstack);
				p->kstack = 0;
				//freevm(p->pgdir);
				p->pid = 0;
				p->parent = 0;
				p->name[0] = 0;
				p->killed = 0;
				p->state = UNUSED;
				release(&ptable.lock);
				return pid;
			}
		}

		// No point waiting if we don't have any children.
		if (!havekids || curproc->killed) {
			release(&ptable.lock);
			return -1;
		}

		// Wait for children to exit.  (See wakeup1 call in proc_exit.)
		sleep(curproc, &ptable.lock);  //DOC: wait-sleep
	}
}

int thread_exit() {
	struct proc *curproc = myproc();
	struct proc *p;
	int fd;

	if (curproc == initproc)
		panic("init exiting");

	// Close all open files.
	for (fd = 0; fd < NOFILE; fd++) {
		if (curproc->ofile[fd]) {
			fileclose(curproc->ofile[fd]);
			curproc->ofile[fd] = 0;
		}
	}

	begin_op();
	iput(curproc->cwd);
	end_op();
	curproc->cwd = 0;

	acquire(&ptable.lock);

	// Parent might be sleeping in wait().
	wakeup1(curproc->parent);

	// Pass abandoned children to init.
	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
		if (p->parent == curproc) {
			p->parent = initproc;
			if (p->state == ZOMBIE)
				wakeup1(initproc);
		}
	}

	// Jump into the scheduler, never to return.
	curproc->state = ZOMBIE;
	sched();
	panic("zombie exit");
}

void kernel_mutex_lock(struct thread_mutex * lk) {
	while (xchg(&lk->lock, 1) != 0)
		yield();
	__sync_synchronize();
	return;
}

void kernel_mutex_unlock(struct thread_mutex * lk) {
	__sync_synchronize();
	asm volatile("movl $0, %0" : "+m" (lk->lock) : );
	return;
}

int mysleep(void * chan, void * lk) {
//	struct thread_mutex * lk;
//	lk = (struct thread_mutex *) lock;

	struct proc *p = myproc();

	if (p == 0)
		panic("sleep");

	if (lk == 0)
		panic("sleep without lk");

	// Must acquire ptable.lock in order to
	// change p->state and then call sched.
	// Once we hold ptable.lock, we can be
	// guaranteed that we won't miss any wakeup
	// (wakeup runs with ptable.lock locked),
	// so it's okay to release lk.
	if (lk != &ptable.lock) {  //DOC: sleeplock0
		acquire(&ptable.lock);  //DOC: sleeplock1
		kernel_mutex_unlock((struct thread_mutex *) lk);
	}
	// Go to sleep.
	p->chan = chan;
	p->state = SLEEPING;

	sched();

	// Tidy up.
	p->chan = 0;

	// Reacquire original lock.
	if (lk != &ptable.lock) {  //DOC: sleeplock2
		release(&ptable.lock);
		kernel_mutex_lock((struct thread_mutex *) lk);
	}
	return 0;
}

// sleep analogous pthread_cond_wait
// Wake up all processes sleeping on chan.
int mywakeup(void * chan) {
	acquire(&ptable.lock);
	struct proc *p;
	for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
		if (p->state == SLEEPING && p->chan == chan)
			p->state = RUNNABLE;
	release(&ptable.lock);
	return 0;
}

