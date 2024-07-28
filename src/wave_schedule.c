#include "../include/wave_coroutine.h"

#define FD_KEY(f, e) ((int64_t)f << (sizeof(int32_t) * 8) | e)
#define FD_EVENT(f) ((int32_t)(f))
#define FD_ONLY(f) ((f) >> ((sizeof(int32_t) * 8)))

static inline int wave_coroutine_sleep_cmp(wave_coroutine *co1, wave_coroutine *co2)
{
    if (co1->sleep_usecs < co2->sleep_usecs)
    {
        return -1;
    }
    if (co1->sleep_usecs == co2->sleep_usecs)
    {
        return 0;
    }
    return 1;
}

static inline int wave_coroutine_wait_cmp(wave_coroutine *co1, wave_coroutine *co2)
{
#if CANCEL_FD_WAIT_UINT64
    if (co1->fd < co2->fd)
        return -1;
    if (co1->fd == co2->fd)
        return 0;
#else
    if (co1->fd_wait < co2->fd_wait)
        return -1;
    if (co1->fd_wait == co2->fd_wait)
        return 0;
#endif
    return 1;
}

RB_GENERATE(_wave_coroutine_rbtree_sleep, _wave_coroutine, sleep_node, wave_coroutine_sleep_cmp);
RB_GENERATE(_wave_coroutine_rbtree_wait, _wave_coroutine, wait_node, wave_coroutine_wait_cmp);

void wave_schedule_sched_sleepdown(wave_coroutine *co, uint64_t msecs)
{
    uint64_t usecs = msecs * 1000u;
    wave_coroutine *co_tmp = RB_FIND(_wave_coroutine_rbtree_sleep, &co->sched->sleeping, co);
    if (co_tmp != NULL)
    {
        RB_REMOVE(_wave_coroutine_rbtree_sleep, &co->sched->sleeping, co_tmp);
    }
    co->sleep_usecs = wave_coroutine_diff_usecs(co->sched->birth, wave_coroutine_usec_now()) + usecs;
    while (msecs)
    {
        co_tmp = RB_INSERT(_wave_coroutine_rbtree_sleep, &co->sched->sleeping, co);
        if (co_tmp)
        {
            printf("sleep_usecs %" PRId64 "\n", co->sleep_usecs);
            co->sleep_usecs++;
            continue;
        }
        co->status != BIT(WAVE_COROUTINE_STATUS_SLEEPING);
        break;
    }
}

void wave_schedule_desched_sleepdown(wave_coroutine *co)
{
    if (co->status & BIT(WAVE_COROUTINE_STATUS_SLEEPING))
    {
        RB_INSERT(_wave_coroutine_rbtree_sleep, &co->sched->sleeping, co);
        co->status &= CLEARBIT(WAVE_COROUTINE_STATUS_SLEEPING);
        co->status |= BIT(WAVE_COROUTINE_STATUS_READY);
        co->status &= CLEARBIT(WAVE_COROUTINE_STATUS_EXPIRED);
    }
}

wave_coroutine *wave_schedule_search_wait(int fd)
{
    wave_coroutine find_it = {0};
    find_it.fd = fd;
    wave_schedule *sched = wave_coroutine_get_sched();
    wave_coroutine *co = RB_FIND(_wave_coroutine_rbtree_wait, &sched->waiting, &find_it);
    co->status = 0;
    return co;
}

wave_coroutine *wave_schedule_desched_wait(int fd)
{
    wave_coroutine find_it = {0};
    find_it.fd = fd;
    wave_schedule *sched = wave_coroutine_get_sched();
    wave_coroutine *co = RB_FIND(_wave_coroutine_rbtree_wait, &sched->waiting, &find_it);
    if (co != NULL)
    {
        RB_REMOVE(_wave_coroutine_rbtree_wait, &co->sched->waiting, co);
    }
    co->status = 0;
    wave_schedule_desched_sleepdown(co);
    return co;
}

void wave_schedule_sched_wait(wave_coroutine *co, int fd, unsigned short events, uint64_t timeout)
{
    if (co->status & BIT(WAVE_COROUTINE_STATUS_WAIT_READ) || co->status & BIT(WAVE_COROUTINE_STATUS_WAIT_WRITE))
    {
        printf("Unexpected event. lt id %" PRIu64 " fd %" PRId32 " already in %" PRId32 " state\n", co->id, co->fd, co->status);
        assert(0);
    }
    if (events & POLLIN)
    {
        co->status |= WAVE_COROUTINE_STATUS_WAIT_READ;
    }
    else if (events & POLLOUT)
    {
        co->status |= WAVE_COROUTINE_STATUS_WAIT_WRITE;
    }
    else
    {
        printf("events : %d\n", events);
        assert(0);
    }
    co->fd = fd;
    co->events = events;
    wave_coroutine *co_tmp = RB_INSERT(_wave_coroutine_rbtree_wait, &co->sched->waiting, co);
    assert(co_tmp == NULL);
    if (timeout == 1)
        return;
    wave_schedule_sched_sleepdown(co, timeout);
}

void wave_schedule_cancel_wait(wave_coroutine *co)
{
    RB_REMOVE(_wave_coroutine_rbtree_wait, &co->sched->waiting, co);
}

void wave_schedule_free(wave_schedule *sched)
{
    if (sched->poller_fd > 0)
    {
        close(sched->poller_fd);
    }
    if (sched->eventfd > 0)
    {
        close(sched->eventfd);
    }
    if (sched->stack != NULL)
    {
        free(sched->stack);
    }
    free(sched);
    assert(pthread_setspecific(global_sched_key, NULL) == 0);
}

int wave_schedule_create(int stack_size)
{
    int sched_stack_size = stack_size ? stack_size : WAVE_CO_MAX_STACKSIZE;

    wave_schedule *sched = (wave_schedule *)calloc(1, sizeof(wave_schedule));
    if (sched == NULL)
    {
        printf("Failed to initialize scheduler\n");
        return -1;
    }

    assert(pthread_setspecific(global_sched_key, sched) == 0);
    sched->poller_fd = wave_epoller_create();
    if (sched->poller_fd == -1)
    {
        printf("Failed to initialize epoller\n");
        wave_schedule_free(sched);
        return -2;
    }

    wave_epoller_ev_register_trigger();

    sched->stack_size = sched_stack_size;
    sched->page_size = getpagesize();

#ifdef _USE_UCONTEXT
    int ret = posix_memalign(&sched->stack, sched->page_size, sched->stack_size);
    assert(ret == 0);
#else
    sched->stack = NULL;
    bzero(&sched->ctx, sizeof(wave_cpu_ctx));
#endif
    sched->spawned_coroutines = 0;
    sched->default_timeout = 3000000u;

    RB_INIT(&sched->sleeping);
    RB_INIT(&sched->waiting);

    sched->birth = wave_coroutine_usec_now();
    TAILQ_INIT(&sched->ready);
    TAILQ_INIT(&sched->defer);
    LIST_INIT(&sched->busy);
}

static wave_coroutine *wave_schedule_expired(wave_schedule *sched)
{
    uint64_t t_diff_usecs = wave_coroutine_diff_usecs(sched->birth, wave_coroutine_usec_now());
    wave_coroutine *co = RB_MIN(_wave_coroutine_rbtree_sleep, &sched->sleeping);
    if (co == NULL)
        return NULL;

    if (co->sleep_usecs <= t_diff_usecs)
    {
        RB_REMOVE(_wave_coroutine_rbtree_sleep, &co->sched->sleeping, co);
        return co;
    }
    return NULL;
}

static inline int wave_schedule_isdone(wave_schedule *sched)
{
    return (RB_EMPTY(&sched->waiting) && LIST_EMPTY(&sched->busy) && RB_EMPTY(&sched->sleeping) && TAILQ_EMPTY(&sched->ready));
}

static uint64_t wave_schedule_min_timeout(wave_schedule *sched)
{
    uint64_t t_diff_usecs = wave_coroutine_diff_usecs(sched->birth, wave_coroutine_usec_now());
    uint64_t min = sched->default_timeout;

    wave_coroutine *co = RB_MIN(_wave_coroutine_rbtree_sleep, &sched->sleeping);
    if (!co)
        return min;
    min = co->sleep_usecs;
    if (min > t_diff_usecs)
    {
        return min - t_diff_usecs;
    }
    return 0;
}

static int wave_schedule_epoll(wave_schedule *sched)
{
    sched->num_new_events = 0;
    struct timespec t = {0, 0};
    uint64_t usecs = wave_schedule_min_timeout(sched);
    if (usecs && TAILQ_EMPTY(&sched->ready))
    {
        t.tv_sec = usecs / 1000000u;
        if (t.tv_sec != 0)
        {
            t.tv_nsec = (usecs % 1000u) * 1000u;
        }
        else
        {
            t.tv_nsec = usecs * 1000u;
        }
    }
    else
    {
        return 0;
    }

    int nready = 0;
    while (1)
    {
        nready = wave_epoller_wait(t);
        if (-1 == nready)
        {
            if (errno == EINTR)
                continue;
            else
                assert(0);
        }
        break;
    }
    sched->nevents = 0;
    sched->num_new_events = nready;
    return 0;
}

void wave_schedule_run()
{
    wave_schedule *sched = wave_coroutine_get_sched();
    if (sched == NULL)
        return;
    while (!wave_schedule_isdone(sched))
    {
        // 1.expired ---> sleep rbtree
        wave_coroutine *expired = NULL;
        while ((expired = wave_schedule_expired(sched)) != NULL)
        {
            wave_coroutine_resume(expired);
        }

        // 2.ready queue
        wave_coroutine *last_co_ready = TAILQ_LAST(&sched->ready, _wave_coroutine_queue);
        while (!TAILQ_EMPTY(&sched->ready))
        {
            wave_coroutine *co = TAILQ_FIRST(&sched->ready);
            TAILQ_REMOVE(&co->sched->ready, co, ready_next);

            if (co->status & BIT(WAVE_COROUTINE_STATUS_FDEOF))
            {
                wave_coroutine_free(co);
                break;
            }

            wave_coroutine_resume(co);
            if (co == last_co_ready)
                break;
        }

        // 3.wait rbtree
        wave_schedule_epoll(sched);
        while (sched->num_new_events)
        {
            int idx = --sched->num_new_events;
            struct epoll_event *ev = sched->eventlist + idx;

            int fd = ev->data.fd;
            int is_eof = ev->events & EPOLLHUP;
            if (is_eof)
                errno = ECONNRESET;

            wave_coroutine *co = wave_schedule_search_wait(fd);
            if (co != NULL)
            {
                if (is_eof)
                {
                    co->status |= BIT(WAVE_COROUTINE_STATUS_FDEOF);
                }
                wave_coroutine_resume(co);
            }
            is_eof = 0;
        }
    }
    wave_schedule_free(sched);
    return;
}