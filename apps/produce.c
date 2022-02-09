#include <xinu.h>
#include <prodcons.h>
#include <stdio.h>
#include <prodcons_bb.h>

void producer(int count) {
  // - Iterates from 0 to count (count including)
  //   - setting the value of the global variable 'n' each time
  //   - print produced value (new value of 'n'), e.g.: "produced : 8"
  int i;
  for (i = 0; i <= count; i++) {
    wait(can_write);
    n = i;
    printf("produced : %d\n", n);
    signal(can_read);
  }
}

void producer_bb(int id, int count) {
	int i;
	for (i = 0; i < count; i++) {
		wait(empty_slots); // Wait for an empty slot to be available
		wait(rw_mutex); // Grab the mutex

		/* BEGIN CRITICAL SECTION */
		arr_q[head] = i;
		head = (head + 1) % BUFFER_SIZE;
		printf("name : producer_%d, write : %d\n", id, i);
		/* END CRITICAL SECTION */

		signal(rw_mutex); // Release the mutex
		signal(full_slots); // Signal that there is a slot ready to be read from
	}
}
