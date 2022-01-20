#include <xinu.h>
#include <prodcons.h>
#include <stdio.h>

void producer(int count) {
  // - Iterates from 0 to count (count including)
  //   - setting the value of the global variable 'n' each time
  //   - print produced value (new value of 'n'), e.g.: "produced : 8"
  int i;
  for (i = 0; i <= count; i++) {
    n = i;
    printf("produced: %d\n", n);  
  }
}
