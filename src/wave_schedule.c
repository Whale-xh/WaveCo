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

void wave_coroutine_sched_sleepdown(wave_coroutine *co, uint64_t msecs)
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