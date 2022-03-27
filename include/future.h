/*******************************************************
 *						       *
 * 	future.h: provides interface for XINU futures  *
 * 	CSCI-P 436 Assignment 5                        *
 * 	zegraber@iu.edu (Zachary E. Graber)            *
 *						       *
 *******************************************************/
#include <xinu.h>

// Quick macro to check if a future's data queue is full (with a pointer 'f')
#define data_queue_full(f) (f->count >= f->max_elems)

typedef enum {
	FUTURE_EMPTY,
	FUTURE_WAITING,
	FUTURE_READY
} future_state_t;

typedef enum {
	FUTURE_EXCLUSIVE,
	FUTURE_SHARED,
	FUTURE_QUEUE
} future_mode_t;

typedef struct future_t {
	char *data;
	uint size;
	future_state_t state;
	future_mode_t mode;
	qid16 get_queue;
	pid32 pid;
	qid16 set_queue;
	uint16 max_elems;
	uint16 count;
	uint16 head;
	uint16 tail;
} future_t;

future_t* future_alloc(future_mode_t mode, uint size, uint nelems);
syscall future_free(future_t* f);

syscall future_get(future_t* f, char* out);
syscall future_set(future_t* f, char* in);

int future_fib(int nargs, char *args[]);
int future_free_test(int nargs, char *args[]);
