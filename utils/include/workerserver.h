#if !defined(WORKER_SERVER_H_)
#define WORKER_SERVER_H_

/**
 *
 * @file Header per thread Worker del Server per gestire le richieste dei Client
 *
 */
 
#include <filestorage.h>
#include <boundedqueue.h>

/**
 * @brief struttura dati che il thread Manager(main) passa ai threads Worker
 * @param thid: id del thread worker - x statistiche (avrebbe bisogno di mutex per essere modificata dai thread worker)
 * @param storage: FileStorageServer necessario per eseguire le richieste dei Client sui file
 * @param queue: coda concorrente per comunicazione M -> Ws , per inserire/prelevare richieste pronte
 * @param fdwrite: fd della pipe(scrittura) per la comunicazione Ws -> M , per scrivere client gestiti
 */
typedef struct thArgsWorker{
	//int thid;	
	filestorage_t* storage; 	
	bqueue_t* queue; 
	int fdwrite; 
} workArgs_t;

//procedura che invia l'esito al Client riguardo la ME sul file (OK o FILEREMOVED), effettua le stampe su schermo e su log
void sendMsgME(int fd, int ret, char* path);

/* funzione eseguita dal thWorker che gestisce una richiesta per volta estratta dalla coda di comunicazione Manager -> Worker */
void* workerF(void* args);

#endif /* WORKER_SERVER_H_ */
