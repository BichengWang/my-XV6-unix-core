#include "types.h"
#include "stat.h"
#include "user.h"
#include "lockFunc.h"

/**************** my thread ****************/
#define READABLE 127

struct q {
   struct thread_cond cv;
   struct thread_mutex ml;
   void *ptr;
   uint num;
};

// Initialize
/**
 * condition variable
 */
void thread_cond_init(struct q *q) {
	q->cv.cond = READABLE;
	q->cv.name = "readable";
	thread_mutex_init(&q->ml);
}

void thread_cond_signal(struct thread_cond * cv) {
	mywakeup( (void*) &cv->cond);
}

void thread_cond_wait(struct thread_cond *cv, struct thread_mutex * ml) {
	mysleep( (void*) &cv->cond, (void*) ml);
}

/**
 * semm
 */
void sem_init(struct q * q, uint num){
	q->ptr = 0;
	q->num = num;
	thread_cond_init(q);
}

void sem_wait(struct q * semm) {
	thread_mutex_lock(&(semm->ml));
	while (semm->num == 0)
		thread_cond_wait(&(semm->cv), &(semm->ml));
	semm->num--;
	thread_mutex_unlock(&(semm->ml));
}

void sem_post(struct q * semm){
	thread_mutex_lock(&(semm->ml));
	/*fuck! Here is a stupid mistake!Just add the num will be ok!!
	 Otherwise,we will be blocked here！
	 while ( semm->num == 0)
	 pthread_cond_wait(&(semm->cond), &(semm->lock));
	 */
	semm->num++;
	thread_mutex_unlock(&(semm->ml));
	thread_cond_signal(&(semm->cv));
}

// Thread 1 (sender)
void* send(struct q *q, void *p)
{
   thread_mutex_lock(&q->ml);
   while(q->ptr != 0)
      ;
   q->ptr = p;
   // wake up
   thread_cond_signal(&q->cv);
   thread_mutex_unlock(&q->ml);
   return p;
}

// Thread 2 (receiver)

void* recv(struct q *q)
{
  void *p;

  thread_mutex_lock(&q->ml);

  while((p = q->ptr) == 0)
    thread_cond_wait(&q->cv, &q->ml);
  q->ptr = 0;

  thread_mutex_unlock(&q->ml);
  return p;
}

char * buffer;
struct q messageQ;

/**************** test code ****************/
struct balance {
	char name[32];
	int amount;
};

volatile int total_balance = 0;

volatile unsigned int delay(unsigned int d) {
	unsigned int i;
	for (i = 0; i < d; i++) {
		__asm volatile( "nop" ::: );
	}

	return i;
}

void write_work(void *arg) {
	char * newMessage = "Hello, Multi-thread world!";
	struct balance *b = (struct balance*) arg;
	printf(1, "I send message: %s\n", newMessage);
	printf(1, "send message by s:%x\n", b->name);
	send(&messageQ, newMessage);
	thread_exit();
	return;
}

void read_work(void *arg) {
	char * receiveMessage = recv(&messageQ);
	struct balance *b = (struct balance*) arg;
	printf(1, "receive message by s:%x\n", b->name);
	printf(1, "I receive message: %s\n", receiveMessage);
	thread_exit();
	return;
}

/**
 * condition variable test
 */
int cond_var_test(){
	printf(1, "test condition variable:\n");

	thread_cond_init(&messageQ);
	buffer = "hello world";
	struct balance b1 = { "b1", 300 };
	struct balance b2 = { "b2", 300 };
	void *s1, *s2;
	int t1, t2, r1, r2;
	s1 = malloc(4096);
	s2 = malloc(4096);
	t1 = thread_create(write_work, (void*) &b1, s1);
	t2 = thread_create(read_work, (void*) &b2, s2);
	r1 = thread_join();
	r2 = thread_join();
	printf(1, "Threads finished: (%d):%d, (%d):%d, shared balance:%d\n", t1, r1,
			t2, r2, total_balance);
	free(s1);
	free(s2);
	return 0;
}

/**
 * semm test
 */
#define NUM 128
#define MAX_NUM 20
int queue[NUM];
struct q producer;
struct q consumer;
struct thread_mutex printLock;
void prod_work(void *arg) {
	int p = 0;
	int count = 0;
	while (1) {
		if(count > MAX_NUM)
			break;
		sem_wait(&consumer);
		thread_mutex_lock(&printLock);
		count++;
		queue[p] = count;
		printf(1, "Produce %d\n", queue[p]);
		thread_mutex_unlock(&printLock);
		sem_post(&producer);
		p = (p + 1) % NUM;
		sleep(1);
	}


	thread_exit();
	return;
}

void cons_work(void *arg) {
	int c = 0;
	int count = 0;
	while (1) {
		if(count > MAX_NUM)
			break;
		sem_wait(&producer);
		thread_mutex_lock(&printLock);
		printf(1, "Consume %d\n", queue[c]);
		count++;
		queue[c] = 0;
		thread_mutex_unlock(&printLock);
		sem_post(&consumer);
		c = (c + 1) % NUM;
		sleep(3);
	}
	thread_exit();
	return;
}

int semm_test(){
	printf(1, "test semm:\n");
	thread_mutex_init(&printLock);
	sem_init(&consumer, NUM);
	sem_init(&producer, 0);
	void *s1, *s2;
	int t1, t2, r1, r2;
	struct balance b1 = { "b1", 300 };
	struct balance b2 = { "b2", 300 };
	s1 = malloc(4096);
	s2 = malloc(4096);
	t1 = thread_create(prod_work, (void*) &b1, s1);
	t2 = thread_create(cons_work, (void*) &b2, s2);
	r1 = thread_join();
	r2 = thread_join();
	printf(1, "Threads finished: (%d):%d, (%d):%d, shared balance:%d\n", t1, r1,
			t2, r2, total_balance);
	free(s1);
	free(s2);
	return 0;
}

int main(int argc, char *argv[]) {
	cond_var_test();
	semm_test();
	exit();
}
