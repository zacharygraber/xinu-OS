/* xsh_prodcons.c - xsh_prodcons */

#include <xinu.h>
#include <stdio.h>
#include <prodcons.h>
#include <stdlib.h>
#include <string.h>
#include <run.h>

int n = 0;
sid32 can_read;
sid32 can_write;
sid32 done_consuming;

/*
 *  xsh_prodcons - Creates "producers" and "consumers" to demonstrate the prod-con problem (no mutex)
 */
shellcmd xsh_prodcons(int nargs, char *args[]) {
	// Argument verifications and validations
	int count = 200;    // local varible to hold count

	// check args[1], if present assign value to count
	if (nargs == 2) {
		count = atoi(args[1]);
	}
	if (nargs > 2 || ((count == 0) && (strncmp(args[1], "0", 2) != 0))) {
		fprintf(stderr, "Syntax: run prodcons [counter]\n");
		return(1);
	}

	can_read = semcreate(0);
	can_write = semcreate(1);
	done_consuming = semcreate(0);

	// create the process producer and consumer and put them in ready queue.
	// Look at the definations of function create and resume in the system folder for reference.
	resume(create(producer, 1024, 20, "producer", 1, count));
	resume(create(consumer, 1024, 20, "consumer", 1, count));

	wait(done_consuming);
	semdelete(can_read);
	semdelete(can_write);
	semdelete(done_consuming);
	signal(run_command_done);
	return (0);
}
