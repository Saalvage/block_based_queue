#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"

/*
	*   Author: Kåre von Geijer
	*
	* This program is distributed in the hope that it will be useful,
	* but WITHOUT ANY WARRANTY; without even the implied warranty of
	* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	* GNU General Public License for more details.
	*
*/

#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <inttypes.h>
#include <sys/time.h>
#include <unistd.h>
#include <malloc.h>
#include "utils.h"

#ifdef __sparc__
	#include <sys/types.h>
	#include <sys/processor.h>
	#include <sys/procset.h>
#endif

#if !defined(VALIDATESIZE)
	#define VALIDATESIZE 1
#endif

#include "2Dd-queue_optimized.h"
#define SPECIFIC_TEST_LOOP()	TEST_LOOP_ONLY_UPDATES()

/* ################################################################### *
	* GLOBALS
* ################################################################### */

RETRY_STATS_VARS_GLOBAL;

size_t initial = DEFAULT_INITIAL;
size_t range = DEFAULT_RANGE;
size_t update = 100;
size_t load_factor;
size_t num_threads = DEFAULT_NB_THREADS;
size_t duration = DEFAULT_DURATION;

size_t print_vals_num = 100;
size_t pf_vals_num = 1023;
size_t put, put_explicit = false;
double update_rate, put_rate, get_rate;

size_t size_after = 0;
int seed = 0;
uint32_t rand_max;
#define rand_min 1

static volatile int stop;
uint64_t relaxation_bound = 1;
uint64_t width = 1;
uint64_t depth = 1;
uint8_t k_mode = 0;
size_t side_work = 0;

volatile ticks *putting_succ;
volatile ticks *putting_fail;
volatile ticks *removing_succ;
volatile ticks *removing_fail;
volatile ticks *putting_count;
volatile ticks *putting_count_succ;
volatile unsigned long *put_cas_fail_count;
volatile unsigned long *get_cas_fail_count;
volatile unsigned long *null_count;
volatile unsigned long *hop_count;
volatile unsigned long *slide_count;
volatile ticks *removing_count;
volatile ticks *removing_count_succ;
volatile ticks *total;


/* ################################################################### *
	* LOCALS
* ################################################################### */

#ifdef DEBUG
	extern __thread uint32_t put_num_restarts;
	extern __thread uint32_t put_num_failed_expand;
	extern __thread uint32_t put_num_failed_on_new;
#endif

__thread unsigned long *seeds;
__thread unsigned long my_put_cas_fail_count;
__thread unsigned long my_get_cas_fail_count;
__thread unsigned long my_null_count;
__thread unsigned long my_hop_count;
__thread unsigned long my_slide_count;
__thread int thread_id;

barrier_t barrier, barrier_global;

typedef struct thread_data
{
	uint32_t id;
	DS_TYPE* set;
} thread_data_t;


#pragma GCC diagnostic pop
