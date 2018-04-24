struct stat;
struct rtcdate;

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(char*, int);
int mknod(char*, short, short);
int unlink(char*);
int fstat(int fd, struct stat*);
int link(char*, char*);
int mkdir(char*);
int chdir(char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int dump(int, char *, char *, uint);
int ps(void);
int thread_create(void (*)(void*), void *, void *);
int thread_join(void);
int thread_exit(void);
int mysleep(void*, void*);
int mywakeup(void*);

// ulib.c
int stat(char*, struct stat*);
char* strcpy(char*, char*);
void *memmove(void*, void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, char*, ...);
char* gets(char*, int max);
uint strlen(char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);

// thread.c
//struct thread_spinlock{
//        volatile uint lock;
//        char *name;
//};
//struct thread_mutex{
//        volatile uint lock;
//        char *name;
//};

//void thread_spin_init(struct thread_spinlock *lk);
//void thread_spin_lock(struct thread_spinlock *lk);
//void thread_spin_unlock(struct thread_spinlock *lk);
//void thread_mutex_init(struct thread_mutex *m);
//void thread_mutex_lock(struct thread_mutex *m);
//void thread_mutex_unlock(struct thread_mutex *m);
// extracredit1.c
//struct thread_cond{
//	volatile uint cond;
//	char *name;
//};
