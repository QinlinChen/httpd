#include <pthread.h>

typedef struct {
  int *buf;
  int n;
  int size;
  int front;
  int rear;
  pthread_mutex_t mutex;
} queue_t;

int queue_init(queue_t *q, int n);
void queue_destroy(queue_t *q);
int queue_empty(queue_t *q);
int queue_full(queue_t *q);
int enqueue(queue_t *q, int item);
int dequeue(queue_t *q, int *item);