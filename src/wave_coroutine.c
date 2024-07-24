#include "../include/wave_coroutine.h"

pthread_key_t global_sched_key;

static pthread_once_t shced_key_once = PTHREAD_ONCE_INIT;

#ifdef _USE_UCONTEXT

static void _save_stack(wave_coroutine *co)
{
    char *top = co->sched->stack + co->sched->stack_size;
    char dummy = 0;
    assert(top - &dummy <= WAVE_CO_MAX_STACKSIZE);
    if (co->stack_size < top - &dummy)
    {
        co->stack = realloc(co->stack, top - &dummy);
        assert(co->stack != NULL);
    }
    co->stack_size = top - &dummy;
    memcpy(co->stack, &dummy, co->stack_size);
}

static void _load_stack(wave_coroutine *co)
{
    memcpy(co->sched->stack + co->sched->stack_size - co->stack_size, co->stack, co->stack_size);
}

static void _exec(void *lt)
{
    wave_coroutine *co = (wave_coroutine *)lt;
    co->func(co->arg);
    co->status |= (BIT(WAVE_COROUTINE_STATUS_EXITED) | BIT(WAVE_COROUTINE_STATUS_FDEOF) | BIT(WAVE_COROUTINE_STATUS_DETACH));
    wave_coroutine_yield(co);
}

#else

int _switch(wave_cpu_ctx *new_ctx, wave_cpu_ctx *cur_ctx);

#ifdef __i386__
__asm__(
    "    .text                                  \n"
    "    .p2align 2,,3                          \n"
    ".globl _switch                             \n"
    "_switch:                                   \n"
    "__switch:                                  \n"
    "movl 8(%esp), %edx      # fs->%edx         \n"
    "movl %esp, 0(%edx)      # save esp         \n"
    "movl %ebp, 4(%edx)      # save ebp         \n"
    "movl (%esp), %eax       # save eip         \n"
    "movl %eax, 8(%edx)                         \n"
    "movl %ebx, 12(%edx)     # save ebx,esi,edi \n"
    "movl %esi, 16(%edx)                        \n"
    "movl %edi, 20(%edx)                        \n"
    "movl 4(%esp), %edx      # ts->%edx         \n"
    "movl 20(%edx), %edi     # restore ebx,esi,edi      \n"
    "movl 16(%edx), %esi                                \n"
    "movl 12(%edx), %ebx                                \n"
    "movl 0(%edx), %esp      # restore esp              \n"
    "movl 4(%edx), %ebp      # restore ebp              \n"
    "movl 8(%edx), %eax      # restore eip              \n"
    "movl %eax, (%esp)                                  \n"
    "ret                                                \n");
#elif defined(__x86_64__)

__asm__(
    "    .text                                  \n"
    "       .p2align 4,,15                                   \n"
    ".globl _switch                                          \n"
    ".globl __switch                                         \n"
    "_switch:                                                \n"
    "__switch:                                               \n"
    "       movq %rsp, 0(%rsi)      # save stack_pointer     \n"
    "       movq %rbp, 8(%rsi)      # save frame_pointer     \n"
    "       movq (%rsp), %rax       # save insn_pointer      \n"
    "       movq %rax, 16(%rsi)                              \n"
    "       movq %rbx, 24(%rsi)     # save rbx,r12-r15       \n"
    "       movq %r12, 32(%rsi)                              \n"
    "       movq %r13, 40(%rsi)                              \n"
    "       movq %r14, 48(%rsi)                              \n"
    "       movq %r15, 56(%rsi)                              \n"
    "       movq 56(%rdi), %r15                              \n"
    "       movq 48(%rdi), %r14                              \n"
    "       movq 40(%rdi), %r13     # restore rbx,r12-r15    \n"
    "       movq 32(%rdi), %r12                              \n"
    "       movq 24(%rdi), %rbx                              \n"
    "       movq 8(%rdi), %rbp      # restore frame_pointer  \n"
    "       movq 0(%rdi), %rsp      # restore stack_pointer  \n"
    "       movq 16(%rdi), %rax     # restore insn_pointer   \n"
    "       movq %rax, (%rsp)                                \n"
    "       ret                                              \n");

#endif

static void _exec(void *lt)
{
#if defined(__lvm__) && defined(__x86_64__)
    __asm__("movq 16(%%rbp), %[lt]" : [lt] "=r"(lt));
#endif

    wave_coroutine *co = (wave_coroutine *)lt;
    co->func(co->arg);
    co->status |= (BIT(WAVE_COROUTINE_STATUS_EXITED) | BIT(WAVE_COROUTINE_STATUS_FDEOF) | BIT(WAVE_COROUTINE_STATUS_DETACH));
#if 1
    wave_coroutine_yield(co);
#else
    co->ops = 0;
    _switch(&co->sched->ctx, &co->ctx);
#endif
}

static inline void wave_coroutine_madvise(wave_coroutine *co)
{

    size_t current_stack = (co->stack + co->stack_size) - co->ctx.esp;
    assert(current_stack <= co->stack_size);

    if (current_stack < co->last_stack_size &&
        co->last_stack_size > co->sched->page_size)
    {
        size_t tmp = current_stack + (-current_stack & (co->sched->page_size - 1));
        assert(madvise(co->stack, co->stack_size - tmp, MADV_DONTNEED) == 0);
    }
    co->last_stack_size = current_stack;
}

#endif

extern int wave_schedule_create(int stack_size);

void wave_coroutine_free(wave_coroutine *co)
{
    if (co == NULL)
        return;
    co->sched->spawned_coroutines--;
    if (co->stack)
    {
        free(co->stack);
        co->stack = NULL;
    }
    if (co)
    {
        free(co);
    }
}

static void wave_coroutine_init(wave_coroutine *co)
{
#ifdef _USE_UCONTEXT
    getcontext(&co->ctx);
    co->ctx.uc_stack.ss_sp = co->sched->stack;
    co->ctx.uc_stack.ss_size = co->sched->stack_size;
    co->ctx.uc_link = &co->sched->ctx;
    makecontext(&co->ctx, (void (*)(void))_exec, 1, (void *)co);
#else
    void **stack = (void **)(co->stack + co->stack_size);

    stack[-3] = NULL;
    stack[-2] = (void *)co;

    co->ctx.esp = (void *)stack - (4 * sizeof(void *));
    co->ctx.ebp = (void *)stack - (3 * sizeof(void *));
    co->ctx.eip = (void *)_exec;
#endif
    co->status = BIT(WAVE_COROUTINE_STATUS_READY);
}

void wave_coroutine_yield(wave_coroutine *co)
{
    co->ops = 0;
#ifdef _USE_UCONTEXT
    if ((co->status & BIT(WAVE_COROUTINE_STATUS_EXITED)) == 0)
    {
        _save_stack(co);
    }
    swapcontext(&co->ctx, &co->sched->ctx);
#else
    _switch(&co->sched->ctx, &co->ctx);
#endif
}

int wave_coroutine_resum(wave_coroutine *co)
{
    if (co->status & BIT(WAVE_COROUTINE_STATUS_NEW))
    {
        wave_coroutine_init(co);
    }
#ifdef _USE_UCONTEXT
    else
    {
        _load_stack(co);
    }
#endif
    wave_schedule *sched = wave_coroutine_get_sched();
    sched->curr_thread = co;

#ifdef _USE_UCONTEXT
    swapcontext(&sched->ctx, &co->ctx);
#else
    _switch(&co->ctx, &co->sched->ctx);
    wave_coroutine_madvise(co);
#endif
    sched->curr_thread = NULL;

    if (co->status & BIT(WAVE_COROUTINE_STATUS_EXITED))
    {
        if (co->status & BIT(WAVE_COROUTINE_STATUS_DETACH))
        {
            wave_coroutine_free(co);
        }
        return -1;
    }
    return 0;
}

void wave_coroutine_renice(wave_coroutine *co)
{
    co->ops++;
    if (co->ops < 5)
        return;
    TAILQ_INSERT_TAIL(&wave_coroutine_get_sched()->ready, co, ready_next);
    wave_coroutine_yield(co);
}

void wave_coroutine_sleep(uint64_t msecs)
{
    wave_coroutine *co = wave_coroutine_get_sched()->curr_thread;
    if (msecs == 0)
    {
        TAILQ_INSERT_TAIL(&co->sched->ready, co, ready_next);
        wave_coroutine_yield(co);
    }
    else
    {
        wave_schedule_sched_sleepdown(co, msecs);
    }
}

void wave_coroutine_detach()
{
    wave_coroutine *co = wave_coroutine_get_sched()->curr_thread;
    co->status |= BIT(WAVE_COROUTINE_STATUS_DETACH);
}

static void wave_coroutine_sched_key_destructor(void *data)
{
    free(data);
}

static void wave_coroutine_sched_key_creator()
{
    assert(pthread_key_create(&global_sched_key, wave_coroutine_sched_key_destructor) == 0);
    assert(pthread_setspecific(global_sched_key, NULL) == 0);
    return;
}

int wave_coroutine_create(wave_coroutine **new_co, proc_coroutine func, void *arg)
{
    assert(pthread_once(&shced_key_once, wave_coroutine_sched_key_creator) == 0);
    wave_schedule *sched = wave_coroutine_get_sched();

    if (sched == NULL)
    {
        wave_schedule_create(0);
        sched = wave_coroutine_get_sched();
        if (sched == NULL)
        {
            printf("Failed to create scheduler\n");
            return -1;
        }
    }
    wave_coroutine *co = calloc(1, sizeof(wave_coroutine));
    if (co == NULL)
    {
        printf("Failed to allocate memory for new coroutine\n");
        return -2;
    }

#ifdef _USE_UCONTEXT
    co->stack = NULL;
    co->stack_size = 0;
#else
    int ret = posix_memalign(&co->stack, getpagesize(), sched->stack_size);
    if (ret)
    {
        printf("Failed to allocate stack for new coroutine\n");
        free(co);
        return -3;
    }
    co->stack_size = sched->stack_size;
#endif
    co->sched = sched;
    co->status = BIT(WAVE_COROUTINE_STATUS_NEW);
    co->id = sched->spawned_coroutines++;
    co->func = func;

#if CANCEL_FD_WAIT_UINT64
    co->fd = -1;
    co->events = 0;
#else
    co->fd_wait = -1;
#endif
    co->arg = arg;
    co->birth = wave_coroutine_usec_now();
    *new_co = co;
    TAILQ_INSERT_TAIL(&co->sched->ready, co, ready_next);
    return 0;
}