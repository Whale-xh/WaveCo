#ifndef __WAVE_COROUTINE_H__
#define __WAVE_COROUTINE_H__

#include "wave_queue.h"
#include "wave_tree.h"

#define __USE_GNU
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
#include <stddef.h>
#include <arpa/inet.h>


#define _USE_UCONTEXT

#ifdef _USE_UCONTEXT
#include <ucontext.h>
#endif

#define WAVE_CO_MAX_EVENTS (1024 * 1024)
#define WAVE_CO_MAX_STACKSIZE (128 * 1024)

#define BIT(x) (1 << (x))
#define CLEARBIT(x) ~(1 << (x))
#define CANCEL_FD_WAIT_UINT64 1

typedef void (*proc_coroutine)(void *);

typedef enum
{
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
} wave_coroutine_status;

typedef enum
{
	WAVE_COROUTINE_COMPUTE_BUSY,
	WAVE_COROUTINE_COMPUTE_FREE
} wave_coroutine_compute_status;

typedef enum
{
	WAVE_COROUTINE_EV_READ,
	WAVE_COROUTINE_EV_WRITE
} wave_coroutine_event;

LIST_HEAD(_wave_coroutine_link, _wave_coroutine);
TAILQ_HEAD(_wave_coroutine_queue, _wave_coroutine);

RB_HEAD(_wave_coroutine_rbtree_sleep, _wave_coroutine);
RB_HEAD(_wave_coroutine_rbtree_wait, _wave_coroutine);

typedef struct _wave_coroutine_link wave_coroutine_link;
typedef struct _wave_coroutine_queue wave_coroutine_queue;
typedef struct _wave_coroutine_rbtree_sleep wave_coroutine_rbtree_sleep;
typedef struct _wave_coroutine_rbtree_wait wave_coroutine_rbtree_wait;

#ifndef _USE_UCONTEXT
typedef struct _wave_cpu_ctx
{
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
} wave_cpu_ctx;
#endif

typedef struct _wave_schedule
{
	uint64_t birth;
#ifdef _USE_UCONTEXT
	ucontext_t ctx;
#else
	wave_cpu_ctx ctx;
#endif
	void *stack;
	size_t stack_size;
	int spawned_coroutines;
	uint64_t default_timeout;
	struct _wave_coroutine *curr_thread;
	int page_size;
	int poller_fd;
	int eventfd;
	struct epoll_event eventlist[WAVE_CO_MAX_EVENTS];
	int nevents;
	int num_new_events;
	pthread_mutex_t defer_mutex;
	wave_coroutine_queue ready;
	wave_coroutine_queue defer;
	wave_coroutine_link busy;
	wave_coroutine_rbtree_sleep sleeping;
	wave_coroutine_rbtree_wait waiting;
} wave_schedule;

typedef struct _wave_coroutine
{
#ifdef _USE_UCONTEXT
	ucontext_t ctx;
#else
	wave_cpu_ctx ctx;
#endif
	proc_coroutine func;
	void *arg;
	void *data;
	size_t stack_size;
	size_t last_stack_size;
	wave_coroutine_status status;
	wave_schedule *sched;
	uint64_t birth;
	uint64_t id;

#if CANCEL_FD_WAIT_UINT64
	int fd;
	unsigned short events; // POLL_EVENT
#else
	int64_t fd_wait;
#endif
	char funcname[64];
	struct _wave_coroutine *co_join;
	void **co_exit_ptr;
	void **stack;
	void **ebp;
	uint32_t ops;
	uint64_t sleep_usecs;

	RB_ENTRY(_wave_coroutine)
	sleep_node;
	RB_ENTRY(_wave_coroutine)
	wait_node;

	LIST_ENTRY(_wave_coroutine)
	busy_next;

	TAILQ_ENTRY(_wave_coroutine)
	ready_next;
	TAILQ_ENTRY(_wave_coroutine)
	defer_next;
	TAILQ_ENTRY(_wave_coroutine)
	cond_next;

	TAILQ_ENTRY(_wave_coroutine)
	io_next;
	TAILQ_ENTRY(_wave_coroutine)
	compute_next;

	struct
	{
		void *buf;
		size_t nbytes;
		int fd;
		int ret;
		int err;
	} io;

	struct _wave_coroutine_compute_sched *compute_sched;
	int ready_fds;
	struct pollfd *pfds;
	nfds_t nfds;
} wave_coroutine;

typedef struct _wave_coroutine_compute_sched
{
#ifdef _USE_UCONTEXT
	ucontext_t ctx;
#else
	wave_cpu_ctx ctx;
#endif
	wave_coroutine_queue coroutines;
	wave_coroutine *curr_coroutine;
	pthread_mutex_t run_mutex;
	pthread_cond_t run_cond;

	pthread_mutex_t co_mutex;
	LIST_ENTRY(_wave_coroutine_compute_sched)
	compute_next;

	wave_coroutine_compute_status compute_status;

} wave_coroutine_compute_sched;

extern pthread_key_t global_sched_key;

static inline wave_schedule *wave_coroutine_get_sched() { return (wave_schedule *)pthread_getspecific(global_sched_key); }

static inline uint64_t wave_coroutine_diff_usecs(uint64_t t1, uint64_t t2) { return t2 - t1; }

static inline uint64_t wave_coroutine_usec_now()
{
	struct timeval t1 = {0, 0};
	gettimeofday(&t1, NULL);
	return t1.tv_sec * 1000000 + t1.tv_usec;
}

int wave_epoller_create();

void wave_schedule_cancel_event(wave_coroutine *co);
void wave_schedule_sched_event(wave_coroutine *co, int fd, wave_coroutine_event e, uint64_t timeout);

void wave_schedule_desched_sleepdown(wave_coroutine *co);
void wave_schedule_sched_sleepdown(wave_coroutine *co, uint64_t msecs);

wave_coroutine *wave_schedule_desched_wait(int fd);
void wave_schedule_sched_wait(wave_coroutine *co, int fd, unsigned short events, uint64_t timeout);

void wave_schedule_run(void);

int wave_epoller_ev_register_trigger(void);
int wave_epoller_wait(struct timespec t);
int wave_coroutine_resume(wave_coroutine *co);
void wave_coroutine_free(wave_coroutine *co);
int wave_coroutine_create(wave_coroutine **new_co, proc_coroutine func, void *arg);
void wave_coroutine_yield(wave_coroutine *co);

void wave_coroutine_sleep(uint64_t msecs);

int wave_socket(int domain, int type, int protocol);
int wave_accept(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t wave_recv(int fd, void *buf, size_t len, int flags);
ssize_t wave_send(int fd, const void *buf, size_t len, int flags);
int wave_close(int fd);
int wave_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int wave_connect(int fd, struct sockaddr *name, socklen_t namelen);

ssize_t wave_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t wave_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);

#define COROUTINE_HOOK

#ifdef COROUTINE_HOOK

typedef int (*socket_t)(int domain, int type, int protocol);
extern socket_t socket_f;

typedef int (*connect_t)(int, const struct sockaddr *, socklen_t);
extern connect_t connect_f;

typedef ssize_t (*read_t)(int, void *, size_t);
extern read_t read_f;

typedef ssize_t (*recv_t)(int sockfd, void *buf, size_t len, int flags);
extern recv_t recv_f;

typedef ssize_t (*recvfrom_t)(int sockfd, void *buf, size_t len, int flags,
							  struct sockaddr *src_addr, socklen_t *addrlen);
extern recvfrom_t recvfrom_f;

typedef ssize_t (*write_t)(int, const void *, size_t);
extern write_t write_f;

typedef ssize_t (*send_t)(int sockfd, const void *buf, size_t len, int flags);
extern send_t send_f;

typedef ssize_t (*sendto_t)(int sockfd, const void *buf, size_t len, int flags,
							const struct sockaddr *dest_addr, socklen_t addrlen);
extern sendto_t sendto_f;

typedef int (*accept_t)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
extern accept_t accept_f;

// new-syscall
typedef int (*close_t)(int);
extern close_t close_f;

int init_hook();

/*

typedef int(*fcntl_t)(int __fd, int __cmd, ...);
extern fcntl_t fcntl_f;

typedef int (*getsockopt_t)(int sockfd, int level, int optname,
		void *optval, socklen_t *optlen);
extern getsockopt_t getsockopt_f;

typedef int (*setsockopt_t)(int sockfd, int level, int optname,
		const void *optval, socklen_t optlen);
extern setsockopt_t setsockopt_f;

*/

#endif

#endif