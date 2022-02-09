#include <xinu.h>
#include <prodcons.h>
#include <prodcons_bb.h>
#include <stdio.h>

void consumer(int count) {
  // - Iterates from 0 to count (count including)
  //   - reading the value of the global variable 'n' each time
  //   - print consumed value (the value of 'n'), e.g. "consumed : 8"
  int i;
  for (i = 0; i <= count; i++) {
    wait(can_read);
    printf("consumed : %d\n", n);
    signal(can_write);
  }
  signal(done_consuming);
}

void consumer_bb(int id, int count) {
	int i;
	for (i = 0; i < count; i++) {
		wait(full_slots); // Wait for a full slot to read from
		wait(rw_mutex); // Grab the mutex

		/* BEGIN CRITICAL SECTION */
		printf("name : consumer_%d, read : %d\n", id, arr_q[tail]);
		tail = (tail + 1) % BUFFER_SIZE;
		/* END CRITICAL SECTION */

		signal(rw_mutex);
		signal(empty_slots); // Signal saying that there's an empty slot to write to
	}
}
