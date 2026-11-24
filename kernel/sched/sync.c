#include "arch.h"
#include "kernel.h"
#include "sched.h"
#include "sync.h"

void sem_init(struct semaphore *s, int count)
{
    s->count = count;
    list_init(&s->waiters);
}

void sem_down(struct semaphore *s)
{
    uint64_t f = irq_save();
    while (s->count <= 0) {
        struct thread *me = current_thread();
        list_push_back(&s->waiters, &me->rq.node);
        thread_block_current();
    }
    s->count--;
    irq_restore(f);
}

int sem_try_down(struct semaphore *s)
{
    uint64_t f = irq_save();
    int ok = 0;
    if (s->count > 0) {
        s->count--;
        ok = 1;
    }
    irq_restore(f);
    return ok;
}

void sem_up(struct semaphore *s)
{
    uint64_t f = irq_save();
    struct list_node *n;
    s->count++;
    n = list_pop_front(&s->waiters);
    if (n)
        thread_wake(container_of(n, struct thread, rq.node));
    irq_restore(f);
}

void mutex_init(struct mutex *m)
{
    sem_init(&m->sem, 1);
    m->owner = 0;
}

void mutex_lock(struct mutex *m)
{
    if (m->owner == current_thread())
        panic("mutex: recursive lock by %s", current_thread()->name);
    sem_down(&m->sem);
    m->owner = current_thread();
}

void mutex_unlock(struct mutex *m)
{
    if (m->owner != current_thread())
        panic("mutex: unlock by non-owner %s", current_thread()->name);
    m->owner = 0;
    sem_up(&m->sem);
}
