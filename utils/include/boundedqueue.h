#if !defined(BOUNDED_QUEUE_H)
#define BOUNDED_QUEUE_H

/**
 *
 * @file Header per Coda concorrente a capacità limitata
 *
 */

#include <pthread.h>

/**
 *
 * @struct node_t
 * @brief nodo interno alla coda
 *
 */
typedef struct node{
	void* data;
	struct node* next;
} node_t;

/**
 *
 * @struct bqueue_t
 * @brief coda limitata, concorrente
 *
 */
typedef struct bqueue{
	node_t* head;		//puntatore al primo elemento della coda 
	node_t* tail;		//puntatore all'ultimo elemento della coda
	size_t maxcap;		//massima capacità della coda
	size_t nelem;		//numero di elementi presenti nella coda
	size_t datasize;	//dimensione dati nella coda
	pthread_mutex_t mtx;	//mutex per accesso in mutua esclusone alla coda
	pthread_cond_t full;	//variabile di condizione per coda piena
	pthread_cond_t empty;	//variabile di condizione per coda vuota	
} bqueue_t;

/** 
 *
 * @brief funzione che alloca ed inizializza una coda limitata
 * @param n: massima capacità della coda
 * @param datasize: dimensione elementi della coda
 * @return puntatore alla coda allocata in caso di successo, NULL in caso di errore (setta errno)
 *
 */
bqueue_t* initBQueue(size_t n, size_t datasize);

/** 
 *
 * @brief funzione che dealloca una coda e tutti i suoi elementi 
 * @param queue: coda da deallocare
 * @return 0 in caso di successo, -1 in caso di errore (setta errno)
 *
 */
int deleteBQueue(bqueue_t *q);

 /** 
 *
 * @brief funzione che alloca ed inserisce nella coda un elemento, se la coda è piena rimane in attesa 
 * @param  q: coda nella quale inserire l'elemento
 * @param data: puntatore all'elemento da inserire
 * @return 0 in caso di successo, -1 in caso di errore (setta errno)
 *
 */
 int enqueue(bqueue_t* q, void* data);
 
 /** 
 *
 * @brief funzione che estrae dalla coda un elemento, se la coda è vuota rimane in attesa
 * @param q: coda dalla quale estrarre l'elemento
 * @param data: puntatore dove salvare l'elemento estatto
 * @return 0 in caso di successo, -1 in caso di errore (setta errno)
 *
 */
 int dequeue(bqueue_t* q, void* data);

#endif /* BOUNDED_QUEUE_H */
