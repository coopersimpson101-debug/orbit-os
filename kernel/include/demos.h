#ifndef ORBIT_DEMOS_H
#define ORBIT_DEMOS_H

/* 0 on success, -1 on resource exhaustion, -2 unknown demo name. */
int demo_spawn(const char *what);
const char *demo_names(void);
void shell_thread(void *arg);

#endif
