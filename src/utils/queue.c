#include "queue.h"


/* Crea una coda */
queue_t* init_queue() {
    queue_t *queue = malloc(sizeof(queue_t));
    if (queue == NULL)
        return NULL;
    queue->head = NULL;
    queue->tail = NULL;
    queue->length = 0;
    return queue;
}


/* Cancella una coda */
void delete_queue(queue_t *queue) {
    if (queue == NULL) {
        errno = EINVAL;
        return;
    }
    node_t *tmp;
    while (queue->head != NULL) {
        tmp = queue->head;
        queue->head = queue->head->next;
        free(tmp->data);
        free(tmp);
    }
    free(queue);
}


/* Inserisce un dato in fondo alla coda */
int push(queue_t *queue, void* data) {
    if (queue == NULL || data == NULL) {
        errno = EINVAL;     // Invalid argument
        return EXIT_FAILURE;
    }
    node_t *tmp = malloc(sizeof(node_t));
    if (tmp == NULL)
        return EXIT_FAILURE;
    tmp->next = NULL;
    tmp->data = data;
    if (queue->head == NULL) {
        queue->head = tmp;
        queue->tail = tmp;
    } else {
        queue->tail->next = tmp;
        queue->tail = tmp;
    }
    queue->length++;
    return EXIT_SUCCESS;
}


/* Estrae un dato dalla cima della coda */
void* pop(queue_t *queue) {
    if (queue == NULL || queue->head == NULL) {
        errno = EINVAL;
        return NULL;
    }
    node_t *tmp = queue->head;
    void *data = tmp->data;
    tmp->data = NULL;
    if (queue->head == queue->tail)
        queue->head = queue->tail = NULL;
    else
        queue->head = queue->head->next;
    queue->length--;
    free(tmp);
    return data;
}

/* Estrae un dato da un punto qualsiasi della coda */
int get(queue_t *queue, void* data) {
    if (queue == NULL || queue->head == NULL) {
        errno = EINVAL;
        return EXIT_FAILURE;
    }
    node_t *curr = queue->head;
    node_t *prev = NULL;
    while (curr != NULL && curr->data != data) {
        prev = curr;
        curr = curr->next;
    }
    if (curr == NULL) {
        errno = EINVAL;
        return EXIT_FAILURE;
    }
    if (curr == queue->head)   // coda con un solo nodo
        queue->head = queue->tail = NULL;
    else if (curr->next == NULL) {  // sto facendo una pop sull'ultimo nodo
        prev->next = NULL;
        queue->tail = prev;
    } else    // è un nodo interno
        prev->next = curr->next;
    free(curr);
    queue->length--;
    return EXIT_SUCCESS;
}
