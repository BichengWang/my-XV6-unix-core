
struct thread_spinlock{
        volatile uint lock;
        char *name;
};

struct thread_mutex{
        volatile uint lock;
        char *name;
};

struct thread_cond{
	volatile uint cond;
	char *name;
};

