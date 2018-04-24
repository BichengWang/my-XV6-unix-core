#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
	int n;
	int block[LOGSIZE]; // contains max block number per commit
};

struct log {
	struct spinlock lock;
	int start; // block number of first log start
	int size;
	// how many level of FS sys calls are executing.
	// every begin_op++, end_op--, once equals to 0 submit operation
	int outstanding;
	int committing;  // in commit(), please wait.
	int dev;
	struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

void initlog(int dev) {
	if (sizeof(struct logheader) >= BSIZE)
		panic("initlog: too big logheader");

	struct superblock sb;
	initlock(&log.lock, "log");
	readsb(dev, &sb);
	log.start = sb.logstart;
	log.size = sb.nlog;
	log.dev = dev;
	recover_from_log();
}

// Copy committed blocks from log to their home location
// copy block from log into device data
static void install_trans(void) {
	int tail;

	for (tail = 0; tail < log.lh.n; tail++) {
		struct buf *lbuf = bread(log.dev, log.start + tail + 1); // read log block, read the log block memory
		struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst, read the truly block memory
		memmove(dbuf->data, lbuf->data, BSIZE);  // copy truly block data to truly dst data
		bwrite(dbuf);  // write dst to disk
		brelse(lbuf);
		brelse(dbuf);
	}
}

// Read the log header from disk into the in-memory log header
static void read_head(void) {
	struct buf *buf = bread(log.dev, log.start);
	struct logheader *lh = (struct logheader *) (buf->data);
	int i;
	log.lh.n = lh->n;
	for (i = 0; i < log.lh.n; i++) {
		log.lh.block[i] = lh->block[i];
	}
	brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
// synchronize disk log with memory log
// let it know which block already write into log disk by write_log()
static void write_head(void) {
	// read the device's log start block
	struct buf *buf = bread(log.dev, log.start);
	// parsing device's log start block as log header block
	struct logheader *hb = (struct logheader *) (buf->data); // this is header block from device log
	int i;
	// write current log.lh.n into header block n
	hb->n = log.lh.n;
	for (i = 0; i < log.lh.n; i++) {
		// write block data pointer array into device log block
		hb->block[i] = log.lh.block[i]; // remember log block number array into header block number array
	}
	bwrite(buf); // write header block into disk
	brelse(buf); // release header block
}

static void recover_from_log(void) {
	read_head();
	install_trans(); // if committed, copy from log to disk
	log.lh.n = 0;
	write_head(); // clear the log
}

// called at the start of each FS system call.
void begin_op(void) {
	acquire(&log.lock);
	while (1) {
		// if current log is during committing, wait for finished
		if (log.committing) {
			sleep(&log, &log.lock);
		} else if (log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE) {
			// log block space not enough
			// this op might exhaust log space; wait for commit.
			sleep(&log, &log.lock);
		} else {
			// the only one exit
			// counts that number of calls;
			// the increment both reserves space and prevents a commit
			// from occuring during this system call.
			log.outstanding += 1;
			release(&log.lock);
			break;
		}
	}
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void end_op(void) {
	int do_commit = 0;

	acquire(&log.lock);

	log.outstanding -= 1; // operation times--

	if (log.committing)
		panic("log.committing");

	// check if it is the last level of end.
	if (log.outstanding == 0) {
		do_commit = 1;
		log.committing = 1;
	} else {
		// wake up other thread waiting for log, like begin_op()
		// attention: the outstanding--
		// begin_op() may be waiting for some log space,
		// and decrementing log.outstanding has decreased
		// the amount of reserved space.
		wakeup(&log);
	}
	// once release lock, the begin_op() can continue execute
	release(&log.lock);

	// check if it needs to commit
	if (do_commit) {
		// call commit w/o holding locks, since not allowed
		// to sleep with locks.
		commit();

		// once commit total log, can wake up others again
		// like begin_op() sleep at log.committing
		acquire(&log.lock);
		log.committing = 0;
		wakeup(&log);
		release(&log.lock);
	}
}

// Copy modified blocks from cache to log.
// wtfuck function name
// actually write buffer data into log block
static void write_log(void) {
	int tail;

	// write all data from cache into log block
	for (tail = 0; tail < log.lh.n; tail++) {
		struct buf *to = bread(log.dev, log.start + tail + 1); // target log block
		struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
		memmove(to->data, from->data, BSIZE);
		bwrite(to);  // write the cache data into log block
		brelse(from);
		brelse(to);
	}
}

static void commit() {
	if (log.lh.n > 0) {
		// wtfuck name of write_log and log_write
		// write buffer into log disk
		write_log();     // Write modified blocks' data from cache to log block
		// update log header infomation
		write_head();    // Write header to disk -- the real commit, this is writing the cache buffer into log block
		// write log disk data into file data
		install_trans(); // Now install writes to home locations, this is writing the log block into disk block
		// clear log.lh.n info
		log.lh.n = 0;    // write log.lh.n = 0 for erase
		// synchronize to log disk
		write_head();    // Erase the transaction from the log
	}
}

// this function is using for write buffer by starting log journey.
// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache with B_DIRTY.
// commit()/write_log() will do the disk write.
//
//  attention: log_write() replaces bwrite() during log; a typical use is:
//   bp = bread(...);
//   modify bp->data[]
//   log_write(bp);
//   brelse(bp);
// log_write is not actually write buffer into log, but only let log header remember the number of buffer
void log_write(struct buf *b) {
	int i;

	// log.size is current log size, log.lh.n is the max contain block of this log
	if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
		panic("too big a transaction");
	if (log.outstanding < 1)
		panic("log_write outside of trans");

	acquire(&log.lock);

	// wtfuck code

	// check if b->block is already in log.lh
	for (i = 0; i < log.lh.n; i++) {
		if (log.lh.block[i] == b->blockno)   // log absorbtion
			break;
	}

	// if yes, b->blockno is the one, if not, b->blockno is the last.
	// add b->blockno into log header
	log.lh.block[i] = b->blockno;

	// check if yes.
	if (i == log.lh.n)
		log.lh.n++;

	// block is dirty but haven't submit, this is the reason of dirty read, xv6 prevent this.
	b->flags |= B_DIRTY; // prevent eviction
	release(&log.lock);
}

