
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spinlock.h"
#include "clock-timer.h"

#define SECONDS 60
#define MINUTES 60
#define HOURS   12
#define ONE_HOUR 3600
#define ONE_MINUTE 60
#define HALF_DAY 43200 // 12*3600

typedef struct link_list {
    timer_node_t head;
    timer_node_t *tail;
}link_list_t;

typedef struct timer {
    link_list_t second[SECONDS];
    link_list_t minute[MINUTES];
    link_list_t hour[HOURS];
    spinlock_t lock;
    uint32_t time;
    time_t current_point;
}timer_st;

static timer_st * TI = NULL;

static timer_node_t *
link_clear(link_list_t *list) {
    timer_node_t * ret = list->head.next;
    list->head.next = 0;
    list->tail = &(list->head);

    return ret;
}

static void
link_to(link_list_t *list, timer_node_t *node) {
    list->tail->next = node;
    list->tail = node;
    node->next=0;
}

static void
add_node(timer_st *T, timer_node_t *node) {
    uint32_t time=node->expire;
    uint32_t current_time=T->time;
    uint32_t msec = time - current_time;
    if (msec < ONE_MINUTE) {
        link_to(&T->second[time % SECONDS], node);
    } else if (msec < ONE_HOUR) {
        link_to(&T->minute[(uint32_t)(time/ONE_MINUTE) % MINUTES], node);
    } else {
        link_to(&T->hour[(uint32_t)(time/ONE_HOUR) % HOURS], node);
    }
}

static void
remap(timer_st *T, link_list_t *level, int idx) {
    timer_node_t *current = link_clear(&level[idx]);
    while (current) {
        timer_node_t *temp=current->next;
        add_node(T, current);
        current=temp;
    }
}

static void
timer_shift(timer_st *T) {
    uint32_t ct = ++T->time % HALF_DAY;
    if (ct == 0) {
        remap(T, T->hour, 0);
    } else {
        if (ct % SECONDS == 0) {
            {
                uint32_t idx = (uint32_t)(ct / ONE_MINUTE) % MINUTES;
                if (idx != 0) {
                    remap(T, T->minute, idx);
                    return;
                }
            }
            {
                uint32_t idx = (uint32_t)(ct / ONE_HOUR) % HOURS;
                if (idx != 0) {
                    remap(T, T->hour, idx);
                }
            }
        }
    }
}

static void
dispatch_list(timer_node_t *current) {
    do {
        timer_node_t * temp = current;
        current=current->next;
        if (temp->cancel == 0)
            temp->callback(temp);
        free(temp);
    } while (current);
}

static void
timer_execute(timer_st *T) {
    uint32_t idx = T->time % SECONDS;

    while (T->second[idx].head.next) {
        timer_node_t *current = link_clear(&T->second[idx]);
        spinlock_unlock(&T->lock);
        dispatch_list(current);
        spinlock_lock(&T->lock);
    }
}

static void
timer_update(timer_st *T) {
    spinlock_lock(&T->lock);
    timer_execute(T);
    timer_shift(T);
    timer_execute(T);
    spinlock_unlock(&T->lock);
}

static timer_st *
create_timer() {
    timer_st *r = (timer_st *)malloc(sizeof(timer_st));
    memset(r,0,sizeof(*r));

    int i;
    for (i=0; i<SECONDS; i++) {
        link_clear(&r->second[i]);
    }
    for (i=0; i<MINUTES; i++) {
        link_clear(&r->minute[i]);
    }
    for (i=0; i<HOURS; i++) {
        link_clear(&r->hour[i]);
    }
    spinlock_init(&r->lock);
    return r;
}

void
init_timer(void) {
    TI = create_timer();
    TI->current_point = now_time();
}

timer_node_t*
add_timer(int time, handler_pt func) {
    timer_node_t *node = (timer_node_t *)malloc(sizeof(*node));
    spinlock_lock(&TI->lock);
    node->expire = time+TI->time;
    printf("add timer at %u, expire at %u, now_time at %lu\n", TI->time, node->expire, now_time());
    node->callback = func;
    node->cancel = 0;
    if (time <= 0) {
        spinlock_unlock(&TI->lock);
        node->callback(node);
        free(node);
        return NULL;
    }
    add_node(TI, node);
    spinlock_unlock(&TI->lock);
    return node;
}

void
del_timer(timer_node_t *node) {
    node->cancel = 1;
}

void
check_timer(int *stop) {
    while (*stop == 0) {
        time_t cp = now_time();
        if (cp != TI->current_point) {
            uint32_t diff = (uint32_t)(cp - TI->current_point);
            TI->current_point = cp;
            int i;
            for (i=0; i<diff; i++) {
                timer_update(TI);
            }
        }
        usleep(200000);
    }
}

void
clear_timer() {
    int i;
    for (i=0; i<SECONDS; i++) {
        link_list_t * list = &TI->second[i];
        timer_node_t* current = list->head.next;
        while(current) {
            timer_node_t * temp = current;
            current = current->next;
            free(temp);
        }
        link_clear(&TI->second[i]);
    }
    for (i=0; i<MINUTES; i++) {
        link_list_t * list = &TI->minute[i];
        timer_node_t* current = list->head.next;
        while(current) {
            timer_node_t * temp = current;
            current = current->next;
            free(temp);
        }
        link_clear(&TI->minute[i]);
    }
    for (i=0; i<HOURS; i++) {
        link_list_t * list = &TI->hour[i];
        timer_node_t* current = list->head.next;
        while(current) {
            timer_node_t * temp = current;
            current = current->next;
            free(temp);
        }
        link_clear(&TI->hour[i]);
    }
}

time_t
now_time() {
    struct timespec ti;
    clock_gettime(CLOCK_MONOTONIC, &ti);
    // 1ns = 1/1000000000 s = 1/1000000 ms
    return ti.tv_sec;
}
