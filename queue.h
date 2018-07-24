#include <pthread.h>

struct queue_node {
    int item;
    struct queue_node *next;
};

typedef struct {
  struct queue_node *front;
  struct queue_node *rear;
  int size;
  pthread_mutex_t mutex;
} queue_t;

int queue_init(queue_t *q);
void queue_destroy(queue_t *q);
int queue_empty(queue_t *q);
int queue_size(queue_t *q);
int enqueue(queue_t *q, int item);
int dequeue(queue_t *q, int *item);