#if !defined(FILE_STORAGE_H_)
#define FILE_STORAGE_H_


/**
 *
 * @file Header per File Storage Server con funzionalità per gestire le richieste dei Client da parte dei threads Worker.
 *
 */
#include <sys/types.h>
#include <pthread.h>
#include <icl_hash.h>
/**
 *
 * @struct fd_t 
 * @brief tipo fd di Client di cui il Server deve tenere traccia relativamente ai file che ha aperto o di cui attende la ME
 *
 */
typedef struct fd{
	int fd;
	struct fd* next;
} fd_t;
 
 /**
 *
 * @struct file_t
 * @brief  tipo file interno al File Storage Server
 * @param ocreate: flag O_CREATE settato su richiesta del Client alla openFile-creazione. Settato con fd del richiedente per indicare che è 				colui che può effettuare la prima scrittura sul file, altrimenti -1 quando si effettua un'altra operazione sul file e
 			non si può più fare la writeFile
 * @param olock: flag O_LOCK settato su richiesta del Client alla openFile e con lockFile,
 			resettato con unlockFile che va prendere il CLient in attesa di fare la lockFile.
 			Settato con fd del richiedente per indicare che è colui che detiene la lock sul file, altrimenti -1 quando è "uncloked"
 */
typedef struct file{
	char* path;
	char* content;
	size_t sizefile;
	
	time_t referencetime;	//tempo in cui è stato usato l'ultima volta il file - per politica LRU
	int referencecount;	//conteggio dei riferimenti del file - per politica LFU

	int nreaders; 		//numero di lettori sul file
	
	int ocreate; 	
	int olock;	
	
	//riferimenti x lista concatenata
	struct file* next;
	//struct file* prev; 
	
	pthread_mutex_t mtx;	//mutex per il file 
	pthread_cond_t condrw; 	//variabile di condizione per lettura/scrittura su file (al max 1 scrittore, più lettori)
	
	fd_t* fdopening;	//lista fd dei Client che hanno aperto tale file
	fd_t* fdwaiting;	//lista fd dei Client che hanno richiesto la lock sul file gia 'locked' e stanno aspettando di acquisirla
} file_t;

 /**
 *
 * @struct filestorage_t
 * @brief  File Storage Server 
 *
 */
typedef struct filestorage{
	icl_hash_t* filetable;	//Hash Table di File (operazione di ricerca più efficiente)

 	file_t* head;		//riferimento primo elemento nella lista di file (efficienza rimpiazzamentoFIFO)
 	file_t* tail;		//riferimento ultimo elemento nella lista di file (aggiunta in coda del file x ordinamentoFIFO)

 	size_t atmostfile;	//numero massimo di file che il FS può contenere - da configurazione
 	size_t nfile;		//numero effettivo di file che il FS contiene 
 	size_t maxnfile;	//numero di file massimo memorizzati nel FS - x stampa di terminazione
 	
 	size_t atmostsize;	//dimensione massima del FS - da configurazione
 	size_t size;		//dimensione effettiva del FS
 	size_t maxsize;		//dimensione massima raggiunta dal FS - x stampa di terminazione
 	
	size_t replacepolicy;	//politica di rimpiazzamento dei file nella cache del server a seguito di una "capacity misses" {0 FIFO, 1 LRU, 2 LFU}
	size_t countreplace;	//numero di volte in cui è stato eseguito l'algoritmo di rimpiazzamento per selezionare la vittima

 	pthread_mutex_t mtx;	//mutex globale per tutti i campi della struttura dati
} filestorage_t;

/**
 *
 * @brief funzione che alloca ed inizializza un FileStorage
 * @param maxfile: numero massimo di file che può contenere il FS
 * @param maxcap: dimensione massima del FS
 * @return puntatore al FS in caso di successo, NULL in caso di errore
 *
 */
filestorage_t* initFStorage(size_t maxfile, size_t maxcap, size_t policy);

/**
 * @brief procedura per stampare a termine dell'esecuzione del server un riepilogo delle informazioni avute durante l'esecusione 
 */
void printSummary(filestorage_t* fs);

/**
 * 
 * @brief  funzione che dealloca il FS e tutti i suoi contenuti
 * @param fs: FS da deallocare 
 * @return 0 in caso di successo, -1 in caso di errore 
 *
 */
int deleteFStorage(filestorage_t* fs);

/*********** funzionalità per i threads Worker per gestire le richieste dei Client ***********/

/**
 *
 * @brief funzione di gestione della creazione e apertura del file interno al FS
 * @param fs: FS contenente il file
 * @param fdreq: fd del Client che ha richiesto l'apertura del file
 * @param pathfile: path assoluto del file da aprire  
 * @param flags: flag O_CREATE e O_LOCK 
 * @param pipetoM: fd scrittura della pipe con il thManager per ritornare il fd del Client la cui richiesta di lockFile era stata sospesa
 * @return 0 in caso di successo, 1 in caso di successo con espulsione di un file, -1 in caso di errore (setta errno: EINVAL, EPERM, ENOENT, EACCES)
 *
 */
int openfileH(filestorage_t* fs, int fdreq, char* pathfile, int flags, int pipetoM);

/**
 *
 * @brief funzione di gestione della lettura del file interno al FS
 * @param fs: FS contenente il file
 * @param fdreq: fd del Client che ha richiesto la lettura del file
 * @param pathfile: path assoluto del file da leggere 
 * @return #bytes letti >= 0 in caso di successo, -1 in caso di errore (setta errno: EINVAL, ENOENT, EACCES)
 *
 */
ssize_t readfileH(filestorage_t* fs, int fdreq, char* pathfile);
 
/**
 *
 * @brief funzione di gestione della lettura di n file qualsiasi interni al FS
 * @param fs: FS da dove leggere i file
 * @param fdreq: fd del Client che ha richiesto la lettura dei file
 * @param n: quantità di file da leggere, se n <= 0 o n > fs->nfile vengono letti tutti i file interni al FS
 * @param nFilesR: numero di file effettivamente letti
 * @return #bytes letti >= 0 in caso di successo, -1 in caso di errore (setta errno: EINVAL)
 *
 */
ssize_t readnfilesH(filestorage_t* fs, int fdreq, int n, int *nFilesR);

/**
 *
 * @brief funzione di gestione della prima scrittura del file interno al FS, permessa solo dopo la openfile con flag O_CREATE|O_LOCK dal fd che ne ha richiesto la creazione
 * @param fs: FS contenente il file su cui scrivere
 * @param fdreq: fd del Client che ha richiesto la scrittura sul file
 * @param pathfile: path assoluto del file in cui scrivere 
 * @param content: contenuto da scrivere sul file
 * @param size: quantità di byte da scrivere
 * @param fileExpell: lista di file spulsi dal rimpiazzamento
 * @param pipetoM: fd scrittura della pipe con il thManager per ritornare il fd del Client la cui richiesta di lockFile era stata sospesa (x replacement)
 * @return #file espulsi >= 0 in caso di successo, -1 in caso di errore (setta errno: EINVAL, ENOENT, E2BIG, EACCES)
 *
 */
int writefileH(filestorage_t* fs, int fdreq, char* pathfile, const char* content, const size_t size, file_t** filesExpell, int pipetoM);

/**
 *
 * @brief funzione di gestione della scrittura (in append) del file interno al FS
 * @param fs: FS contenente il file
 * @param fdreq: fd del Client che ha richiesto la scrittura sul file
 * @param pathfile: path assoluto del file in cui scrivere 
 * @param content: contenuto da scrivere sul file (in append)
 * @param size: quantità di byte da scrivere (in append)
 * @param fileExpell: lista di file spulsi dal rimpiazzamento
 * @param pipetoM: fd scrittura della pipe con il thManager per ritornare il fd del Client la cui richiesta di lockFile era stata sospesa (x replacement)
 * @return #file espulsi >= 0 in caso di successo, -1 in caso di errore (setta errno: EINVAL, ENOENT, EACCES, E2BIG)
 *
 */
int appendtofileH(filestorage_t* fs, int fdreq, char* pathfile, const char* content, const size_t size, file_t** filesExpell, int pipetoM);

/**
 *
 * @brief funzione di gestione dell'acquisizione della ME sul file interno al FS - setta il flag olock 
 * 	Se il file è bloccato da un altro client, il fd del Client viene messo nella lista dei fd in attesa di acquisire la ME sul file
 * @param fs: FS contenente il file
 * @param fdreq: fd del Client che ha richiesto l'acquisizione della lock sul file
 * @param pathfile: path assoluto del file su cui acquisire la lock  
 * @return 0 in caso di successo, 1 in caso di file già bloccato, -1 in caso di errore (setta errno: EINVAL, ENOENT, EACCES)
 *
 */
int lockfileH(filestorage_t* fs, int fdreq, char* pathfile);

/**
 *
 * @brief funzione di gestione del rilascio della lock sul file interno al FS - passa la Me sul fil al fd successivo nella lista
 * @param fs: FS contenente il file
 * @param fdreq: fd del Client che ha richiesto il rilascio della lock sul file
 * @param pathfile: path assoluto del file da su cui rilasciare la lock 
 * @return fd >= 0 contenente il fd del client che ha acquisito la ME sul file in caso di successo, -1 in caso di errore (setta errno: EINVAL, ENOENT, EACCES)
 *
 */
int unlockfileH(filestorage_t* fs, int fdreq, char* pathfile);

/**
 *
 * @brief funzione di gestione della chiusura del file interno al FS relativamente al fd del Client che ne ha fatto richiesta
 * @param fs: FS contenente il file
 * @param fdreq: fd del Client che ha richiesto la chiusura del file
 * @param pathfile: path assoluto del file da chiudere  
 * @return 0 in caso di successo, -1 in caso di errore (setta errno: EINVAL, ENOENT)
 *
 */
int closefileH(filestorage_t* fs, int fdreq, char* pathfile);

/**
 *
 * @brief funzione di gestione della rimozione del file interno al FS
 * @param fs: FS contenente il file
 * @param fdreq: fd del Client che ha richiesto l'apertura del file
 * @param pathfile: path assoluto del file da rimuovere
 * @param pipetoM: fd scrittura della pipe con il thManager per ritornare il fd del Client la cui richiesta di lockFile era stata sospesa
 * @return 0 in caso di successo, -1 in caso di errore (setta errno: EINVAL, ENOENT, EACCES)
 *
 */
int removefileH(filestorage_t* fs, int fdreq, char* pathfile, int pipetoM);

/**
 *
 * @brief funzione di gestione della chiusura della connesione del Client. resetta il flag olock sul file bloccato
 * @param fs: FS contenente i file
 * @param fdreq: fd del Client di cui va chiusa la connessione
 * @param pipetoM: fd scrittura della pipe con il thManager per ritornare il fd del Client la cui richiesta di lockFile era stata sospesa
 * @return 0 in caso di successo, -1 in caso di errore (setta errno: EINVAL)
 *
 */
int closeconnectionH(filestorage_t* fs, int fdreq, int pipetoM);

/**
 *
 * @brief procedura che ripristina stato FS a prima dell openFile se writeFile non ha avuto successo lato server o lato client
 * @param fs: FS contenente il file
 * @param fdreq: fd del Client richiedente
 * @param pathfile: path del file la cui write non ha avuto successo ma ne è stata fatta la openfile
 *
 */
void restorefsH(filestorage_t* fs, int fdreq, char* pathfile);
#endif /* FILE_STORAGE_H_ */
