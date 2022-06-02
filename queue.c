#include "queue.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

//credit to youtube channel Jacob Sorber for queue implementation

node_t *head = NULL;
node_t *tail = NULL;

bool enqueue(int *connfd) {
    printf("enqueueing a new request!\n");
    node_t *newnode;
    newnode = malloc(sizeof(node_t));
    if (newnode == NULL) {
        return false;
    }
    newnode->connfd = connfd;
    newnode->next = NULL;
    if (tail != NULL) {
        tail->next = newnode;
    } else {
        head = newnode;
    }
    tail = newnode;
    return true;
}

int *dequeue() {
    if (head != NULL) {
        printf("dequeueing a request!\n");
        int *connfd = head->connfd;
        node_t *tempnode = head;
        head = head->next;
        if (head == NULL) {
            tail = NULL;
        }
        free(tempnode);
        return connfd;
    } else { //if queue empty, return null
        return NULL;
    }
}
