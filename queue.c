#include "queue.h"

#include <stdlib.h>
#include <errno.h>
#include <assert.h>

int queue_init(queue_t *q, int n) {
    int rc;

    if ((q->buf = malloc(n * sizeof(int))) != NULL) {
        q->n = n;
        q->front = q->rear = q->size = 0;
        if ((rc = pthread_mutex_init(&q->mutex, NULL)) == 0) 
            return 0;
        errno = rc;
        free(q->buf);
    }
    return -1;
}

void queue_destroy(queue_t *q) {
    pthread_mutex_destroy(&q->mutex);
    free(q->buf);
    q->buf = NULL;
    q->front = q->rear = q->n = q->size = 0;
}

int queue_empty(queue_t *q) {
    int empty;

    pthread_mutex_lock(&q->mutex);
    empty = (q->size == 0);
    pthread_mutex_unlock(&q->mutex);

    return empty;
}

int queue_full(queue_t *q) {
    int full;

    pthread_mutex_lock(&q->mutex);
    full = (q->size == q->n);
    pthread_mutex_unlock(&q->mutex);
    
    return full;
}

int enqueue(queue_t *q, int item) {
    pthread_mutex_lock(&q->mutex);
    if (q->size == q->n) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    q->buf[(++q->rear)%(q->n)] = item;
    q->size++;
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

int dequeue(queue_t *q, int *item) {
    pthread_mutex_lock(&q->mutex);
    if (q->size == 0) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    *item = q->buf[(++q->front)%(q->n)];
    q->size--;
    pthread_mutex_unlock(&q->mutex);
    return 0;
}