#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <util.h>
#include <conn.h>
#include <workerserver.h>
#include <loggingserver.h>
#include <filestorage.h>
#include <boundedqueue.h>

/**
 *
 * @file workerserver.c
 * @brief File di implementazione dell'interfaccia del thread Worker per gestire le richieste dei Client
 *
 */

// procedura per inviare il msg di errore al client a seguito della sua richiesta.
// La gestione della richiesta ha ritornato -1 settando errno 
static void sendMsgError(int fd){
	ssize_t n;
	int err = 0;
	switch(errno){
	case EINVAL: {
		err = INVALIDARGS;
		SC_EXIT(n, writen(fd, &err, sizeof(err)), "writenS27");
	} break;
	case EEXIST: {
		err = ALREADYEXIST;
		SC_EXIT(n, writen(fd, &err, sizeof(err)), "writenS31");
	} break;
	case ENOENT: {
		err = NOSUCHFILE;
		SC_EXIT(n, writen(fd, &err, sizeof(err)), "writenS35");
	} break;
	case EACCES: {
		err = PERMDENIED;
		SC_EXIT(n, writen(fd, &err, sizeof(err)), "writenS39");
	} break;
	case E2BIG: {
		err = FILETOOBIG;
		SC_EXIT(n, writen(fd, &err, sizeof(err)), "writenS43");
	} break;
	}
}

// procedura per inviare al Client i file espulsi
static void sendFiles(int fd, file_t* filesToSend){
	ssize_t n;
	//si invia al client un file per volta
	while(filesToSend){
		file_t* tmp = filesToSend;
		//si invia al client un msg con lunghezza path e lunghezza file, un msg con path e un msg con contenuto
		sreturnfile_t retfile;
		memset(&retfile, '0', sizeof(retfile));
		retfile.lenpath = strlen(filesToSend->path);
		retfile.contentsize = filesToSend->sizefile;
		SC_EXIT(n, writen(fd, &retfile, sizeof(retfile)), "writenS59");
		SC_EXIT(n, writen(fd, filesToSend->path, strlen(filesToSend->path)), "writenS60");
		SC_EXIT(n, writen(fd, filesToSend->content, filesToSend->sizefile), "writenS61");
		filesToSend = filesToSend->next;
		free(tmp->path);
		free(tmp->content);		
		free(tmp);
	}
}

/** procedura che invia l'esito al Client riguardo la ME sul file (OK o FILEREMOVED),
	effettua le stampe su schermo e su log */
void sendMsgME(int fd, int ret, char* path){
	ssize_t n;
	logEvent("E' stata acquisita la ME sul file \"%s\" per Client id:%d", path, fd);
	logEvent("[LOCK]");
	printf("\nS > E' stata acquisita la ME sul file \"%s\" per Client id:%d\n", path, fd);
	//si invia al client l'esito con successo della richiesta
	SC_EXIT(n, writen(fd, &ret, sizeof(ret)), "writenS78");
}

/* funzione eseguita dal thWorker che gestisce una richiesta per volta estratta dalla coda di comunicazione Manager -> Worker */
void* workerF(void* args){
	//inizializzazione struttura degli argomenti
	//int idWorker = ((workArgs_t*)args)->thid + 1; //non si utilizza poichè avrebbe bisogno di una mutex per essere modificata dai thW
	filestorage_t* fStorageMain = ((workArgs_t*)args)->storage;
	bqueue_t* queueMtoW = ((workArgs_t*)args)->queue;
	int pipeWtoM = ((workArgs_t*)args)->fdwrite;
	
	//si utilizza pthread_self per ottenere l'id del thread per non ricorrere ad una variabile globale 
	fprintf(stdout, "-> Avvio Worker THID:%ld\n", pthread_self());
	
	while(1){
		//prelevo dalla coda Manager -> Worker il fd pronto per la lettura del task da eseguire
		int fdreq = 0;
		CHECK_EQ_EXIT(dequeue(queueMtoW, (void*)&fdreq), -1, "dequeue fd ready");
		if(!fdreq) break; //si deve terminare l'esecuzione 

		ssize_t n = 0; 
		ssize_t nread = 0; //per prima lettura
		char* path = NULL;
		char* content = NULL;
		file_t* filesEx = NULL; //lista di file ritornati dal rimpiazzamento in write e append (open-creazione)
		
		//si legge la RICHIESTA {op, lenpath, contentsize, flags} del Client
		crequest_t req;
		memset(&req, 0, sizeof(req));
		req.op = 0; //?
		SC_EXIT(nread, readn(fdreq, &req, sizeof(req)), "readnS108");
		
		if(nread == 0){
			//client ha chiuso la connessione
			CHECK_EQ_EXIT(closeconnectionH(fStorageMain, fdreq, pipeWtoM), -1, "closeconnection");
			//si chiude il fd della connessione con il Client
			close(fdreq);
			logEvent("Connessione con Client id:%d chiusa\n", fdreq);
			fprintf(stdout, "\nS > Connessione Chiusa CLient fd: %d\n", fdreq);
			//ritorno al Manager fd = 0 così saprà che il Client gestito non è più connesso	
			int fdclose = 0;
			SC_EXIT(n, write(pipeWtoM, &fdclose, sizeof(nread)), "writeC"); 	
		}
		else{
			if(req.op != READNFILE){
				//si legge il Path del file
				path = calloc(req.lenpath + 1, 1);
				CHECK_EQ_EXIT(path, NULL, "calloc");
				SC_EXIT(n, readn(fdreq, path, req.lenpath), "readnS125");
	 		}
	 		//per le scritture si legge il Contenuto del file da scrivere
			if(req.op == WRITEFILE || req.op == APPENDFILE){
				content = calloc(req.contentsize + 1, 1);
				CHECK_EQ_EXIT(content, NULL, "calloc");
				SC_EXIT(n, readn(fdreq, content, req.contentsize), "readnS131");
			}
		
			logEvent("[WORKERID] %ld", pthread_self());
			
			switch(req.op){
			//ad ogni operazione si invia l'esito sul fd del Client richiedente l'operazione
			//se l'esito è negativo si invia il messaggio di errore in base al valore di errno
			//altrimenti si invia il messaggio di successo o direttamento il messaggio relativo alla risposta della richiesta
			case OPENFILE : {
				//op richiesta prima di w/W (con creazione e lock), a, r (apertura)
				logEvent("Richiesta OPENFILE da elaborare per Client id:%d", fdreq);
				int ret = 0; //indica se è stato espulso un file durante la creazione (caso w/W)
				if((ret = openfileH(fStorageMain, fdreq, path, req.flags, pipeWtoM)) == -1)
					sendMsgError(fdreq); //invia ERR < 0
				else {
					logEvent("File \"%s\" aperto", path);
					logEvent("[OPEN]");
					printf("\nS > File \"%s\" aperto\n", path);
					int openOK = (ret == 0) ? OK : ret; 
					//si invia al client l'esito con successo della richiesta
					SC_EXIT(n, writen(fdreq, &openOK, sizeof(openOK)), "writenS152"); 
					if(ret > 0) 
						logEvent("ESPULSO un file");
				}						
			} break;
			case READFILE : {
				logEvent("Richiesta READFILE da elaborare per Client id:%d", fdreq);
				ssize_t bytesRead = 0;
				if((bytesRead = readfileH(fStorageMain, fdreq, path)) == -1)
					sendMsgError(fdreq); //invia ERR < 0
				else {
					logEvent("File \"%s\" letto", path);
					logEvent("[READ] %zu", bytesRead);
					printf("S > File \"%s\" letto. Letti %zu bytes.\n", path, bytesRead);
					//il file letto viene inviato durante la gestione della readfile
				}
			} break;
			case READNFILE : {
				logEvent("Richiesta READNFILE da elaborare per Client id:%d", fdreq);
				ssize_t bytesRead = 0;
				int nFilesR = 0;
				if((bytesRead = readnfilesH(fStorageMain, fdreq, req.flags, &nFilesR)) == -1)
					sendMsgError(fdreq);
				else {
					logEvent("%d File letti", nFilesR);
					logEvent("[READ] %zu", bytesRead);
					printf("S > Files %d letti\n", nFilesR);
					//i file letti vengono inviati durante la gestione della readnfile
				}
			} break;
			case WRITEFILE : {
				logEvent("Richiesta WRITEFILE da elaborare per Client id:%d", fdreq);
				int nFilesEx = 0; //numero di file ritornati dal rimpiazzamento
				if((nFilesEx = writefileH(fStorageMain, fdreq, path, content, req.contentsize, &filesEx, pipeWtoM)) == -1)
					sendMsgError(fdreq);
				else {
					logEvent("File \"%s\" scritto", path);
					logEvent("[WRITE] %zu", req.contentsize);
					printf("S > File \"%s\" scritto. Scritti %zu bytes.\n", path, req.contentsize);
					int writeOK = nFilesEx; // >= 0 equivale a OK -> evito una writen
					//si invia al client l'esito con n.files espulsi
					SC_EXIT(n, writen(fdreq, &writeOK, sizeof(writeOK)), "writenS193"); 
					//si invia al client i file espulsi se ci sono
					if(nFilesEx > 0){
						sendFiles(fdreq, filesEx);
						logEvent("ESPULSI %d files", nFilesEx);
						fprintf(stdout, "S > Invio di %d file espulsi effettuato\n", nFilesEx);
					}
				}
			} break;
			case APPENDFILE : {
				logEvent("Richiesta APPENDFILE da elaborare per Client id:%d", fdreq);
				int nFilesEx = 0; //numero di file ritornati dal rimpiazzamento
				if((nFilesEx = appendtofileH(fStorageMain, fdreq, path, content, req.contentsize, &filesEx, pipeWtoM)) == -1)
					sendMsgError(fdreq);
				else {
					logEvent("File \"%s\" scritto", path);
					logEvent("[WRITE] %zu", req.contentsize);
					printf("S > File \"%s\" scritto. Scritti %zu bytes.\n", path, req.contentsize);
					int writeOK = nFilesEx; // >= 0 equivale a OK -> evito una writen
					//si invia al client l'esito con n.files espulsi
					SC_EXIT(n, writen(fdreq, &writeOK, sizeof(writeOK)), "writenS213"); 
					//si invia al client i file espulsi se ci sono
					if(nFilesEx > 0){
						sendFiles(fdreq, filesEx);
						logEvent("ESPULSI %d files", nFilesEx);
						fprintf(stdout, "S > Invio di %d file espulsi effettuato\n", nFilesEx);
					}
				}
			} break;
			case LOCKFILE : {
				logEvent("Richiesta LOCKFILE da elaborare per Client id:%d", fdreq);
				int ret = 0;
				if((ret = lockfileH(fStorageMain, fdreq, path)) == -1){
					sendMsgError(fdreq);
				}
				else if(ret == 1) //il fd è stato inserito nella lista di fd in attesa di ottenere la ME sul file
					continue; //salto all'iterazione successiva (non si ripassa fdreq al thM)
				else 
					sendMsgME(fdreq, OK, path);
					
			} break;
			case UNLOCKFILE : {
				logEvent("Richiesta UNLOCKFILE da elaborare per Client id:%d", fdreq);
				int fdlock = 0; //se >0 contiene fd del Client che era in attesa della ME sul file
				if((fdlock = unlockfileH(fStorageMain, fdreq, path)) == -1)
					sendMsgError(fdreq);
				else{
					logEvent("E' stata rilasciata la ME sul file \"%s\"", path);
					logEvent("[UNLOCK]");
					printf("\nS > E' stata rilasciata la ME sul file \"%s\"\n", path);
					int unlockOK = OK;
					//si invia al client l'esito con successo della richiesta
					SC_EXIT(n, writen(fdreq, &unlockOK, sizeof(unlockOK)), "writenS245"); 
				}
				if(fdlock > 0){
					//fdlock è il fd del Client che ha ottenuto la ME che attendeva sul file
					sendMsgME(fdlock, OK, path);
					//ritorno al Manager il fd del Client la cui richiesta di lockFile era stata sospesa
					SC_EXIT(n, write(pipeWtoM, &fdlock, sizeof(fdlock)), "writeC"); 
				}
			} break;
			case CLOSEFILE : {
				logEvent("Richiesta CLOSEFILE da elaborare per Client id:%d", fdreq);
				int fdlock = 0; //se >0 contiene fd del Client che era in attesa della ME sul file
				if((fdlock = closefileH(fStorageMain, fdreq, path)) == -1)
					sendMsgError(fdreq);
				else {
					logEvent("File \"%s\" chiuso", path);
					logEvent("[CLOSE]");
					printf("S > File \"%s\" chiuso\n", path);
					int closeOK = OK;
					//si invia al client l'esito con successo della richiesta
					SC_EXIT(n, writen(fdreq, &closeOK, sizeof(closeOK)), "writenS265"); 
				}
				if(fdlock > 0){
					//fdlock è il fd del Client che ha ottenuto la ME che attendeva sul file
					sendMsgME(fdlock, OK, path);
					//ritorno al Manager il fd del Client la cui richiesta di lockFile era stata sospesa
					SC_EXIT(n, write(pipeWtoM, &fdlock, sizeof(fdlock)), "writeC"); 
				}
			} break;
			case REMOVEFILE : {
				logEvent("Richiesta REMOVEFILE da elaborare per Client id:%d", fdreq);
				if(removefileH(fStorageMain, fdreq, path, pipeWtoM) == -1)
					sendMsgError(fdreq);
				else {
					logEvent("File \"%s\" rimosso", path);
					printf("\nS > File \"%s\" rimosso\n", path);
					int removeOK = OK;
					//si invia al client l'esito con successo della richiesta
					SC_EXIT(n, writen(fdreq, &removeOK, sizeof(removeOK)), "writenS283"); 
				}
			} break;
			case RESTORE : {
				restorefsH(fStorageMain, fdreq, path);
			} break;
			default : {
				logEvent("Richiesta da elaborare per Client id:%d NON GESTIBILE", fdreq);
				fprintf(stderr, "S > Codice Richiesta %d non supportato\n", req.op);
			}
			}
			//tramite endpoint di scrittura della pipe
			//ritorno al Manager il fd del Client appena gestito e nuovamente disponibile per una nuova richiesta
			SC_EXIT(n, write(pipeWtoM, &fdreq, sizeof(fdreq)), "writeS296"); 
			if(!errno)
				logEvent("Richiesta %d GESTITA\n", req.op);
		}
		
		free(path);
		free(content);
	}
	return NULL;
}
