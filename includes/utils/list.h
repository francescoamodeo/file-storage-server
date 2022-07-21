#ifndef FILE_STORAGE_SERVER_LIST_H
#define FILE_STORAGE_SERVER_LIST_H

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

typedef struct elem{
    struct elem* previous;
    void* data;
    struct elem* next;
}elem_t;

typedef struct list{
    elem_t* head;
    elem_t* tail;
    int length;
}list_t;


/**
 * Crea una lista di tipo list_t inizializzando tutti i campi.
 * @return puntatore alla lista creata o NULL in caso di errore. (setta errno)
 */
list_t* list_init();

/**
 * Stampa ogni elemento della lista come array di caratteri. (setta errno)
 */
void list_tostring(list_t* list);

/**
 *  Aggiunge in coda alla lista un elemento con valore data
 *
 * @param list - puntatore alla lista a cui aggiungere l'elemento
 * @param data - puntatore al contenuto generico del nuovo elemento
 * @return il puntatore all'oggetto appena inserito, NULL in caso di errore. (setta errno)
 */
elem_t* list_add(list_t* list, void* data);

/**
 *  Aggiunge in testa alla lista un elemento con valore data
 *
 * @param list - puntatore alla lista a cui aggiungere l'elemento
 * @param data - puntatore al contenuto generico del nuovo elemento
 * @return il puntatore all'oggetto appena inserito, NULL in caso di errore. (setta errno)
 */
elem_t* list_addhead(list_t* list, void* data);

/**
 * Restituisce il puntatore alla testa della lista, senza rimuoverlo
 *
 * @param list - lista da cui prelevare il nodo di testa
 * @return il puntatore al primo nodo della lista, NULL in caso errore. (setta errno)
 */
elem_t* list_gethead(list_t* list);

/**
 * Restituisce l'elemento successivo di quello passato come parametro. (setta errno)
 */
elem_t* list_getnext(list_t* list, elem_t* elem);

/**
 * Restituisce un puntatore al nodo in testa alla lista, rimuovendolo da essa.
 *
 * @param list - puntatore alla lista da cui rimuovere il primo nodo
 * @return puntatore al nodo appena rimosso. (setta errno)
 */
elem_t* list_removehead(list_t* list);

/**
 * Restituisce un puntatore all'ultimo nodo della lista, senza rimuoverlo
 *
 * @param list - lista da cui prelevare il nodo di coda
 * @return il puntatore all'ultimo nodo della lista, NULL in caso errore. (setta errno)
 */
elem_t* list_gettail(list_t* list);

/**
 * Restituisce un puntatore all'ultimo nodo della lista, rimuovendolo da essa.
 *
 * @param list - puntatore alla lista da cui rimuovere l'ultimo nodo
 * @return puntatore al nodo appena rimosso. (setta errno)
 */
elem_t* list_removetail(list_t* list);

/**
 * Confronta il contenuto dei nodi della lista con il valore passato come parametro,
 * se trova una corrispondenza restituisce un puntatore all'elemento, senza rimuoverlo dalla lista.
 *
 * @param list - puntatore alla lista
 * @param compared - valore da confrontare con il contenuto dei nodi della lista
 * @param compare_function - funzione utilizzata per confrontare i nodi,
 * @return puntatore all'elemento trovato, NULL altrimenti. (setta errno)
 */
elem_t* list_get(list_t* list, void* compared, int (*compare_function)(void*, void*));

/**
 * Confronta il contenuto dei nodi della lista con il valore passato come parametro,
 * se trova una corrispondenza restituisce un puntatore all'elemento, rimuovendolo dalla lista,
 *
 * @param list - puntatore alla lista
 * @param value - valore da confrontare con il contenuto dei nodi della lista
 * @param compare_function - funzione utilizzata per confrontare i nodi,
 * @return puntatore all'elemento trovato, NULL altrimenti. (setta errno)
 */
elem_t* list_remove(list_t* list, void* value, int (*compare_function)(void*, void*));

/**
 * Funzione che dealloca la lista e ogni suo nodo.
 *
 * @param list -  puntatore alla lista da deallocare
 * @param free_content - puntatore alla funzione che dealloca il contenuto dei nodi
 * (setta errno)
 */
void list_destroy(list_t *list, void (*free_func)(void*));

int compare_int(void *a, void *b);

#endif //FILE_STORAGE_SERVER_LIST_H
