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

	// Set its mode, size, state, and queue size
	new_future->size = size;
	new_future->mode = mode;
	new_future->state = FUTURE_EMPTY;
	new_future->max_elems = nelems;

	// Allocate `size` amount of space for the data (array of size `nelems`)
	new_future->data = getmem(size * nelems);
	if (new_future->data == (char *)SYSERR) {
		return (future_t *)SYSERR;
	}
	
	// If the future is in shared or queue mode, get a "get" queue
	if (mode == FUTURE_SHARED || mode == FUTURE_QUEUE) {
		qid16 q = newqueue();
		if (q == (qid16)SYSERR) {
			return (future_t *)SYSERR;
		}
		new_future->get_queue = q;
	}
	// For FUTURE_QUEUE, we also need a "set" queue and to intialize queue-related vars.
	if (mode == FUTURE_QUEUE) {
		qid16 q = newqueue();
		if (q == (qid16)SYSERR) {
			return (future_t *)SYSERR;
		}
		new_future->set_queue = q;
		
		new_future->count = 0;
		new_future->head = 0;
		new_future->tail = 0;
	}
	
	return new_future;
}

syscall future_free(future_t* f) {
	intmask mask;
	mask = disable();

	syscall status = OK;
	if (f->mode == FUTURE_EXCLUSIVE && f->state == FUTURE_WAITING) {
		if (kill(f->pid) == SYSERR) {
			status = SYSERR;
		}
	}
	else if (f->mode == FUTURE_SHARED || f->mode == FUTURE_QUEUE) {
		// Kill all waiting processes in the "get" queue, then free the queue
		pid32 pid;
		while (!(isempty(f->get_queue))) {
			pid = dequeue(f->get_queue);
			if (pid == SYSERR) {
				status = SYSERR;
				break;
			}
			if (kill(pid) == SYSERR) {
				status = SYSERR;
			}
		}
		if (delqueue(f->get_queue) == SYSERR) {
			status = SYSERR;
		}
	}
	if(f->mode == FUTURE_QUEUE) {
		// Kill waiting processes in "set" queue and free the queue
		pid32 pid;
		while (!(isempty(f->set_queue))) {
			pid = dequeue(f->set_queue);
			if (pid == SYSERR) {
				status = SYSERR;
				break;
			}
			if (kill(pid) == SYSERR) {
				status = SYSERR;
			}
		}
		if (delqueue(f->get_queue) == SYSERR) {
			status = SYSERR;
		}
	}
	if (freemem(f->data, f->size * f->max_elems) == SYSERR) {
		status = SYSERR;
	}
	if (freemem((char *) f, sizeof(future_t)) == SYSERR) {
		status = SYSERR;
	}
	
	restore(mask);
	return status;
}

syscall future_get(future_t* f, char* out) {
	intmask mask = disable();
	if (f->mode == FUTURE_EXCLUSIVE) {
		if (f->state == FUTURE_EMPTY) {
			// Need to block until the data is ready.
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

			restore(mask);
			return OK;
		}
		else if (f->state == FUTURE_WAITING) {
			restore(mask);
			return SYSERR;
		}
	}
	else if (f->mode == FUTURE_SHARED) {
		if (f->state == FUTURE_READY) {
			// Give back data into location provided by `out`
			memcpy((void *) out, (void *) f->data, f->size);
			restore(mask);
			return OK;
		}
		else {
			struct procent *proc_ptr = &proctab[currpid];

			// Enqueue this processes's PID and update the status to WAITING,
			// then block until the data is written
			if (enqueue(currpid, f->get_queue) == SYSERR) {
				restore(mask);
				return SYSERR;
			}
			f->state = FUTURE_WAITING;
			proc_ptr->prstate = PR_FWAIT;
			resched();

			// Copy data out
			memcpy((void *) out, (void *) f->data, f->size);
			restore(mask);
			return OK;
		}
	}
	else if (f->mode == FUTURE_QUEUE) {
		if (f->count <= 0) {	
			// Queue is empty. Need to wait.
			struct procent *proc_ptr = &proctab[currpid];
			if (enqueue(currpid, f->get_queue) == SYSERR) {
				restore(mask);
				return SYSERR;
			}
			proc_ptr->prstate = PR_FWAIT;
			resched();
		}
		// Copy data out
		memcpy((void *) out, (void *)(f->data + (f->tail * f->size)), f->size);
		(f->count)--;
		f->tail = (f->tail + 1) % f->max_elems;

		// Wake up a waiting writer, if any
		if (!isempty(f->set_queue)) {
			pid32 pid = dequeue(f->set_queue);
			if (pid == SYSERR || isbadpid(pid)) {
				restore(mask);
				return SYSERR;
			}
			ready(pid);
		}
		restore(mask);
		return OK;
	}
	restore(mask);
	return SYSERR;
}

syscall future_set(future_t* f, char* in) {
	intmask mask = disable();

	if (f->mode == FUTURE_EXCLUSIVE || f->mode == FUTURE_SHARED) {
		if (f->state == FUTURE_READY) {
			restore(mask);
			return SYSERR;
		}
		else if (f->state == FUTURE_EMPTY) {
			memcpy((void *) f->data, (void *) in, f->size);
			f->state = FUTURE_READY;
			restore(mask);
			return OK;
		}
		else if (f->state == FUTURE_WAITING) {
			// Write data, then "Wake up" waiting process[es]
			memcpy((void *) f->data, (void *) in, f->size);
			f->state = FUTURE_READY;

			if (f->mode == FUTURE_EXCLUSIVE) {
				if (isbadpid(f->pid)) {
					restore(mask);
					return SYSERR;
				}
				ready(f->pid);
			}
			else if (f->mode == FUTURE_SHARED) {
				pid32 pid;
				while (!(isempty(f->get_queue))) {
					pid = dequeue(f->get_queue);
					if (pid == SYSERR || isbadpid(pid)) {
						restore(mask);
						return(SYSERR);
					}
					ready(pid);
				}
			}
			restore(mask);
			return OK;
		}
	}
	else if (f->mode == FUTURE_QUEUE) {
		// If there's no space to write, need to wait
		if (data_queue_full(f)) {
			struct procent *proc_ptr = &proctab[currpid];
			if (enqueue(currpid, f->set_queue) == SYSERR) {
				restore(mask);
				return SYSERR;
			}
			proc_ptr->prstate = PR_FWAIT;
			resched();
		}

		// Write the data at the head and update relevant queue info
		memcpy((void *) f->data + (f->head * f->size), (void *) in, f->size);
		f->count++;
		f->head = (f->head + 1) % f->max_elems;

		// If anyone is waiting, wake the first one up.
		if (!isempty(f->get_queue)) {
			pid32 pid = dequeue(f->get_queue);
			if (pid == SYSERR || isbadpid(pid)) {
				restore(mask);
				return SYSERR;
			}
			ready(pid);
		}
		restore(mask);
		return OK;
	}
	restore(mask);
	return SYSERR;
}
