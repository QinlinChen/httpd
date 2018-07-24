#include "queue.h"

#include <stdlib.h>
#include <errno.h>
#include <assert.h>

static int enqueue_nolock(queue_t *q, int item) {
    struct queue_node *pnew;
    
    if ((pnew = malloc(sizeof(struct queue_node))) == NULL)
        return -1;

    pnew->item = item;
    pnew->next = NULL;
    if (q->size == 0)
        q->front = pnew;
    else
        q->rear->next = pnew;
    q->rear = pnew;
    q->size++;
    
    return 0;
}

static int dequeue_nolock(queue_t *q, int *item) {
    struct queue_node *temp;
    
    if (q->size == 0)
        return -1;

    if (item != NULL)
        *item = q->front->item;
    temp = q->front;
    q->front = q->front->next;
    free(temp);
    q->size--;
    if (q->size == 0)
        q->rear = NULL;
    
    return 0;
}

int queue_init(queue_t *q) {
    int rc;

    q->front = q->rear = NULL;
    q->size = 0;
    if ((rc = pthread_mutex_init(&q->mutex, NULL)) == 0)
        return 0;
    errno = rc;
    return -1;
}

void queue_destroy(queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    while (q->size != 0)
        dequeue_nolock(q, NULL);
    assert(q->front == NULL);
    assert(q->rear == NULL);
    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
}

int queue_empty(queue_t *q) {
    int empty;

    pthread_mutex_lock(&q->mutex);
    empty = (q->size == 0);
    pthread_mutex_unlock(&q->mutex);

    return empty;
}

int enqueue(queue_t *q, int item) {
    int ret;

    pthread_mutex_lock(&q->mutex);
    ret = enqueue_nolock(q, item);
    pthread_mutex_unlock(&q->mutex);

    return ret;
}

int dequeue(queue_t *q, int *item) {
    int ret;

    pthread_mutex_lock(&q->mutex);
    ret = dequeue_nolock(q, item);
    pthread_mutex_unlock(&q->mutex);

    return ret;
}