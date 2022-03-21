#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <util.h>
#include <boundedqueue.h>

/**
 * @file boundedqueue.c
 * @brief file di implementazione dell'interfaccia per la coda concorrente a capacità limitata
 *
 */
 
bqueue_t* initBQueue(size_t n, size_t datasize){
	int errnocpy = 0;
	if(n <= 0){
		errno = EINVAL; //argomento non valido
		return NULL;
	}
	
	//alloco spazio per la coda
	bqueue_t* q = malloc(sizeof(bqueue_t));
	CHECK_EQ_RETURN(q, NULL, "malloc", NULL);

	//inizializzo campi della coda
	if(pthread_mutex_init(&(q->mtx), NULL) != 0){
		perror("pthread_mutex_init");
		errnocpy = errno;
		free(q);
		errno = errnocpy;
		return NULL;
	}
	if(pthread_cond_init(&(q->full), NULL) != 0){
		perror("pthread_cond_init");
		errnocpy = errno;
		if(&(q->mtx)) pthread_mutex_destroy(&(q->mtx));
		free(q);
		errno = errnocpy;
		return NULL;
	}
	if(pthread_cond_init(&(q->empty), NULL) != 0){
		perror("pthread_cond_init");
		errnocpy = errno;
		if(&(q->mtx)) pthread_mutex_destroy(&(q->mtx));
		if(&(q->full)) pthread_cond_destroy(&(q->full));
		free(q);
		errno = errnocpy;
		return NULL;
	}
	q->head = NULL;
	q->tail = NULL;
	q->maxcap = n;
	q->nelem = 0;
	q->datasize = datasize;
	return q;
}

int deleteBQueue(bqueue_t *q){
	if(!q){
		errno = EINVAL;	//argomento non valido
		return -1;
	}
	while(q->nelem){
		//estraggo senza salvare
		dequeue(q, NULL);
	}
	//libero spazio
	if(&(q->mtx)) pthread_mutex_destroy(&(q->mtx));
	if(&(q->full)) pthread_cond_destroy(&(q->full));
	if(&(q->empty)) pthread_cond_destroy(&(q->empty));
	free(q);
	return 0;
}

int enqueue(bqueue_t* q, void* data){
	if(!q || !data){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	//alloco un nuovo nodo per inserire l'elemento
	node_t* new = malloc(sizeof(node_t));
	CHECK_EQ_RETURN(new, NULL, "malloc", -1);

	new->data = malloc(q->datasize);
	CHECK_EQ_RETURN(new->data, NULL, "malloc", -1);
	if(!new->data){
		perror("malloc");
		free(new);
		errno = ENOMEM;
		return -1;
	}

	memcpy(new->data, data, q->datasize);
	new->next = NULL;
	
	LOCK(&(q->mtx), "96bq");
	
	while(q->nelem == q->maxcap) //coda piena -> attendere
		WAIT(&(q->full), &(q->mtx));
	
	//inserimento in coda
	if(q->nelem) //ci sono già altri elementi
		q->tail->next = new;
	else //primo elemento
		q->head = new;
	q->tail = new;
	q->nelem++;
	
	if(q->nelem == 1) //prima la coda era vuota -> svegliare che la coda non è più vuota
		BROADCAST(&(q->empty));
		
	UNLOCK(&(q->mtx));
	
	return 0;
}

int dequeue(bqueue_t* q, void* data){
	//data può essere NULL se vogliamo estrarre l'elemento dalla coda senza salvarlo
	if(!q){
		errno = EINVAL;
		return -1;
	}
	
	LOCK(&(q->mtx), "124bq");
	
	while(q->nelem == 0) //coda vuota -> attendere
		WAIT(&(q->empty), &(q->mtx));
	
	//rimozione in testa
	node_t* tmp = q->head; 
	if(data)
		memcpy(data, tmp->data, q->datasize);
	q->head = tmp->next;
	q->nelem--;
	if(q->nelem == 0) //la coda è rimasta vuota
		q->tail = NULL;
	
	if(q->nelem < q->maxcap) //la coda non è piena -> svegliare che la coda non è più piena 
		BROADCAST(&(q->full));
		
	UNLOCK(&(q->mtx));
	
	free(tmp->data);
	free(tmp);
	return 0;
}
