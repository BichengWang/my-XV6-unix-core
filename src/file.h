// the unnamed file
struct file {
	// is this file a inode, or pipe, or none
	enum {
		FD_NONE, FD_PIPE, FD_INODE
	} type;
	int ref; // reference count
	char readable;
	char writable;
	struct pipe *pipe;
	struct inode *ip; // inode pointer
	uint off; // current offset of inode, represent last read progress
};

// in-memory copy of an inode
struct inode {
	uint dev;           // Device number
	uint inum;          // Inode number
	int ref;            // Reference count
	struct sleeplock lock; // protects everything below here
	int valid;          // inode has been read from disk?

	short type;         // copy of disk inode
	short major;
	short minor;
	short nlink;
	uint size;
	uint addrs[NDIRECT + 1];
};

// table mapping major device number to
// device functions
struct devsw {
	int (*read)(struct inode*, char*, int);
	int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
