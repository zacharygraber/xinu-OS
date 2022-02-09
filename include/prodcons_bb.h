#define BUFFER_SIZE 5

/* Global variable for producer consumer */
extern int arr_q[BUFFER_SIZE];
extern int head;
extern int tail;

/* Global declaration for semaphores */
extern sid32 empty_slots;
extern sid32 full_slots;
extern sid32 rw_mutex;

/* Function Prototype */
void consumer_bb(int id, int count);
void producer_bb(int id, int count);
