/**************************************************************************
 * Filename: stream_proc.c						  																	*
 * Authors: Zachary E Graber (zegraber@iu.edu), CSCI-P 436 Staff          *
 * Purpose: Sets up the producer and consumer processes for A7/A8         *
 * Date: 3/17/2022                                                        *
 **************************************************************************/
#include "tscdf.h"
#include <stream.h>
#include <string.h>
#include <stdlib.h>
#include <run.h>

void stream_consumer(int32 id, struct stream *str);
struct stream* streams;
struct tscdf** tscdf_arr; // An array of pointers to tscdf structs
int work_queue_depth, output_time;
int32 sync_port;

int stream_proc(int nargs, char* args[]) {
	ulong secs, msecs, time;
	secs = clktime;
	msecs = clkticks;

  // Parse input
	int num_streams = 0;
	int time_window = 0;

	char usage[] = "Usage: run tscdf -s <num_streams> -w <work_queue_depth> -t <time_window> -o <output_time>\n";

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

  // Create streams:
	//
	//     Streams are created on heap space to avoid implicitly sharing
	// data between processes' stacks
	streams = (struct stream *) getmem(sizeof(struct stream) * num_streams);

	// Each stream needs a tscdf
	tscdf_arr = (struct tscdf **) getmem(sizeof(struct tscdf *) * num_streams);
	for (i = 0; i < num_streams; i++) {
		tscdf_arr[i] = tscdf_init(time_window);
	}

	sync_port = ptcreate(num_streams);

  // Create consumer processes and initialize streams
  // Use `i` as the stream id.
	char process_name[18];
  for (i = 0; i < num_streams; i++) {
		streams[i].spaces = semcreate(work_queue_depth);
		streams[i].items = semcreate(0);
		streams[i].mutex = semcreate(1);
		streams[i].head = 0;
		streams[i].tail = 0;
		streams[i].queue = (de *) getmem(sizeof(de) * work_queue_depth);

		sprintf(process_name, "stream_consumer_%d", i);
		resume(create((void *) stream_consumer, 4096, 20, process_name, 2, i, &(streams[i])));
  }

	
  // Parse input header file data and populate work queue
	int st, ts, v;
	char* a;
	for (i = 0; i < n_input; i++) {
		a = (char *) stream_input[i];
		st = atoi(a);
		while (*a++ != '\t');
		ts = atoi(a);
		while (*a++ != '\t');
		v = atoi(a);

		wait(streams[st].spaces); // Wait for an empty space to write to
		wait(streams[st].mutex); // Grab the stream's mutex

		((streams[st].queue)[streams[st].head]).time = ts;
		((streams[st].queue)[streams[st].head]).value = v;
		streams[st].head = (streams[st].head + 1) % work_queue_depth; // Increment the head

		signal(streams[st].mutex); // Release the mutex
		signal(streams[st].items); // Signal that there is another item available for consumption
	}

  // Join all launched consumer processes
	for (i = 0; i < num_streams; i++) {
		// We would expect to receie `num_streams` messages from the port
		kprintf("process %d exited\n", ptrecv(sync_port));
	}

  // Measure the time of this entire function and report it at the end
	time = (((clktime * 1000) + clkticks) - ((secs * 1000) + msecs));
	printf("time in ms: %u\n", time);

	// Free the queues in the streams (since they're on heap space) and the semaphores and the tscdfs
	for (i = 0; i < num_streams; i++) {
		semdelete(streams[i].spaces);
		semdelete(streams[i].items);
		semdelete(streams[i].mutex);
		freemem((char *) streams[i].queue, sizeof(de) * work_queue_depth);
		tscdf_free(tscdf_arr[i]);
	}
	// Free the streams and tscdf ptrs from the heap
	freemem((char *) streams, sizeof(struct stream) * num_streams);
	freemem((char *) tscdf_arr, sizeof(struct tscdf *) * num_streams);
	signal(run_command_done);
  return OK;
}

void stream_consumer(int32 id, struct stream *str) {

  //  Print the current id and pid
	kprintf("stream_consumer id:%d (pid:%d)\n", id, currpid);

	// Sanity check to make sure pointer to stream is valid
	if (isbadsem(str->items) || isbadsem(str->spaces) || isbadsem(str->mutex)) panic("BAD STREAM (BAD SEM)");

  // Consume all values from the work queue of the corresponding stream
	int timestamp, value;
	int count = 0;
	int32* qarray;
	while(1) {
		count++;

		wait(str->items); // Wait for an item to read
		wait(str->mutex); // grab the stream's mutex

		timestamp = ((str->queue)[str->tail]).time; // read from the tail
		value = ((str->queue)[str->tail]).value;
		str->tail = (str->tail + 1) % work_queue_depth;

		signal(str->mutex); // Release the mutex
		signal(str->spaces); // Signal that a value has been consumed (so another space is free)

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
	kprintf("stream_consumer exiting\n");
	ptsend(sync_port, (umsg32) currpid);
}
