#include "arch.h"
#include "drivers.h"
#include "kernel.h"
#include "mm.h"
#include "sched.h"
#include "user.h"

static struct thread threads[MAX_THREADS];
static struct thread *cur;
static struct thread *idle;
static struct runqueue rq;
static volatile int need_resched;
static int next_tid;
static uint64_t nswitch;

void sched_init(void)
{
    rq_init(&rq);
    idle = &threads[0];
    k_memset(idle, 0, sizeof(*idle));
    idle->tid = next_tid++;
    k_strlcpy(idle->name, "idle", sizeof(idle->name));
    idle->state = T_RUNNING;
    idle->prio = PRIO_IDLE;
    idle->slice = SLICE_TICKS;
    idle->user_slot = -1;
    list_init(&idle->rq.node);
    cur = idle;
}

struct thread *current_thread(void)
{
    return cur;
}

struct thread *thread_create(const char *name, thread_fn fn, void *arg, int prio)
{
    struct thread *t = 0;
    uint64_t f;
    int i;
    void *stack;
    if (prio < 0 || prio >= PRIO_IDLE)
        return 0;
    stack = page_alloc(KSTACK_ORDER);
    if (!stack)
        return 0;
    f = irq_save();
    for (i = 1; i < MAX_THREADS; i++)
        if (threads[i].state == T_UNUSED) {
            t = &threads[i];
            break;
        }
    if (!t) {
        irq_restore(f);
        page_free(stack);
        return 0;
    }
    k_memset(t, 0, sizeof(*t));
    t->tid = next_tid++;
    k_strlcpy(t->name, name, sizeof(t->name));
    t->prio = prio;
    t->kstack = stack;
    t->user_slot = -1;
    t->ctx.sp = (uint64_t)(uintptr_t)stack + KSTACK_SIZE;
    t->ctx.lr = (uint64_t)(uintptr_t)thread_trampoline;
    t->ctx.x19 = (uint64_t)(uintptr_t)fn;
    t->ctx.x20 = (uint64_t)(uintptr_t)arg;
    list_init(&t->rq.node);
    t->state = T_RUNNABLE;
    rq_push(&rq, &t->rq, prio);
    if (prio < cur->prio)
        need_resched = 1;
    irq_restore(f);
    return t;
}

void schedule(void)
{
    uint64_t f = irq_save();
    struct thread *prev = cur, *next;
    struct rq_entry *e;

    need_resched = 0;
    if (prev->state == T_RUNNING) {
        prev->state = T_RUNNABLE;
        if (prev != idle) /* idle is never queued; it runs when nothing else can */
            rq_push(&rq, &prev->rq, prev->prio);
    }
    e = rq_pop(&rq);
    next = e ? container_of(e, struct thread, rq) : idle;
    next->state = T_RUNNING;
    next->slice = SLICE_TICKS;
    if (next != prev) {
        nswitch++;
        cur = next;
        cpu_switch(&prev->ctx, &next->ctx);
        /* ...we resume here when someone switches back to `prev`. */
    }
    irq_restore(f);
}

void thread_yield(void)
{
    schedule();
}

void thread_sleep_ticks(uint64_t n)
{
    uint64_t f = irq_save();
    if (n == 0)
        n = 1;
    cur->wake_tick = timer_ticks() + n;
    cur->state = T_SLEEPING;
    schedule();
    irq_restore(f);
}

void thread_sleep_ms(uint64_t ms)
{
    thread_sleep_ticks((ms * HZ + 999) / 1000);
}

void thread_exit(int code)
{
    irq_disable();
    if (cur == idle)
        panic("idle thread tried to exit");
    cur->exit_code = code;
    cur->state = T_ZOMBIE;
    kprintf("[sched] thread %d (%s) exited with code %d\n", cur->tid, cur->name,
            code);
    schedule();
    for (;;)
        ; /* not reached */
}

void thread_block_current(void)
{
    cur->state = T_BLOCKED;
    schedule();
}

void thread_wake(struct thread *t)
{
    if (t->state != T_SLEEPING && t->state != T_BLOCKED)
        return;
    t->state = T_RUNNABLE;
    rq_push(&rq, &t->rq, t->prio);
    if (t->prio < cur->prio)
        need_resched = 1;
}

void sched_tick(void)
{
    uint64_t now = timer_ticks();
    int i;
    cur->cpu_ticks++;
    for (i = 0; i < MAX_THREADS; i++)
        if (threads[i].state == T_SLEEPING && threads[i].wake_tick <= now)
            thread_wake(&threads[i]);
    if (cur == idle) {
        if (rq.count)
            need_resched = 1;
    } else if (--cur->slice <= 0) {
        need_resched = 1;
    }
}

void sched_preempt_point(void)
{
    if (need_resched)
        schedule();
}

void sched_reap(void)
{
    uint64_t f = irq_save();
    int i;
    for (i = 1; i < MAX_THREADS; i++) {
        struct thread *t = &threads[i];
        if (t->state == T_ZOMBIE && t != cur) {
            void *stack = t->kstack;
            if (t->user_slot >= 0)
                user_slot_release(t->user_slot);
            t->kstack = 0;
            t->state = T_UNUSED;
            irq_restore(f);
            page_free(stack);
            f = irq_save();
        }
    }
    irq_restore(f);
}

const char *thread_state_name(enum thread_state s)
{
    switch (s) {
    case T_RUNNABLE: return "ready";
    case T_RUNNING:  return "running";
    case T_SLEEPING: return "sleeping";
    case T_BLOCKED:  return "blocked";
    case T_ZOMBIE:   return "zombie";
    default:         return "unused";
    }
}

struct thread *thread_at(int idx)
{
    if (idx < 0 || idx >= MAX_THREADS)
        return 0;
    return &threads[idx];
}

uint64_t sched_context_switches(void)
{
    return nswitch;
}
