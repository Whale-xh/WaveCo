#ifndef __WAVE_COROUTINE_H__
#define __WAVE_COROUTINE_H__

#include "wave_queue.h"
#include "wave_tree.h"

#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <inttypes.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <errno.h>

#define _USE_UCONTEXT

#ifdef _USE_UCONTEXT
#include <ucontext.h>
#endif

#define WAVE_CO_MAX_EVENTS (1024*1024)
#define WAVE_CO_MAX_STACKSIZE  (128*1024) 

#define BIT(x)  (1 << (x))
#define CLEARBIT(X) ~(1 << (x))
#define CANCEL_FD_WAIT_UINT64   1

typedef void (*proc_coroutine)(void *);

typedef enum{
    WAVE_COROUTINE_STATUS_WAIT_READ,
	WAVE_COROUTINE_STATUS_WAIT_WRITE,
	WAVE_COROUTINE_STATUS_NEW,
	WAVE_COROUTINE_STATUS_READY,
	WAVE_COROUTINE_STATUS_EXITED,
	WAVE_COROUTINE_STATUS_BUSY,
	WAVE_COROUTINE_STATUS_SLEEPING,
	WAVE_COROUTINE_STATUS_EXPIRED,
	WAVE_COROUTINE_STATUS_FDEOF,
	WAVE_COROUTINE_STATUS_DETACH,
	WAVE_COROUTINE_STATUS_CANCELLED,
	WAVE_COROUTINE_STATUS_PENDING_RUNCOMPUTE,
	WAVE_COROUTINE_STATUS_RUNCOMPUTE,
	WAVE_COROUTINE_STATUS_WAIT_IO_READ,
	WAVE_COROUTINE_STATUS_WAIT_IO_WRITE,
	WAVE_COROUTINE_STATUS_WAIT_MULTI
}wave_coroutine_status;

typedef enum{
    WAVE_COROUTINE_COMPUTE_BUSY,
    WAVE_COROUTINE_COMPUTE_FREE
}wave_coroutine_compute_status;

typedef enum{
    WAVE_COROUTINE_EV_READ,
    WAVE_COROUTINE_EV_WRITE
}wave_coroutinue_event;

LIST_HEAD(_wave_coroutine_link,_wave_coroutine);
TAILQ_HEAD(_wave_coroutine_queue,_wave_coroutine);

RB_HEAD(_wave_coroutine_rbtree_sleep,_wave_coroutine);
RB_HEAD(_wave_coroutine_rbtree_wait,_wave_coroutine);

typedef struct _wave_coroutine_link wave_coroutine_link;
typedef struct _wave_coroutine_queue wave_coroutine_queue;
typedef struct _wave_coroutine_rbtree_sleep wave_coroutine_rbtree_sleep;
typedef struct _wave_coroutine_rbtree_wait wave_coroutine_rbtree_wait;

#ifndef _USE_UCONTEXT
typedef struct _wave_cpu_ctx{
    void *esp; 
	void *ebp;
	void *eip;
	void *edi;
	void *esi;
	void *ebx;
	void *r1;
	void *r2;
	void *r3;
	void *r4;
	void *r5;
}wave_cpu_ctx;
#endif

typedef struct _wave_schedule{
    uint64_t birth;
#ifdef _USE_UCONTEXT
    ucontext_t ctx;
#else
    corowave_cpu_ctx ctx;
#endif
    void *stack;
    size_t stack_size;
    int spawned_coroutines;
    uint64_t default_timeout;
    
}wave_schedule;




#endif