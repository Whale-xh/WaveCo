
#include "../include/wave_coroutine.h"
#include <sys/eventfd.h>

int wave_epoller_create()
{
    return epoll_create(1);
}

int wave_epoller_wait(struct timespec t)
{
    wave_schedule *sched = wave_coroutine_get_sched();
    return epoll_wait(sched->poller_fd, sched->eventlist, WAVE_CO_MAX_EVENTS, t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0);
}

int wave_epoller_ev_register_trigger()
{
    wave_schedule *sched = wave_coroutine_get_sched();
    if (!sched->eventfd)
    {
        sched->eventfd = eventfd(0, EFD_NONBLOCK);
        assert(sched->eventfd != -1);
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sched->eventfd;
    int ret = epoll_ctl(sched->poller_fd, EPOLL_CTL_ADD, sched->eventfd, &ev);
    assert(ret != -1);
}