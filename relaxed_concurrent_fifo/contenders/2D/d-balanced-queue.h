#ifndef D_BALANCED_QUEUE_H
#define D_BALANCED_QUEUE_H

#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include "common.h"

#include "lock_if.h"
#include "ssmem.h"
#include "utils.h"

// Include specific partial queue
#include "partial-queue.h"

 /* ################################################################### *
	* Definition of macros: per data structure
* ################################################################### */

#define DS_ADD(s,k,v)       enqueue(s,k,v)
#define DS_REMOVE(s)        dequeue(s)
#define DS_SIZE(s)          queue_size(s)
#define DS_NEW(w,s,i)		create_queue(w,s,i)
#define DS_REGISTER(q,i)	d_balanced_register(q,i)

#define DS_HANDLE 			dbco_queue*
#define DS_TYPE             dbco_queue
#define DS_NODE             sval_t

#define DCBO_CANDIDATES 2

typedef ALIGNED(CACHE_LINE_SIZE) struct
{
	PARTIAL_T *queues;
	uint32_t width;
	uint32_t enqueue_stick;
	uint32_t dequeue_stick;
	uint32_t max_stick;
	uint32_t enqueue_candidates[DCBO_CANDIDATES];
	uint32_t dequeue_candidates[DCBO_CANDIDATES];
	uint8_t padding[CACHE_LINE_SIZE - (sizeof(PARTIAL_T*)) - (4 + 2 * DCBO_CANDIDATES)*sizeof(int32_t)];
} dbco_queue;

/*Global variables*/


/*Thread local variables*/
extern __thread ssmem_allocator_t* alloc_dcbo;
extern __thread int thread_id;

extern __thread unsigned long my_put_cas_fail_count;
extern __thread unsigned long my_get_cas_fail_count;
extern __thread unsigned long my_null_count;
extern __thread unsigned long my_hop_count;
extern __thread unsigned long my_slide_count;

/* Interfaces */
int enqueue_dcbo(dbco_queue *set, skey_t key, sval_t val);
sval_t dequeue_dcbo(dbco_queue *set);
dbco_queue* create_queue_dcbo(uint32_t n_partial, uint32_t s, int nbr_threads);
size_t queue_size_dcbo(dbco_queue *set);
uint32_t random_index_dcbo(dbco_queue *set);
sval_t double_collect(dbco_queue *set, uint32_t start_index);
dbco_queue* d_balanced_register(dbco_queue *set, int thread_id);
void d_balanced_free();

#endif