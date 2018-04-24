#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers(void);
static void mpmain(void) __attribute__((noreturn));
extern pde_t *kpgdir;
extern char end[]; // first address after kernel loaded from ELF file

// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
int main(void) {
	// after boot, the first thing is we need memory
	// so we need to first construct the page table malloc() and free() mechanism
	// donate from 0x801126fc to 0x80400000 memory to the page allocate linked list
	kinit1(end, P2V(4 * 1024 * 1024));	//phys page allocator
	// using the former page to setup kernel page table
	kvmalloc();      // kernel page table
	// after above, we can already make new process by exec

	mpinit();        // detect other processors init
	lapicinit();     // interrupt controller init
	seginit();       // segment descriptors init
	picinit();       // disable pic
	ioapicinit();    // another interrupt controller
	consoleinit();   // console hardware
	uartinit();      // serial port
	pinit();         // process table

	// initial IDT
	tvinit();        // trap vectors init
	binit();         // buffer cache
	fileinit();      // file table
	ideinit();       // disk

	// other process boots
	startothers();   // start other processors
	kinit2(P2V(4 * 1024 * 1024), P2V(PHYSTOP)); // must come after startothers()
	userinit(); // like a fork to initial first user process, and set it as runnable
	mpmain(); // go into CPU scheduler process and finish this processor's setup, to run first user process
}

// Other CPUs jump here from entryother.S.
static void mpenter(void) {
	// load first normal 4 kb kernel page table into cr_3
	// xv6 share this pgdir across CPUs
	switchkvm();
	// init GDT entries
	seginit();
	// init interrupt controller
	lapicinit();
	mpmain();
}

// Common CPU setup code.
static void mpmain(void) {
	cprintf("cpu%d: starting %d\n", cpuid(), cpuid());
	idtinit();       // load idt register
	xchg(&(mycpu()->started), 1); // tell startothers() we're up
	scheduler();     // start running processes
}

pde_t entrypgdir[];  // For entry.S

// Start the non-boot (AP) processors.
static void startothers(void) {
	extern uchar _binary_entryother_start[], _binary_entryother_size[];
	uchar *code;
	struct cpu *c;
	char *stack;

	// same as first CPU boot, copy code from 0x7000
	// Write entry code to unused memory at 0x7000.
	// The linker has placed the image of entryother.S in
	// _binary_entryother_start.
	code = P2V(0x7000);
	memmove(code, _binary_entryother_start, (uint) _binary_entryother_size);

	for (c = cpus; c < cpus + ncpu; c++) {
		if (c == mycpu())  // We've started already.
			continue;

		// allocate one stack for one CPU running
		// Tell entryother.S what stack to use, where to enter, and what
		// pgdir to use. We cannot use kpgdir yet, because the AP processor
		// is running in low  memory, so we use entrypgdir for the APs too.
		stack = kalloc();
		*(void**) (code - 4) = stack + KSTACKSIZE;
		*(void**) (code - 8) = mpenter;
		*(int**) (code - 12) = (void *) V2P(entrypgdir);

		// execute code
		lapicstartap(c->apicid, V2P(code));

		// wait for cpu finishing mpmain()
		while (c->started == 0)
			;
	}
}

// The boot page table used in entry.S and entryother.S.
// Page directories (and page tables) must start on page boundaries,
// hence the __aligned__ attribute.
// PTE_PS in a page directory entry enables 4Mbyte pages.
// the first page table is there, this array define 1024 entries for one page table
__attribute__((__aligned__(PGSIZE)))
     pde_t entrypgdir[NPDENTRIES] = {
// Map VA's [0, 4MB) to PA's [0, 4MB)
		// entry 0 map 0 physical address
		[0] = (0) | PTE_P | PTE_W | PTE_PS,
		// entry 1 map 0 physical address
		// Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
		// TODO: ?? ","
		[KERNBASE >> PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS, };

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

