/* xsh_run.c - xsh_run */

#include <xinu.h>
#include <stdio.h>
#include <string.h>
#include <shprototypes.h>

void print_list();

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

	if (strncmp(args[0], "hello", 5) == 0) {
		resume(create((void *) xsh_hello, 4096, 20, "hello", 2, nargs, args));
	}
	else if (strncmp(args[0], "prodcons", 8) == 0) {
		resume(create((void *) xsh_prodcons, 4096, 20, "prodcons", 2, nargs, args));
	}
	else {
		print_list();
		return(1);
	}
	return(0);
}

void print_list() {
	printf("hello\n");
	printf("list\n");
	printf("prodcons\n");
}
