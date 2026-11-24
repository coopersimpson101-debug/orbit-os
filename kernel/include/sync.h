/* Sleeping synchronisation primitives built on the scheduler.
 * Uniprocessor: atomicity comes from masking IRQs. */
#ifndef ORBIT_SYNC_H
#define ORBIT_SYNC_H

#include "list.h"

struct thread;

struct semaphore {
    int count;
    struct list_node waiters;
};

struct mutex {
    struct semaphore sem;
    struct thread *owner;
};

void sem_init(struct semaphore *s, int count);
void sem_down(struct semaphore *s);
int  sem_try_down(struct semaphore *s);   /* 1 on success */
void sem_up(struct semaphore *s);         /* IRQ-safe */

void mutex_init(struct mutex *m);
void mutex_lock(struct mutex *m);
void mutex_unlock(struct mutex *m);

#endif
