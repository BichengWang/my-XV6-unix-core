#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

/**
 * exec:
 * 1.write code and data from path by inode into elf format in memory
 * 2.create user stack
 * 3.setup process to executes
 */
int exec(char *path, char **argv) {
	char *s, *last;
	int i, off;
	uint argc, sz, sp, ustack[3 + MAXARG + 1];
	struct elfhdr elf;
	struct inode *ip;
	struct proghdr ph;
	pde_t *pgdir, *oldpgdir;
	struct proc *curproc = myproc();

	// write inode file into elf
	// start a transaction
	begin_op();

	// get inode by path
	if ((ip = namei(path)) == 0) {
		end_op();
		cprintf("exec: fail\n");
		return -1;
	}
	ilock(ip);
	pgdir = 0;

	// Check ELF header
	if (readi(ip, (char*) &elf, 0, sizeof(elf)) != sizeof(elf))
		goto bad;
	if (elf.magic != ELF_MAGIC)
		goto bad;

	// allocate a format pgdir
	if ((pgdir = setupkvm()) == 0)
		goto bad;

	// Load program data into memory.
	sz = 0;
	for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
		if (readi(ip, (char*) &ph, off, sizeof(ph)) != sizeof(ph))
			goto bad;
		if (ph.type != ELF_PROG_LOAD)
			continue;
		if (ph.memsz < ph.filesz)
			goto bad;
		if (ph.vaddr + ph.memsz < ph.vaddr)
			goto bad;
		// growth process's size
		if ((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
			goto bad;
		if (ph.vaddr % PGSIZE != 0)
			goto bad;
		if (loaduvm(pgdir, (char*) ph.vaddr, ip, ph.off, ph.filesz) < 0)
			goto bad;
	}
	// unlock and recycle inode
	iunlockput(ip);

	// submit transaction
	end_op();

	ip = 0;

	// Allocate two align pages at the next page boundary.
	// Make the first inaccessible.  Use the second as the user stack.
	sz = PGROUNDUP(sz);
	if ((sz = allocuvm(pgdir, sz, sz + 2 * PGSIZE)) == 0)
		goto bad;

	// setup guard page
	// now the sz at the end of process
	// mark the first page inaccessible, it need to - 2 PGSIZE
	clearpteu(pgdir, (char*) (sz - 2 * PGSIZE));

	// stack point to sz
	sp = sz;

	// Push argument strings into user stack, prepare rest of stack in ustack.
	for (argc = 0; argv[argc]; argc++) {
		if (argc >= MAXARG)
			goto bad;
		sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
		if (copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
			goto bad;
		ustack[3 + argc] = sp;
	}
	ustack[3 + argc] = 0;

	ustack[0] = 0xffffffff;  // fake return PC, because no one use this return
	ustack[1] = argc; // local variable 1 in stack
	ustack[2] = sp - (argc + 1) * 4;  // argv pointer: local variable 2 in stack

	sp -= (3 + argc + 1) * 4;
	if (copyout(pgdir, sp, ustack, (3 + argc + 1) * 4) < 0)
		goto bad;

	// Save program name for debugging.
	for (last = s = path; *s; s++)
		if (*s == '/')
			last = s + 1;
	safestrcpy(curproc->name, last, sizeof(curproc->name));

	// Commit to the user image.
	// give the old pgdir to local
	oldpgdir = curproc->pgdir;
	// load new pgdir made by above steps into current process
	// the new pgdir is a page belong to old pgdir
	curproc->pgdir = pgdir;
	curproc->sz = sz;
	curproc->tf->eip = elf.entry;  // main
	curproc->tf->esp = sp;

	// attention: crucial step-- switchuvm()
	switchuvm(curproc);

	// don't forget recycle memory
	freevm(oldpgdir);
	return 0;

	bad: if (pgdir)
		freevm(pgdir);
	if (ip) {
		iunlockput(ip);
		end_op();
	}
	return -1;
}
