#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <util.h>
#include <conn.h>
#include <configserver.h>
#include <loggingserver.h>
#include <filestorage.h>
#include <boundedqueue.h>
#include <signalhserver.h>
#include <workerserver.h>

/**
 * @brief File servermain.c in cui si implementano le funzionalità del main thread
 */

#define MAX_REQUESTS 2048 	//numero max di richieste (task) che può contenere la coda Manager -> Workers


//informazioni per la configurazione del Server per il suo avvio
static info_t* configinfo;
static char sockname[UNIX_PATH_MAX] = "";

//var.condivisa tra MainThread e WorkerThreads
//FILE* logFile = NULL;
//pthread_mutex_t logmtx = PTHREAD_MUTEX_INITIALIZER;

void cleanup(){
	unlink(sockname);
	//unlink(configinfo->sockname.value);
}

//funzione che aggiorna il massimo fd di un set per ciclare su select
static int updatemax(fd_set set, int fdmax){
	for(int i = (fdmax-1); i >= 0; i--)
		if(FD_ISSET(i, &set))
			return i;
	return -1;
}

int main(int argc, char* argv[]){
	
	printf("-> Avvio Server PID:%d\n", getpid());
	
	//controllo numero di argomenti passati da linea di comando
	if(argc == 1){
		fprintf(stderr, "S > USAGE: %s -f <config_file_name>\n", argv[0]);
		return EXIT_FAILURE;
	}
	int err; //variabile usata in SC_EXIT
	
	//MASCHERA DEI SEGNALI: SIGINT, SIGQUIT, SIGHUP
	sigset_t mask;
	SC_EXIT(err, sigemptyset(&mask), "sigempty");
	SC_EXIT(err, sigaddset(&mask, SIGINT), "sigaddset");
	SC_EXIT(err, sigaddset(&mask, SIGQUIT), "sigaddset");
	SC_EXIT(err, sigaddset(&mask, SIGHUP), "sigaddset");
	CHECK_NEQ_EXIT(pthread_sigmask(SIG_BLOCK, &mask, NULL), 0, "pthread_sigmask");	
	//si ignora SIGPIPE per evitare di essere terminato da una scrittura su un socket (chiuso)
	struct sigaction sigapipe;
	memset(&sigapipe, 0, sizeof(sigapipe));
	sigapipe.sa_handler = SIG_IGN;
	SC_EXIT(err, sigaction(SIGPIPE, &sigapipe, NULL), "sigactionSIGPIPE");
	//flag per 
	//ricezione SIGINT e SIGQUIT, terminazione immediata = non accetta nuove richieste da client già connessi + chiude connessioni attive
	int terminationServer = 0; 
	//ricezione SIGHUP, terminazione x nuove richieste = non accetta nuove richieste da nuovi client + serve richieste da client già attivi
	int stopRequests = 0; 
	//mutex per falg
	pthread_mutex_t sigmtx;
	CHECK_NEQ_EXIT(pthread_mutex_init(&sigmtx, NULL), 0, "pthread_mutex_init");
	//pipe per la comunicazione del thread che gestisce i segnali con il thread main 
	int pipeSig[2];
	SC_EXIT(err, pipe(pipeSig), "pipe");
	//thread per la gestione dei segnali
	pthread_t sigHandler;
	//argomenti da passare al thread che gestisce i segnali
	sigHArgs_t sigArgs;// = {&mask, &terminationServer, &stopRequests, &sigmtx, pipeSig[1]};
	sigArgs.mask = &mask;
	sigArgs.terminationServer = &terminationServer;
	sigArgs.stopRequests = &stopRequests;
	sigArgs.sigmtx = &sigmtx;
	sigArgs.fdmain = pipeSig[1];
	CHECK_NEQ_EXIT(pthread_create(&sigHandler, NULL, &sighandlerF, (void*)&sigArgs), 0, "pthread_create");
	
	//PARSING file di configurazione
	char opt;
	char *configfilename = NULL;
	while((opt = getopt(argc, argv, ":f:")) != -1){
		switch(opt){
		case 'f':{
			configfilename = optarg;
		} break;
		case '?':{
			fprintf(stderr, "S > WARNING: l'opzione '-%c' non è riconosciuta\n", optopt);
		} break;
		case ':':{
			fprintf(stderr, "S > WARNING: l'opzione '-%c' richiede un argomento (nome del file di configurazione)\n", optopt);
		} break;
		default:;
		}
	}
	//CONFIGURAZIONE SERVER, tramite informazioni contenute nel file passato come argomento
	configinfo = getConfigInfo(configfilename);
	if(!configinfo) {
		fprintf(stderr, "S > ERRORE durante la configurazione del Server\n");
		exit(EXIT_FAILURE);
	}
	printConfiguration(configinfo, configfilename);
	
	//path del file di log
	char* pathlog = configinfo->logpath.value; 
	//creazione file di log in cui il server effettuerà il logging durante la sua esecuzione
	logFile = fopen(pathlog, "w");
	CHECK_EQ_EXIT(logFile, NULL, "fopen");
	
	logEvent("Configurazione Server effettuata\n");
	
	size_t maxfile = (size_t) atoi(configinfo->maxfile.value);
	size_t maxcap = (size_t) atoi(configinfo->maxcap.value);
	
	size_t replacementp = getPolicy(configinfo->replacementp.value);
	
	strncpy(sockname, configinfo->sockname.value, strlen(configinfo->sockname.value));
	
	filestorage_t* fstorage = initFStorage(maxfile, maxcap, replacementp);
	CHECK_EQ_EXIT(fstorage, NULL, "initFStorage");
	
	//pulizia di eventuali socket precedenti
	cleanup();
	atexit(cleanup);
	
	//COMUNICAZIONE
	int maxconn = 0;	//numero massimo di connessioni contemporanee
	int listenfd, connfd;
	int countfd = 0;
	SC_EXIT(listenfd, socket(AF_UNIX, SOCK_STREAM, 0), "socket");
	struct sockaddr_un sa;
	memset(&sa, '0', sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, configinfo->sockname.value, UNIX_PATH_MAX);
	SC_EXIT(err, bind(listenfd, (struct sockaddr*)&sa, sizeof(sa)), "bind");
	SC_EXIT(err, listen(listenfd, MAXBACKLOG), "listen");
	
	//coda per la comunicazione del thread Manager con i threads Worker
	bqueue_t* queueMtoW = initBQueue(MAX_REQUESTS, sizeof(int)); 
	CHECK_EQ_EXIT(queueMtoW, NULL, "initBQueue");
	
	//pipe per la comunicazione dei threads Worker con il thread Manager
	int pipeWtoM[2]; 
	SC_EXIT(err, pipe(pipeWtoM), "pipe");
	
	int nthwork = atoi(configinfo->nworker.value);
	free(configinfo);
		
	//logEvent("[NTHREADSW] %d\n", nthwork);

	pthread_mutex_t workmtx;
	CHECK_NEQ_EXIT(pthread_mutex_init(&workmtx, NULL), 0, "pthread_mutex_init");
	pthread_cond_t workcond;
	CHECK_NEQ_EXIT(pthread_cond_init(&workcond, NULL), 0, "pthread_cond_init");
	//int idWorker = 0; // x statistiche
	//argomenti da passare al thread Worker per eseguire le richieste
	workArgs_t* workerArgs = (workArgs_t*) malloc(sizeof(workArgs_t)); 
	CHECK_EQ_EXIT(workerArgs, NULL, "malloc");
	//workerArgs[i].thid = i+1;
	workerArgs->storage = fstorage;
	workerArgs->queue = queueMtoW;
	workerArgs->fdwrite = pipeWtoM[1];
	
	//il main thread è il Manager
	//array dei threads Worker
	pthread_t* workers = (pthread_t*) malloc(nthwork * sizeof(pthread_t));
	CHECK_EQ_EXIT(workers, NULL, "malloc");
	for(int i = 0; i < nthwork; i++)
		CHECK_NEQ_EXIT(pthread_create(&workers[i], NULL, &workerF, (void*)workerArgs), 0, "pthread_create");
	
	//set dei descrittori attivi ed attesi in lettura
	fd_set set, rdset;
	FD_ZERO(&set);
	FD_ZERO(&rdset);
	FD_SET(pipeSig[0], &set);	//fd endpoint di lettura della pipe di comunicazione tra i thread gestore segnali e main
	FD_SET(listenfd, &set);		//fd listen socket per richeste di connessioni
	FD_SET(pipeWtoM[0], &set); 	//fd endpoint di lettura della pipe di comunicazione Ws -> M
	
	countfd = (pipeSig[0] < listenfd) ? listenfd : pipeSig[0];
	countfd = (pipeWtoM[0] < countfd) ? countfd : pipeWtoM[0];
	
	int nActiveClient = 0; //numero di client connessi
		
	//ciclo sulla select finchè non viene catturato un segnale per cui si deve avviare la terminazione del server
	while(!iscaught(sigmtx, terminationServer)){
		//in caso di terminazione con SIGHUP si deve togliere dalla maschera listenfd (se sempre valida)
		/*if (iscaught(sigmtx, stopRequests) && listenfd != -1) {
			FD_CLR(listenfd, &set);
			if (listenfd == countfd)
				countfd = updatemax(set, countfd);
			SC_EXIT(err, close(listenfd), "close listenfd(1)");
			listenfd = -1;
			if(nActiveClient <= 0){
				//non ci sono più client connessi si può terminare il server
				terminazione(sigmtx, &terminationServer);
				break;
			}
		}*/
		
		//salvo il set nella variabile temporanea per la select, che darà i fd pronti per la lettura
		rdset = set; 
		if(select(countfd + 1, &rdset, NULL, NULL, NULL) == -1){
			perror("select");
			exit(EXIT_FAILURE);
		}
		//scansione dei vari fd del set
		for(int i = 0; i <= countfd; i++){
			//controllo se è stato catturato il segnale per cui terminationServer viene settato
			if(iscaught(sigmtx, terminationServer))
				break;
			if(FD_ISSET(i, &rdset)){
			//descrittore i pronto -> si analizza a cosa corrisponde
				if(i == pipeSig[0]){ //ricevuto un segnale, controllo il tipo di terminazione da attuare
					//è stato catturato SIGINT/SIGQUIT e settato terminationServer
					if(iscaught(sigmtx, terminationServer))
						break;
					//altrimenti è stato catturato SIGHUP e settato stopRequests
					FD_CLR(pipeSig[0], &set);
					if(pipeSig[0] == countfd)
						countfd = updatemax(set, countfd);
					if(nActiveClient == 0){
						//non ci sono più client connessi si può terminare il server
						terminazione(sigmtx, &terminationServer);
						break;
					}
				}
				else if(i == pipeWtoM[0]){ //worker ha terminato una richiesta di un Client
					//si legge e si inserisce tale fd nel set dei descrittori da ascoltare
					int fdreq;
					SC_EXIT(err, read(i, &fdreq, sizeof(fdreq)), "read");
					if(fdreq == 0){ //un client ha fatto chiudere la connessione
						nActiveClient--; 
						if(iscaught(sigmtx, stopRequests) && nActiveClient <= 0){
						//non devo ricevere nuove richieste e non ci sono più Client connessi
						//se ci fossero stati Client sempre connessi avrei continuato a gestire le loro richieste
							terminazione(sigmtx, &terminationServer);
							break;
						}
					}
					//altrimenti reinserisco il fd del Client, appena gestito, per un'altra richiesta
					//conversione della stringa nell'intero rappresentate il fd
					else{ 
						FD_SET(fdreq, &set); 
						countfd = (countfd < fdreq) ? fdreq : countfd;
					}
				}
				else if(i == listenfd){ //nuova richiesta di connessione
					//controllo se è stato catturato un segnale per cui terminationServer viene settato
					if(iscaught(sigmtx, terminationServer))
						break; //si esce dal ciclo esterno per terminare
					//controllo se è stato catturato SIGHUP per cui stopRequests viene settato
					if(iscaught(sigmtx, stopRequests))
						continue; //si esce dal ciclo interno per gestire le richieste da client già connessi
					//se non è arrivato SIGHUP e non si deve terminare, accetto la nuova connessione
					SC_EXIT(connfd, accept(listenfd, NULL, NULL), "accept");
					FD_SET(connfd, &set);
					countfd = (countfd < connfd) ? connfd : countfd;
					nActiveClient++;
					maxconn = (maxconn < nActiveClient) ? nActiveClient : maxconn;
					logEvent("Nuova richiesta di connessione accettata: Client id:%d\n", connfd);
					fprintf(stdout, "\nS > Nuovo Client connesso fd:%d\n", connfd);
				}
				else{ //nuova richiesta da client connesso
					//controllo se è stato catturato un segnale per cui terminationServer viene settato
					if(iscaught(sigmtx, terminationServer))
						break; //si esce dal ciclo esterno
					FD_CLR(i, &set); //viene resettato a 0 fd del client che ha appena fatto una richiesta
					if(i == countfd)
						countfd = updatemax(set, countfd);
					//si inserisce fd del client nella coda di comunicazione tra Manager -> Workers	
					CHECK_EQ_EXIT(enqueue(queueMtoW, (void*)&i), -1, "enqueue fd ready");
				}
			}
		}
	}
	//'nthwork' msg di terminazione per i thWorker che consumano dati da BQueue condivisa con il thManager
	int end = 0;
	for(int i = 0; i < nthwork; i++)
		CHECK_EQ_EXIT(enqueue(queueMtoW, (void*)&end), -1, "enqueue");	
	
	for(int i = 0; i < nthwork; i++)
		CHECK_NEQ_EXIT(pthread_join(workers[i], NULL), 0, "pthread_join");
		
	
	CHECK_NEQ_EXIT(pthread_join(sigHandler, NULL), 0, "pthread_join");
	pthread_mutex_destroy(&sigmtx);
	
	//if(listenfd != -1) 
		SC_EXIT(err, close(listenfd), "close listenfd(2)");		
	SC_EXIT(err, close(pipeWtoM[0]), "close pipeWtoM[0]");
	SC_EXIT(err, close(pipeWtoM[1]), "close pipeWtoM[1]");
	SC_EXIT(err, close(pipeSig[0]), "close pipeSig[0]");
	
	unlink(sockname);
	
	//al termine dell'esecuzione si stampa sullo std output sunto delle operazioni effettuate durante l'esecuzione
	printSummary(fstorage);
	logEvent("[MAXCONNECTIONS] %zu", maxconn);
	logEvent("[MAXSIZE] %zu", fstorage->maxsize);
	logEvent("[MAXNFILE] %zu", fstorage->maxnfile);
	logEvent("[REPLACE] %zu", fstorage->countreplace);
	
	fflush(logFile);
	fclose(logFile);
	free(workerArgs);
	free(workers);
	deleteBQueue(queueMtoW);
	deleteFStorage(fstorage);	
	return 0;
}
