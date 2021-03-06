#if !defined(_UTIL_H)
#define _UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if !defined(MAX_PATH)
#define MAX_PATH 128
#endif

#define SC_EXIT(r, sc, str)	\
	if((r=sc) == -1){	\
		perror(#str);	\
		exit(errno);	\
	}

#define SC_RETURN(r, sc, str)	\
	if((r=sc) == -1){	\
		perror(#str);	\
		return r;	\
	}
	

#define CHECK_EQ_RETURN(X, val, str, r)	\
	if((X) == val){			\
		fprintf(stderr, "ERRORE FATALE: ");\
		perror(#str);		\
		return r;		\
	}

#define CHECK_NEQ_RETURN(X, val, str, r)\
	if((X) != val){			\
		fprintf(stderr, "ERRORE FATALE: ");\
		perror(#str);		\
		return r;		\
	}

#define CHECK_EQ_EXIT(X, val, str)	\
	if((X) == val){			\
		fprintf(stderr, "ERRORE FATALE: ");\
		perror(#str);		\
		exit(EXIT_FAILURE);	\
	}

#define CHECK_NEQ_EXIT(X, val, str)	\
	if((X) != val){			\
		fprintf(stderr, "ERRORE FATALE: ");\
		perror(#str);		\
		exit(EXIT_FAILURE);	\
	}


#define LOCK(m)						\
	if(pthread_mutex_lock(m) != 0){			\
		fprintf(stderr, "ERRORE FATALE lock\n");\
		pthread_exit((void*)EXIT_FAILURE);	\
	}

#define UNLOCK(m)					\
	if(pthread_mutex_unlock(m) != 0){		\
		fprintf(stderr, "ERRORE FATALE unlock\n");\
		pthread_exit((void*)EXIT_FAILURE);	\
	}

#define SIGNAL(c)					\
	if(pthread_cond_signal(c) != 0){		\
		fprintf(stderr, "ERRORE FATALE signal\n");\
		pthread_exit((void*)EXIT_FAILURE);	\
	}

#define WAIT(c, m)					\
	if(pthread_cond_wait(c, m) != 0){		\
		fprintf(stderr, "ERRORE FATALE wait\n");\
		pthread_exit((void*)EXIT_FAILURE);	\
	}

#define BROADCAST(c)					\
	if(pthread_cond_broadcast(c) != 0){		\
		fprintf(stderr, "ERRORE FATALE broadcast\n");\
		pthread_exit((void*)EXIT_FAILURE);	\
	}

/** 
 * @brief Controlla se la stringa passata come primo argomento è un numero ed effettua la conversione.
 * @return  0 ok, 1 non e' un numero,  2 overflow/underflow
 */
static inline int isNumber(const char* s, long* n){
	if(s == NULL) return 1;
	if(strlen(s) == 0) return 1;
	char* e = NULL;
	errno = 0;
	long val = strtol(s, &e, 10);
	if(errno == ERANGE) return 2;
	if(e != NULL && *e == (char)0){
		*n = val;
		return 0;
	}	
	return 1;
}

#endif /* _UTIL_H */
