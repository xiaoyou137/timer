#include <stdio.h>
#include "clock-timer.h"

void do_timer(timer_node_t *node) {
    (void)node;
    printf("do_timer expired now_time:%lu\n", now_time());
}

int stop = 0;
int main() {
    init_timer();
    add_timer(3, do_timer);
    check_timer(&stop);
    clear_timer();
    return 0;
}