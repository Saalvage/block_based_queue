#ifndef LCRQ_H
#define LCRQ_H

#include <stdint.h>
#include "align.h"
//#include "hzdptr.h"

/* New imports */
#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include "common.h"

#include "lock_if.h"
#include "ssmem.h"
#include "utils.h"
/* End of new imports */
#ifdef RELAXATION_TIMER_ANALYSIS
#include "relaxation_analysis_timestamps.h"
#elif RELAXATION_ANALYSIS
#error "Lock-based relaxation measurements not supported for LCRQ"
#endif

/*External definitions*/
extern __thread ssmem_allocator_t* alloc;
extern __thread int thread_id;

extern __thread unsigned long my_put_cas_fail_count;
extern __thread unsigned long my_get_cas_fail_count;
extern __thread unsigned long my_null_count;
extern __thread unsigned long my_hop_count;
extern __thread unsigned long my_slide_count;

//#define EMPTY ((void *) -1)

#ifndef LCRQ_RING_SIZE
#define LCRQ_RING_SIZE (1ull << 12)
#endif

typedef struct RingNode {
  volatile uint64_t val;
  volatile uint64_t idx;
} RingNode;

typedef struct PaddedRingNode {
  RingNode ring_node;
  // uint64_t pad[14];
} PaddedRingNode;

typedef ALIGNED(CACHE_LINE_SIZE) struct RingQueue {
  volatile int64_t head CACHE_ALIGNED;
  volatile int64_t tail CACHE_ALIGNED;
  struct RingQueue *next CACHE_ALIGNED;
  //New field
  int64_t items_enqueued;
  PaddedRingNode array[LCRQ_RING_SIZE];
} RingQueue;

typedef CACHE_ALIGNED struct {
  RingQueue * volatile head;
  RingQueue * volatile tail;
  //int nprocs;
} queue_t;

typedef struct {
  RingQueue * next;
  //hzdptr_t hzdptr;
} handle_t;

#endif /* end of include guard: LCRQ_H */
