/*******************************************************
 *						       *
 * 	future.h: provides interface for XINU futures  *
 * 	CSCI-P 436 Assignment 5                        *
 * 	zegraber@iu.edu (Zachary E. Graber)            *
 *						       *
 *******************************************************/
#include <xinu.h>

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
} future_t;

future_t* future_alloc(future_mode_t mode, uint size, uint nelems);
syscall future_free(future_t* f);

syscall future_get(future_t* f, char* out);
syscall future_set(future_t* f, char* in);

int future_fib(int nargs, char *args[]);
int future_free_test(int nargs, char *args[]);
