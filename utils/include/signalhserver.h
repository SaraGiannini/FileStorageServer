#if !defined(SIGNALHANDLER_SERVER_H_)
#define SIGNALHANDLER_SERVER_H_


/**
 *
 * @file Header per la funzionalit√† di gestione segnali dedicata a un thread del Server
 *
 */

#include <pthread.h>


/**
 * @brief struttura dati che il thread main passa al thread che gestisce i segnali
 * @param mask: mashera dei segnali mascherati
 * @param terminationServer: flag per ricezione dei segnali SIGQUIT e SIGINT
 * @param stopRequests: glag per ricezione del segnale SIGHUP
 * @param sigmtx: mutex per l'accesso in ME ai flag dei segnali
 * @param fdmain: fd della pipe(scrittura) con il thMain a cui comunicare la ricezione dei segnali
 */
typedef struct thArgsSigHandler{
	sigset_t* mask;
	int* terminationServer;
	int* stopRequests;
	pthread_mutex_t* sigmtx;
	int fdmain;
} sigHArgs_t;

/* funzione eseguita dal thread che gestisce i segnali */
void *sighandlerF(void* args);

//procedura per settare il flag di terminazione del Server in mutua esclusione 
void terminazione(pthread_mutex_t mtx, int *termina);

//funzione per controllare se √® stato catturato un segnale e gestito, in mutua esclusione
int iscaught(pthread_mutex_t mutex, int flag);

#endif /* SIGNALHANDLER_SERVER_H_ */
