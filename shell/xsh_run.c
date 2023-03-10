/* xsh_run.c - xsh_run */

#include <xinu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shprototypes.h>
#include <run.h>
#include <prodcons_bb.h>
#include <stdbool.h>
#include <future_prodcons.h>
#include <stream.h>

void print_list();
void prodcons_bb(int nargs, char *args[]);
sid32 run_command_done, empty_slots, full_slots, rw_mutex;
int head, tail;
int arr_q[BUFFER_SIZE];

/*
 *  xsh_run - Takes an argument that will map to a function and then run it
 */
shellcmd xsh_run(int nargs, char *args[]) {
	if ((nargs == 1) || (strncmp(args[1], "list", 4) == 0)) {
		print_list();
		return(0);
	}

	args++;
	nargs--;

	// Create a mutex (semaphore) that starts locked, then have the 
	// other process unlock it when it finishes to 'join' them
	run_command_done = semcreate(0);

	if (strncmp(args[0], "hello", 5) == 0) {
		resume(create((void *) xsh_hello, 4096, 20, "hello", 2, nargs, args));
	}
	else if (strncmp(args[0], "prodcons", 11) == 0) {
		resume(create((void *) xsh_prodcons, 4096, 20, "prodcons", 2, nargs, args));
	}
	else if (strncmp(args[0], "prodcons_bb", 11) == 0) {
		prodcons_bb(nargs, args);
	}
	else if (strncmp(args[0], "futest", 6) == 0) {
		future_prodcons(nargs, args);
	}
	else if (strncmp(args[0], "tscdf", 5) == 0) {
		if (strncmp(args[0], "tscdf_fq", 8) == 0) {
			resume(create((void *) stream_proc_futures, 4096, 20, "stream_proc_futures", 2, nargs, args));
		}
		else {
			resume(create((void *) stream_proc, 4096, 20, "stream_proc", 2, nargs, args));
		}
	}
	else if (strncmp(args[0], "fstest", 6) == 0) {
		fstest(nargs, args);
		signal(run_command_done);
	}
	else {
		print_list();
		return(1);
	}
	
	wait(run_command_done);
	semdelete(run_command_done);
	return(0);
}

void print_list() {
	printf("fstest\n");
	printf("futest\n");
	printf("hello\n");
	printf("list\n");
	printf("prodcons\n");
	printf("prodcons_bb\n");
	printf("tscdf\n");
	printf("tscdf_fq\n");
}

void prodcons_bb(int nargs, char *args[]) {
	// Get rid of the argument that's just the command name
	args++;
	nargs--;

	// Validate args
	if (nargs != 4) {
		fprintf(stderr, "Syntax: run prodcons_bb <# of producer processes> <# of consumer processes> <# of iterations the producer runs> <# of iterations the consumer runs>\n");
		signal(run_command_done);
		return;
	}

	// Good args, continue
	int num_prods = atoi(args[0]);
	int num_cons = atoi(args[1]);
	int prods_iter_cnt = atoi(args[2]);
	int cons_iter_cnt = atoi(args[3]);
	if ((num_prods * prods_iter_cnt) != (num_cons * cons_iter_cnt)) {
		fprintf(stderr, "Iteration Mismatch Error: the number of producer(s) iteration does not match the consumer(s) iteration\n");
		signal(run_command_done);
		return;
	}

	// Create semaphores and shared variables
	empty_slots = semcreate(BUFFER_SIZE);
	full_slots = semcreate(0);
	rw_mutex = semcreate(1);
	head = 0;
	tail = 0;

	// Spawn Processes. Keep an array of the PIDS so we can check when they're done
	pid32 consumers[num_cons];
	int i;
	for (i = 0; i < num_cons; i++) {
		consumers[i] = create((void *) consumer_bb, 4096, 20, "consumer_bb", 2, i, cons_iter_cnt);
		resume(consumers[i]);
	}
	for (i = 0; i < num_prods; i++) {
		resume(create((void *) producer_bb, 4096, 20, "producer_bb", 2, i, prods_iter_cnt));
	}

	bool done = false;
	while (!done) {
		done = true;
		for (i = 0; i < BUFFER_SIZE; i++) {
			if (!(isbadpid(consumers[i]))) {
				done = false;
			}
		}
	}

	// Clean up semaphores
	semdelete(empty_slots);
	semdelete(full_slots);
	semdelete(rw_mutex);

	signal(run_command_done);
	return;
}

void future_prodcons(int nargs, char *args[]) {
  print_sem = semcreate(1);
	const char* USAGE_STR = "Syntax: run futest [-pc [g ...] [s VALUE ...]] | [-pcq LENGTH [g ...] [s VALUE ...]] | [-f NUMBER] | [--free]";
  if (nargs < 2) {
  	printf("%s\n", USAGE_STR);
		signal(run_command_done);
		return;
  }
  if (strncmp(args[1], "--free", 6) == 0) {
		if (nargs > 2) {
			// Extra args
			printf("%s\n", USAGE_STR);
			signal(run_command_done);
			return;
		}
  	future_free_test(nargs, args);
  }
  else if (strncmp(args[1], "-f", 2) == 0) {
		future_fib(nargs, args);
  }
  else if (strncmp(args[1], "-pc", 3) == 0) {
		// PC and PCQ
		future_t* f;
		future_mode_t f_mode = (strncmp(args[1], "-pcq", 4) == 0) ? FUTURE_QUEUE : FUTURE_EXCLUSIVE;
		if (f_mode == FUTURE_QUEUE) {
			// Queue mode... make sure user provided queue size
			int q_size;
			if (nargs < 4 || ((q_size = atoi(args[2])) == 0)) {
				printf("%s\n", USAGE_STR);
				signal(run_command_done);
				return;
			}
			else {
				// Args are good... make the future
				f = future_alloc(FUTURE_QUEUE, sizeof(int), q_size);
			}
		}
		else {
			// Exclusive mode
			f = future_alloc(FUTURE_EXCLUSIVE, sizeof(int), 1);
		}
		char *val;

		if (nargs < 3) {
			// No arguments given
			printf("%s\n", USAGE_STR);
			future_free(f);
			signal(run_command_done);
			return;
		}
		int i = (f_mode == FUTURE_QUEUE) ? 3 : 2;
		int s_count = 0;
		int other_count = 0;
		while (i < nargs) {
			if (strncmp(args[i], "g", 1) == 0) {
				i++;
				continue;
	    }
			else if (strncmp(args[i], "s", 1) == 0) {
				// Every 's' needs to have a value with it
				s_count++;
				i++;
				continue;
	    }
	    else {
				// Input is neither 'g' nor 's'
				// Either follows an 's' or is an error
				if (strncmp(args[i-1], "s", 1) == 0) {
					other_count++;
					i++;
					continue;
				}
				else {
					printf("%s\n", USAGE_STR);
					future_free(f);
					signal(run_command_done);
					return;
				}
	    }
		}
		if (s_count != other_count) {
			printf("%s\n", USAGE_STR);
			future_free(f);
			signal(run_command_done);
			return;
		}

		int num_args = i;  // keeping number of args to create the array
		i = (f_mode == FUTURE_QUEUE) ? 3 : 2; // reseting the index
		val  =  (char *) getmem(num_args); // initializing the array to keep the "s" numbers

		// Iterate again through the arguments and create the following processes based on the passed argument ("g" or "s VALUE")
		while (i < nargs) {
			if (strcmp(args[i], "g") == 0) {
				char id[10];
				sprintf(id, "fcons%d",i);
				resume(create(future_cons, 2048, 20, id, 1, f));
			}
			if (strcmp(args[i], "s") == 0) {
				i++;
	      uint8 number = atoi(args[i]);
	      val[i] = number;
	      resume(create(future_prod, 2048, 20, "fprod1", 2, f, &val[i]));
	      sleepms(5);
			}
			i++;
		}
		sleepms(100);
		future_free(f);
  }
  else {
  	// Unsupported flag
	printf("%s\n", USAGE_STR);
  } 
  signal(run_command_done);
  return;
}
