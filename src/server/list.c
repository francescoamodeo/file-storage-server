
#include <list.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

int compare_int(void *a, void *b){
    return (*(int*)a) - (*(int*)b);
}

list_t* list_init() {

    list_t *list = (list_t *)malloc(sizeof(list_t));
    if(!list) 
        return NULL;
    
    list->head = NULL;
    list->tail = NULL;
    list->length = 0;

    return list;
}

void list_tostring(list_t* list){

    if (!list || list->length == 0) {
        errno = EINVAL;
        return;
    }

    elem_t *curr = list->head;
    while (curr != NULL) {
        printf("%s\n", (char*)curr->data);
        curr = curr->next;
    }
}

elem_t* list_add(list_t* list, void* data){

    if(!list || !data) {
        errno = EINVAL;
        return NULL;
    }

    elem_t* new = NULL;
    if((new = (elem_t*)malloc(sizeof(elem_t))) == NULL)
        return NULL;
    
    new->data = data;
    new->previous = NULL;
    new->next = NULL;

    //Se la lista è vuota
    if(list->length == 0) {
        list->head = new;
        list->tail = new;
    }else{
        new->previous = list->tail;
        list->tail->next = new;
        list->tail = new;
    }
    list->length++;
    return new;
}

elem_t* list_addhead(list_t* list, void* data){

    if(!list || !data) {
        errno = EINVAL;
        return NULL;
    }

    elem_t* new = NULL;
    if((new = (elem_t*)malloc(sizeof(elem_t))) == NULL)
        return NULL;
    
    new->data = data;
    new->next = NULL;
    new->previous = NULL;

    //Se la lista è vuota
    if(list->length == 0) {
        list->head = new;
        list->tail = new;
    }else{
        new->next = list->head;
        list->head->previous = new;
        list->head = new;
    }
    list->length++;
    return new;
}

elem_t* list_gethead(list_t* list){

    if(!list || list->length == 0) {
        errno = EINVAL;
        return NULL;
    }
    return list->head;
}

elem_t* list_removehead(list_t* list){

    if(!list || list->head == NULL) {
        errno = EINVAL;
        return NULL;
    }

    elem_t *return_elem = list->head;
    if (list->head == list->tail)
        list->head = list->tail = NULL;
    else {
        list->head = list->head->next;
        list->head->previous = NULL;
    }
    list->length--;
    return return_elem;
}

elem_t* list_gettail(list_t* list){

    if(!list || list->length == 0) {
        errno = EINVAL;
        return NULL;
    }
    return list->tail;
}

elem_t* list_removetail(list_t* list){

    if(!list || list->length == 0) {
        errno = EINVAL;
        return NULL;
    }

    elem_t* return_elem = list->tail;
    if(list->head == list->tail)
        list->head = list->tail = NULL;
    else{
        list->tail = list->tail->previous;
        list->tail->next = NULL;
    }
    list->length--;

    return return_elem;
}

elem_t* list_get(list_t* list, void* compared, int (*compare_function)(void*, void*)) {

    if (!list || !list->head) {
        errno = EINVAL;
        return NULL;
    }

    elem_t *curr = list->head;
    while (curr != NULL) {
        //Se la compare_function è diversa da NULL uso quella, altrimenti uso uguaglianza semplice
        if (((compare_function != NULL) && (compare_function(curr->data, compared) == 0)) ||
            ((compare_function == NULL) && (curr->data == compared))) {
            //elemento trovato
            return curr;
        }
        curr = curr->next;
    }
    errno = EINVAL;
    return NULL;
}

elem_t* list_remove(list_t* list, void* compared, int (*compare_function)(void*, void*)) {

    if (!list || !list->head || !compared) {
        errno = EINVAL;
        return NULL;
    }

    elem_t* return_elem = NULL;
    elem_t *curr = list->head;
    while (curr != NULL) {
        //Se la compare_function è diversa da NULL uso quella, altrimenti uso una shallow equality
        if (((compare_function != NULL) && (compare_function(curr->data, compared) == 0)) ||
            ((compare_function == NULL) && (curr->data == compared))) {
            //elemento trovato
            return_elem = curr;
            //Se è l'elemento di testa
            if (curr == list->head) {
                list->head = list->head->next;
                if (list->head)
                    list->head->previous = NULL;
            }
            //Se è l'elemento di coda
            else if (curr == list->tail) {
                list->tail = curr->previous;
                if (list->tail)
                    list->tail->next = NULL;
            }
            //E' un elemento centrale
            else {
                curr->previous->next = curr->next;
                curr->next->previous = curr->previous;
            }
            list->length--;
            //Se non ci sono più elementi sistemo la coda
            if (list->length == 0) {
                list->head = NULL;
                list->tail = NULL;
            }
            return return_elem;
        }
        curr = curr->next;
    }
    errno = EINVAL;
    return NULL;
}

elem_t* list_getnext(list_t* list, elem_t* elem){
    if(list == NULL || elem == NULL) {
        errno = EINVAL;
        return NULL;
    }
    elem_t *curr = list->head;
    while (curr != NULL){
        if(curr == elem)
            return elem->next;
        curr = curr->next;
    }

    errno = EINVAL;
    return NULL;
}

void list_destroy(list_t *list, void (*free_func)(void*)) {
    if(list == NULL || free_func == NULL) {
        errno = EINVAL;
        return;
    }

    elem_t *tmp;
    while(list->head != NULL){
        tmp = list->head;
        list->head = list->head->next;
        if(tmp->data) free_func(tmp->data);
        free(tmp);
    }
    free(list);
}