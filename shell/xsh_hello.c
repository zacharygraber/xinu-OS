/* xsh_hello.c - xsh_hello */

#include <xinu.h>
#include <stdio.h>

/*
 *  xsh_hello - Print formatted text "Hello <input>, Welcome to the world of Xinu!!"
 */
shellcmd xsh_hello(int nargs, char *args[]) {
	if (nargs == 2) {
		printf("Hello %s, Welcome to the world of Xinu!!\n", args[1]);
		return 0;
	}
	else if (nargs < 2) {
		fprintf(stderr, "%s: too few arguments\n", args[0]);
		return 1;
	}
	else {
		fprintf(stderr, "%s: too many arguments\n", args[0]);
		return 1;
	}
}
