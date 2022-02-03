/* xsh_hello.c - xsh_hello */

#include <xinu.h>
#include <stdio.h>
#include <run.h>

/*
 *  xsh_hello - Print formatted text "Hello <input>, Welcome to the world of Xinu!!"
 */
shellcmd xsh_hello(int nargs, char *args[]) {
	if (nargs == 2) {
		printf("Hello %s, Welcome to the world of Xinu!!\n", args[1]);
		signal(run_command_done);
		return 0;
	}
	else {
		fprintf(stderr, "Syntax: run hello name\n");
		signal(run_command_done);
		return 1;
	}
}
