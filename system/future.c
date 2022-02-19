/*******************************************************
 *						       *
 * 	future.c: provides interface for XINU futures  *
 * 	CSCI-P 436 Assignment 5                        *
 * 	zegraber@iu.edu (Zachary E. Graber)            *
 *						       *
 *******************************************************/
#include <xinu.h>
#include <future.h>

future_t* future_alloc(future_mode_t mode, uint size, uint nelems) {
	// Allocate space for a future
	future_t* new_future = (future_t *) getmem(sizeof(future_t));
	if (new_future == (future_t *)SYSERR) {
		return (future_t *)SYSERR;
	}

	// Set its mode, size, and state
	new_future->size = size;
	new_future->mode = mode;
	new_future->state = FUTURE_EMPTY;

	// Allocate `size` amount of space for the data
	new_future->data = getmem(size);
	if (new_future->data == (char *)SYSERR) {
		return (future_t *)SYSERR;
	}
	return new_future;
}

syscall future_free(future_t* f) {
	syscall status = OK;
	if (f->state == FUTURE_WAITING) {
		if (kill(f->pid) == SYSERR) {
			status = SYSERR;
		}
	}
	if (freemem(f->data, f->size) == SYSERR) {
		status = SYSERR;
	}
	if (freemem((char *) f, sizeof(future_t)) == SYSERR) {
		status = SYSERR;
	}
	return status;
}

syscall future_get(future_t* f, char* out) {
	if (f->state == FUTURE_EMPTY) {
		// Need to block until the data is ready.

		// Disable interrupts
		intmask mask = disable();
		struct procent *proc_ptr = &proctab[currpid];

		// Save the PID in the future struct and set the state to WAITING
		f->pid = currpid;
		f->state = FUTURE_WAITING;

		proc_ptr->prstate = PR_FWAIT;
		resched();

		// Copy the data
		memcpy((void *) out, (void *) f->data, f->size);
		f->state = FUTURE_EMPTY;

		restore(mask);
		return OK;
	}
	else if (f->state == FUTURE_READY) {
		// Give back data into location provided by `out`
		memcpy((void *) out, (void *) f->data, f->size);
		f->state = FUTURE_EMPTY;
		return OK;
	}
	else if (f->state == FUTURE_WAITING) {
		// TODO: non-exclusive logic
		if (f->mode == FUTURE_EXCLUSIVE) {
			return SYSERR;
		}
	}
	return SYSERR;
}

syscall future_set(future_t* f, char* in) {
	// TODO: non-exclusive mode logic
	if (f->state == FUTURE_READY) {
		return SYSERR;
	}
	else if (f->state == FUTURE_EMPTY) {
		memcpy((void *) f->data, (void *) in, f->size);
		f->state = FUTURE_READY;
		return OK;
	}
	else if (f->state == FUTURE_WAITING) {
		intmask mask = disable();

		// Write data, then "Wake up" waiting process
		memcpy((void *) f->data, (void *) in, f->size);
		f->state = FUTURE_READY;

		if (isbadpid(f->pid)) {
			restore(mask);
			return SYSERR;
		}
		ready(f->pid);

		restore(mask);
		return OK;
	}
	return SYSERR;
}
