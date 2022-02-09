/* xsh_run.c - xsh_run */

#include <xinu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shprototypes.h>
#include <run.h>
#include <prodcons_bb.h>
#include <stdbool.h>

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
	else {
		print_list();
		return(1);
	}
	
	wait(run_command_done);
	semdelete(run_command_done);
	return(0);
}

void print_list() {
	printf("hello\n");
	printf("list\n");
	printf("prodcons\n");
	printf("prodcons_bb\n");
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
		fprintf(stderr, "Syntax: run prodcons_bb <# of producer processes> <# of consumer processes> <# of iterations the producer runs> <# of iterations the consumer runs>\n");
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
