#include "task.h"
#include "pmm.h"
#include "console.h"

#include <stddef.h>
#include <stdint.h>

#define TASK_LIMIT 4

typedef void (*task_entry_t)(void *);

typedef struct {
    uint64_t *saved_stack;
    void *stack_page;
    task_entry_t entry;
    void *argument;
    int active;
} task_t;

static task_t tasks[TASK_LIMIT];
static size_t current_task;
static int scheduler_initialized;

extern void context_switch(uint64_t **old_stack, uint64_t *new_stack);

static void task_bootstrap(void);

static void tasking_init(void) {
    if (scheduler_initialized) {
        return;
    }

    for (size_t i = 0; i < TASK_LIMIT; i++) {
        tasks[i].saved_stack = NULL;
        tasks[i].stack_page = NULL;
        tasks[i].entry = NULL;
        tasks[i].argument = NULL;
        tasks[i].active = 0;
    }

    /* Task zero represents the shell/kernel task already running. */
    tasks[0].active = 1;
    current_task = 0;
    scheduler_initialized = 1;
}

static size_t find_next_active_task(void) {
    for (size_t distance = 1; distance <= TASK_LIMIT; distance++) {
        size_t candidate = (current_task + distance) % TASK_LIMIT;
        if (tasks[candidate].active) {
            return candidate;
        }
    }

    return current_task;
}

void task_yield(void) {
    if (!scheduler_initialized) {
        return;
    }

    size_t previous = current_task;
    size_t next = find_next_active_task();

    if (next == previous) {
        return;
    }

    current_task = next;
    context_switch(&tasks[previous].saved_stack,
                   tasks[next].saved_stack);
}

void task_schedule_tick(void) {
    if (!scheduler_initialized) {
        return;
    }

    size_t previous = current_task;
    size_t next = find_next_active_task();

    if (next == previous) {
        return;
    }

    current_task = next;
    context_switch(&tasks[previous].saved_stack,
                   tasks[next].saved_stack);
}

static void task_bootstrap(void) {
    __asm__ volatile ("sti");
    task_entry_t entry = tasks[current_task].entry;
    void *argument = tasks[current_task].argument;

    entry(argument);

    /* The task has returned; switch away and never resume this stack. */
    tasks[current_task].active = 0;

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static int create_task(task_entry_t entry, void *argument) {
    for (size_t i = 1; i < TASK_LIMIT; i++) {
        if (tasks[i].active) {
            continue;
        }

        if (tasks[i].stack_page == NULL) {
            tasks[i].stack_page = pmm_alloc_page();
            if (tasks[i].stack_page == NULL) {
                return 0;
            }
        }

        tasks[i].entry = entry;
        tasks[i].argument = argument;

        uintptr_t top =
            (uintptr_t)tasks[i].stack_page + PMM_PAGE_SIZE;
        top &= ~(uintptr_t)0xF;
        top -= 8;

        /* Matches the register frame saved by context_switch.asm. */
        uint64_t *frame = (uint64_t *)(top - 7 * sizeof(uint64_t));
        for (size_t slot = 0; slot < 6; slot++) {
            frame[slot] = 0;
        }
        frame[6] = (uint64_t)(uintptr_t)task_bootstrap;

        tasks[i].saved_stack = frame;
        tasks[i].active = 1;
        return 1;
    }

    return 0;
}

static size_t active_task_count(void) {
    size_t count = 0;

    for (size_t i = 0; i < TASK_LIMIT; i++) {
        if (tasks[i].active) {
            count++;
        }
    }

    return count;
}

static void demo_worker(void *argument) {
    const char *name = argument;

    for (int round = 0; round < 3; round++) {
        console_write(name);
        console_write(" is running\n");
        task_yield();
    }
}

int task_test(void) {
    tasking_init();

    if (!create_task(demo_worker, "Task A") ||
        !create_task(demo_worker, "Task B")) {
        return 0;
    }

    while (active_task_count() > 1) {
        __asm__ volatile ("hlt");
    }

    return 1;
}
