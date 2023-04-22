#ifndef CONN_CHANNEL_H
#define CONN_CHANNEL_H

#include <pthread.h>
#include <string.h>
#include <stdlib.h>

#include "common_structs.h"
#include "shared_memory.h"

#define MAX_QUEUE_LEN 100

typedef struct node_t
{
    int req_or_res_block_id;
} node_t;

typedef struct queue_t
{
    pthread_mutex_t lock;
    int head;
    int tail;
    int size;
    node_t nodes[MAX_QUEUE_LEN];
} queue_t;

queue_t *create_queue()
{
    create_file_if_does_not_exist(CONNECT_CHANNEL_FNAME);
    queue_t *q = (queue_t *)attach_memory_block(CONNECT_CHANNEL_FNAME, sizeof(queue_t));

    if (q == NULL)
    {
        fprintf(stderr, "ERROR: Could not create shared memory block for queue.\n");
        return NULL;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&q->lock, &attr);
    pthread_mutexattr_destroy(&attr);
    q->head = 0;
    q->tail = 0;
    q->size = 0;

    return q;
}

queue_t *get_queue()
{
    queue_t *q = (queue_t *)attach_memory_block(CONNECT_CHANNEL_FNAME, sizeof(queue_t));
    if (q == NULL)
    {
        fprintf(stderr, "ERROR: Could not create shared memory block for queue.\n");
        return NULL;
    }

    return q;
}

RequestOrResponse *post(queue_t *q, const char *client_name)
{
    pthread_mutex_lock(&q->lock);
    if (q->size == MAX_QUEUE_LEN)
    {
        fprintf(stderr, "ERROR: Could not enqueue to connection queue. Connection queue is full.\n");
        pthread_mutex_unlock(&q->lock);
    }

    char shm_req_or_res_fname[MAX_CLIENT_NAME_LEN];
    sprintf(shm_req_or_res_fname, "queue_%d", q->tail);
    create_file_if_does_not_exist(shm_req_or_res_fname);

    int req_or_res_block_id = get_shared_block(shm_req_or_res_fname, sizeof(RequestOrResponse));
    RequestOrResponse *shm_req_or_res = (RequestOrResponse *)attach_with_shared_block_id(req_or_res_block_id);
    if (shm_req_or_res == NULL)
    {
        fprintf(stderr, "ERROR: Could not create shared memory block %s for the queue.\n", shm_req_or_res_fname);
        return NULL;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm_req_or_res->lock, &attr);
    pthread_mutexattr_destroy(&attr);

    strncpy(shm_req_or_res->client_name, client_name, MAX_CLIENT_NAME_LEN);
    shm_req_or_res->stage = 0;

    // ? Does the nodes req_or_res pointer point to the same shared memory object
    // ? across proceeses ?

    // ? It is the same value for the pointer. Which means, the memcpy of the client_name did not occur properly.
    // q->nodes[q->tail].req_or_res = shm_req_or_res;
    q->nodes[q->tail].req_or_res_block_id = req_or_res_block_id;

    q->tail = (q->tail + 1) % MAX_QUEUE_LEN;
    q->size += 1;
    pthread_mutex_unlock(&q->lock);

    return shm_req_or_res;
}

RequestOrResponse *dequeue(queue_t *q)
{
    pthread_mutex_lock(&q->lock);

    if (q->size == 0)
    {
        fprintf(stderr, "ERROR: Connection queue empty. Could not fetch new request.\n");
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }

    int req_or_res_block_id = q->nodes[q->head].req_or_res_block_id;
    RequestOrResponse *req_or_res = attach_with_shared_block_id(req_or_res_block_id);

    q->head = (q->head + 1) % MAX_QUEUE_LEN;
    q->size -= 1;

    pthread_mutex_unlock(&q->lock);

    return req_or_res;
}

RequestOrResponse *fetch(queue_t *q)
{
    pthread_mutex_lock(&q->lock);

    if (q->size == 0)
    {
        fprintf(stderr, "ERROR: Connection queue empty. Could not fetch new request.\n");
        pthread_mutex_unlock(&q->lock);

        return NULL;
    }

    int req_or_res_block_id = q->nodes[q->head].req_or_res_block_id;

    // TODO: Preferably attach memory block at the original location we had specified. If not, that is fine as well.
    // RequestOrResponse *req_or_res = q->nodes[q->head].req_or_res;
    // req_or_res = attach_with_shared_block_id(req_or_res_block_id, req_or_res);

    RequestOrResponse *req_or_res = attach_with_shared_block_id(req_or_res_block_id);
    pthread_mutex_unlock(&q->lock);

    return req_or_res;
}

int empty(queue_t *q)
{
    pthread_mutex_lock(&q->lock);

    q->size = 0;
    pthread_mutex_unlock(&q->lock);

    return 0;
}

#endif