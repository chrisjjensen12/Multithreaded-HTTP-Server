#ifndef QUEUE_H_
#define QUEUE_H_
#include <stdbool.h>

struct node {
    int *connfd;

    struct node *next;
};
typedef struct node node_t;

int *dequeue();

bool enqueue(int *connfd);

#endif
