/**************************************************************************
 * Filename: stream_proc_futures.			  																	*
 * Authors: Zachary E Graber (zegraber@iu.edu), CSCI-P 436 Staff          *
 * Purpose: Sets up the producer and consumer processes for    A8         *
 * Date: 3/30/2022                                                        *
 **************************************************************************/
#include "tscdf.h"
#include <stream.h>
#include <future.h>
#include <string.h>
#include <stdlib.h>
#include <run.h>

void stream_consumer_future(int32 id, future_t* f);
static struct tscdf** tscdf_arr; // An array of pointers to tscdf structs
static int work_queue_depth, output_time;
static int32 sync_port;

int stream_proc_futures(int nargs, char* args[]) {
	ulong secs, msecs, time;
	secs = clktime;
	msecs = clkticks;

  // Parse input
	int num_streams = 0;
	int time_window = 0;

	char usage[] = "run tscdf_fq -s <num_streams> -w <work_queue_depth> -t <time_window> -o <output_time>\n";

	int i;
	char *ch, c;
	if (nargs != 9) {
		printf("%s", usage);
		signal(run_command_done);
		return SYSERR;
	} else {
		i = nargs - 1;
		while (i > 0) {
			ch = args[i - 1];
			c = *(++ch);

			switch (c) {
				case 's':
					num_streams = atoi(args[i]);
					break;

				case 'w':
					work_queue_depth = atoi(args[i]);
					break;

				case 't':
					time_window = atoi(args[i]);
					break;

				case 'o':
					output_time = atoi(args[i]);
					break;

				default:
					printf("%s", usage);
					signal(run_command_done);
					return SYSERR;
			}

			i -= 2;
		}
	}	

  // Create futures and store pointers in the array "futures" 
	future_t* futures[num_streams];

	// Each stream/future needs a tscdf
	tscdf_arr = (struct tscdf **) getmem(sizeof(struct tscdf *) * num_streams);
	for (i = 0; i < num_streams; i++) {
		tscdf_arr[i] = tscdf_init(time_window);
	}

	sync_port = ptcreate(num_streams);

  // Create consumer processes and initialize futures
  // Use `i` as the stream id.
	char process_name[99]; // Just make this big to avoid buffer overflow
  for (i = 0; i < num_streams; i++) {
		futures[i] = future_alloc(FUTURE_QUEUE, sizeof(de), work_queue_depth);

		sprintf(process_name, "future_consumer_%d", i);
		resume(create((void *) stream_consumer_future, 4096, 20, process_name, 2, i, futures[i]));
  }

	
  // Parse input header file data and populate work queue
	int st, ts, v;
	char* a;
	de* write_in = (de *) getmem(sizeof(de));
	for (i = 0; i < n_input; i++) {
		a = (char *) stream_input[i];
		st = atoi(a);
		while (*a++ != '\t');
		ts = atoi(a);
		while (*a++ != '\t');
		v = atoi(a);

		// Need to pass a pointer to copy into the future
		write_in->time = ts;
		write_in->value = v;

		if (future_set(futures[st], (char *)write_in) == SYSERR) { // write_in cast to char* (see future.h)
			kprintf("ERROR: failed writing value (%d, %d) to stream/future %d\n", ts, v, st);
		}
	}
	freemem((char*)write_in, sizeof(de));

  // Join all launched consumer processes
	for (i = 0; i < num_streams; i++) {
		// We would expect to receive `num_streams` messages from the port
		kprintf("process %d exited\n", ptrecv(sync_port));
	}

  // Measure the time of this entire function and report it at the end
	time = (((clktime * 1000) + clkticks) - ((secs * 1000) + msecs));
	printf("time in ms: %u\n", time);

	// Free the futures and tscdfs
	for (i = 0; i < num_streams; i++) {
		future_free(futures[i]);
		tscdf_free(tscdf_arr[i]);
	}
	// Free the tscdf ptrs from the heap
	freemem((char *) tscdf_arr, sizeof(struct tscdf *) * num_streams);
	signal(run_command_done);
  return OK;
}

void stream_consumer_future(int32 id, future_t* f) {

  //  Print the current id and pid
	kprintf("stream_consumer_future id:%d (pid:%d)\n", id, currpid);

  // Consume all values from the future until we receive a (0,0) timestamp/value pair
	int timestamp, value;
	int count = 0;
	int32* qarray;
	de* copy_out = (de *) getmem(sizeof(de));
	while(1) {
		count++;

		// Read the data_element from the future
		if ((future_get(f, (char*)copy_out)) == SYSERR) {
			kprintf("ERROR: failed getting value in consumer w/ id %d", id);
		}
		// copy_out should now be populated with the data.
		timestamp = copy_out->time;
		value = copy_out->value;

		if (timestamp == 0 && value == 0) {
			break;
		}

		tscdf_update(tscdf_arr[id], timestamp, value);
		if (count == output_time) {
			qarray = tscdf_quartiles(tscdf_arr[id]);

			if (qarray == NULL) {
  			kprintf("tscdf_quartiles returned NULL\n");
  			continue;
			}

			kprintf("s%d: %d %d %d %d %d\n", id, qarray[0], qarray[1], qarray[2], qarray[3], qarray[4]);
			freemem((char *) qarray, (6*sizeof(int32)));
			count = 0;
		}
	}
	freemem((char *)copy_out, sizeof(de));
	kprintf("stream_consumer_future exiting\n");
	ptsend(sync_port, (umsg32) currpid);
}
