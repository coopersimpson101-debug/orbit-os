/* Demo workloads started with the shell's `spawn` command. Each one
 * exercises a kernel feature and prints a verifiable result line (the
 * QEMU CI test greps for these). */
#include "arch.h"
#include "drivers.h"
#include "kernel.h"
#include "sched.h"
#include "sync.h"
#include "user.h"
#include "demos.h"

/* --- counter: sleep/wake ------------------------------------------------ */
static void counter_fn(void *arg)
{
    int i;
    (void)arg;
    for (i = 1; i <= 3; i++) {
        kprintf("[counter] %d (uptime %lu ms)\n", i, uptime_ms());
        thread_sleep_ms(200);
    }
    kprintf("[counter] done\n");
}

/* --- mutex: two threads hammer a shared counter ------------------------ */
static struct mutex demo_mutex;
static volatile long shared_counter;
static struct semaphore mutex_done;

static void mutex_worker(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < 1000; i++) {
        mutex_lock(&demo_mutex);
        {
            long v = shared_counter;
            if ((i & 63) == 0)
                thread_yield(); /* deliberately yield inside the critical section */
            shared_counter = v + 1;
        }
        mutex_unlock(&demo_mutex);
    }
    sem_up(&mutex_done);
}

static void mutex_main(void *arg)
{
    (void)arg;
    mutex_init(&demo_mutex);
    sem_init(&mutex_done, 0);
    shared_counter = 0;
    thread_create("mutex-a", mutex_worker, 0, PRIO_DEFAULT);
    thread_create("mutex-b", mutex_worker, 0, PRIO_DEFAULT);
    sem_down(&mutex_done);
    sem_down(&mutex_done);
    kprintf("[mutex] shared counter = %ld (expected 2000) %s\n", (int64_t)shared_counter,
            shared_counter == 2000 ? "OK" : "FAIL");
}

/* --- producer/consumer with counting semaphores ------------------------- */
#define PC_SLOTS 4
static int pc_buf[PC_SLOTS];
static int pc_head, pc_tail;
static struct semaphore pc_empty, pc_full, pc_done;

static void producer(void *arg)
{
    int i;
    (void)arg;
    for (i = 1; i <= 8; i++) {
        sem_down(&pc_empty);
        pc_buf[pc_head++ % PC_SLOTS] = i * i;
        sem_up(&pc_full);
    }
}

static void consumer(void *arg)
{
    int i, sum = 0;
    (void)arg;
    for (i = 0; i < 8; i++) {
        sem_down(&pc_full);
        sum += pc_buf[pc_tail++ % PC_SLOTS];
        sem_up(&pc_empty);
        thread_sleep_ms(10);
    }
    kprintf("[pc] consumer sum = %d (expected 204) %s\n", sum, sum == 204 ? "OK" : "FAIL");
    sem_up(&pc_done);
}

static void pc_main(void *arg)
{
    (void)arg;
    pc_head = pc_tail = 0;
    sem_init(&pc_empty, PC_SLOTS);
    sem_init(&pc_full, 0);
    sem_init(&pc_done, 0);
    thread_create("producer", producer, 0, PRIO_DEFAULT);
    thread_create("consumer", consumer, 0, PRIO_DEFAULT);
    sem_down(&pc_done);
}

/* --- spin: proves timer preemption between equal-priority CPU hogs ------ */
static volatile uint64_t spin_count[2];

static void spin_fn(void *arg)
{
    int me = (int)(uintptr_t)arg, other = !me;
    uint64_t frq = cpu_counter_freq();
    uint64_t end = cpu_counter() + frq / 2; /* 500 ms of pure CPU */
    uint64_t other_start = spin_count[other];
    while (cpu_counter() < end)
        spin_count[me]++;
    kprintf("[spin-%c] done without yielding; other thread ran meanwhile -> "
            "preempted: %s\n", 'A' + me, spin_count[other] != other_start ? "yes" : "no");
}

/* --- fault: a kernel thread that dereferences an unmapped address ------- */
static void fault_fn(void *arg)
{
    (void)arg;
    kprintf("[fault] dereferencing 0xdead00000000...\n");
    (void)*(volatile uint64_t *)0xdead00000000ULL;
    kprintf("[fault] ERROR: survived a bad access\n");
}

int demo_spawn(const char *what)
{
    if (!k_strcmp(what, "counter"))
        return thread_create("counter", counter_fn, 0, PRIO_DEFAULT) ? 0 : -1;
    if (!k_strcmp(what, "mutex"))
        return thread_create("mutex", mutex_main, 0, PRIO_DEFAULT) ? 0 : -1;
    if (!k_strcmp(what, "pc"))
        return thread_create("pc", pc_main, 0, PRIO_DEFAULT) ? 0 : -1;
    if (!k_strcmp(what, "spin")) {
        spin_count[0] = spin_count[1] = 0;
        if (!thread_create("spin-A", spin_fn, (void *)0, PRIO_DEFAULT + 1))
            return -1;
        return thread_create("spin-B", spin_fn, (void *)1, PRIO_DEFAULT + 1) ? 0 : -1;
    }
    if (!k_strcmp(what, "fault"))
        return thread_create("fault", fault_fn, 0, PRIO_DEFAULT) ? 0 : -1;
    if (!k_strcmp(what, "user"))
        return spawn_user("user", UPROG_HELLO) >= 0 ? 0 : -1;
    if (!k_strcmp(what, "ufault"))
        return spawn_user("ufault", UPROG_FAULT) >= 0 ? 0 : -1;
    return -2;
}

const char *demo_names(void)
{
    return "counter mutex pc spin fault user ufault";
}
