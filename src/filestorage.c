#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <filestorage.h>
#include <icl_hash.h>
#include <math.h>
#include <limits.h>
#include <util.h>
#include <conn.h>
#include <workerserver.h>
#include <loggingserver.h>

/**
 * @file filestorage.c
 * @brief File di implementazione dell'interfaccia per il File Storage Server 
 *
 */

/*********** operazioni per manipolazione delle liste di fd_t ************/

/**
 *
 * @brief funzione che controlla se un fd appartiene alla lista di fd_t - l'accesso in mutua esclusione sul file è garantito dal chiamante -
 * @param fdlist: lista di fd in cui cercare fd
 * @param fd: fd da cercare nella lista di fd_t
 * @return 1 in caso in cui lo trova, 0 in caso in cui non lo trova
 *
 */
static int containsFdList(fd_t* fdlist, int fd){
	fd_t* tmp = fdlist;
	while(tmp){
		if(tmp->fd == fd)
			return 1;
		tmp = tmp->next;
	}
	return 0;
}

/**
 *
 * @brief funzione che inserisce in coda un fd nella lista di fd_t - l'accesso in mutua esclusione sul file è garantito dal chiamante
 * @param fdList: lista di fd in cui aggiungere un fd
 * @param fd: fd di Client da inserire nella lista di fd_t
 * @return 0 in caso di successo, -1 in caso di errore
 *
 */
static int addFdList(fd_t** fdlist, int fd){
	if(fd <= 0)
		return -1;
	else if(!containsFdList(*fdlist, fd)){
		fd_t* fdnode = malloc(sizeof(fd_t));
		CHECK_EQ_RETURN(fdnode, NULL, "malloc", -1);
		fdnode->fd = fd;
		fdnode->next = NULL;
		//aggiungo fd in coda
		fd_t* tmp;
		if(!(*fdlist)) //lista vuota
			*fdlist = fdnode;
		else{
			tmp = *fdlist;
			while(tmp->next)
				tmp = tmp->next;
			tmp->next = fdnode;
		}
	}
	return 0;
}

/**
 *
 * @brief procedura che rimuove un fd dalla lista di fd_t - l'accesso in mutua esclusione sul file è garantito dal chiamante -
 * @param fdList: lista di fd in cui rimuovere un fd
 * @param fd: fd di Client da rimuovere nella lista di fd_t se != -1, altrimenti si rimuove dalla testa
 * @return valore > 0 rappresentante del fd estratto dalla testa della lista, 0 se non è presente fd specificato, -1 se la lista è vuota
 *
 */
static int removeFdList(fd_t** fdlist, int fd){
	if(!(*fdlist)) //lista vuota
		return -1;
		
	fd_t* fdnode = NULL;
	int fdRemoved = 0;
	if(fd != -1){
		//togliere fd specificato (in fdopening)
		fd_t* fdcorr = *fdlist;
		fd_t* fdprev = NULL;
		while(fdcorr){
			if(fdcorr->fd == fd){
				fdnode = fdcorr;
				if(!fdprev) //testa della lista
					*fdlist = fdcorr->next;
				else
					fdprev->next = fdcorr->next;
				break;
			}
			//avanzo nella lista
			fdprev = fdcorr;
			fdcorr = fdcorr->next;
		}
	}
	else {
		//fd non specificato - viene tolto il fd dalla testa (in fdwaiting)
		fdnode = *fdlist;
		*fdlist = (*fdlist)->next;
	}
	if(fdnode){
		fdRemoved = fdnode->fd;
		free(fdnode);
	}
	//se non è stato trovato fd specificato, il valore di ritorno è 0
	return fdRemoved;
}

/**
 *
 * procedura che elimina un file dalla lista di file
 * @param fs: FSS per avere i puntatori della lista da cui eliminare il file
 * @param file: file da eliminare 
 *
 */
static void removeFileList(filestorage_t* fs, file_t* file){
	
	file_t* corr = fs->head;
	file_t* prev = NULL;
	while(corr && strncmp(corr->path, file->path, strlen(file->path)) != 0){
		prev = corr;
		corr = corr->next;	
	}
	//il file esiste ed è garantito dal chiamante
	if(!prev) //testa della lista
		fs->head = corr->next;
	else
		prev->next = corr->next;
	if(!corr->next) //coda della lista
		fs->tail = prev;
	
}

/************ operazioni per manipolazione della lista di file_t ************/

/**
 *
 * @brief funzione che alloca un file in memoria
 * @param pathfile: path del file da allocare
 * @return puntatore al file in caso di successo, NULL in caso di errore 
 *
 */
static file_t* allocFile(const char* pathfile){
	file_t* f = calloc(sizeof(file_t), 1);
	CHECK_EQ_RETURN(f, NULL, "calloc", NULL);
	CHECK_EQ_RETURN(f->path = calloc(sizeof(char)*strlen(pathfile) + 1, 1), NULL, "calloc", NULL);
	//memset(f->path, '\0', sizeof(*f->path));
	strncpy(f->path, pathfile, strlen(pathfile));
	if(pthread_mutex_init(&(f->mtx), NULL) != 0){
		fprintf(stderr,"pthread_mutex_init\n");
		free(f->path);
		free(f);
		return NULL;
	}
	if(pthread_cond_init(&(f->condrw), NULL) != 0){
		fprintf(stderr, "pthread_cond_init\n");
		free(f->path);
		free(f);
		if(&(f->mtx)) pthread_mutex_destroy(&(f->mtx));
		return NULL;
	}
	f->next = NULL;
	f->ocreate = -1;
	f->olock = -1;
	f->fdopening = NULL;
	f->fdwaiting = NULL;
	f->referencetime = clock(); //per LRU
	f->referencecount = 0;
	f->nreaders = 0;
	return f;
}
static void deallocFile(file_t* file){
	if(!file) return;
	if(&(file->mtx)) pthread_mutex_destroy(&(file->mtx));
	if(&(file->condrw)) pthread_cond_destroy(&(file->condrw));
	free(file->path);
	free(file);
}
/**
 *
 * @brief funzione che aggiunge un file al FS - l'accesso in mutua esclusione al FS è garantito dal chiamante
 * @param fs: FS in cui aggiungere il file
 * @param file: file da aggiungere al Fs
 *
 */
static void addFile(filestorage_t* fs, file_t* file){
	//aggiungo file in coda alla lista
	if(!fs->head)
		fs->head = file;
	else	fs->tail->next = file;
	fs->tail = file;
	
	//aggiungo file anche nella hash table
	CHECK_EQ_EXIT(icl_hash_insert(fs->filetable, file->path, file), NULL, "icl_hash_insert");	
	
	//aggiorno dati sulla quantità attuale
	fs->nfile++;
	//fs->maxnfile = (fs->maxnfile > fs->nfile) ? fs->maxnfile : fs->nfile;//meglio quando il file viene effettivamento scritto sul FS
}
 
/**
 *
 * @brief procedura che elimina un file dal FS - l'accesso in mutua esclusione al FS è garantito dal chiamante.
 * @param fs: FileStorage contenente il file da eliminare
 * @param file: file da eliminare
 * @param notify: indica se si deve notificare il Client in attesa della ME sul file che verrà cancellato
 *			se >0 è fd di scrittura della pipe con il thManager a cui ritornare il fd del client sbloccato
 * @param dealloc: indica se si deve deallocare memoria del path e del contenuto del file.
 *			non settato in replacement per mantenere il file da inviare al client
 *
 */
static void deleteFile(filestorage_t* fs, file_t* file, int notify, int dealloc){
	ssize_t nw;
	int err;
		
	//per eliminare un file accedere al file in mutua esclusione
	LOCK(&(file->mtx));
	
	while(file->nreaders > 0)
		WAIT(&(file->condrw), &(file->mtx));
	removeFileList(fs, file);
	//cancello il file pure dalla hash table
	CHECK_EQ_EXIT(icl_hash_delete(fs->filetable, file->path, NULL, NULL), -1, "icl_hash_delete");
		
	fs->nfile--;
	fs->size -= file->sizefile;	
	//libero lista di fd che hanno aperto il file
	while(file->fdopening){
		fd_t* tmp = file->fdopening;
		file->fdopening = file->fdopening->next;
		free(tmp);
	}
	//libero lista di fd che attendono la ME sul file
	while(file->fdwaiting){
		fd_t* tmp = file->fdwaiting;
		if(notify > 0){
			//si invia un msg al fd del Client in attesa di acquisire la ME sul file che tale file è stato rimosso
			err = FILEREMOVED;
			sendMsgME(tmp->fd, err, file->path);
			//ritorno al Manager il fd del Client la cui richiesta di lockFile era stata sospesa
			SC_EXIT(nw, write(notify, &tmp->fd, sizeof(tmp->fd)), "write"); 
		}
		file->fdwaiting = file->fdwaiting->next;
		free(tmp);
	}
		
	//logLock("UnlockpreFD %p %ld\n", file->mtx, pthread_self());
	UNLOCK(&(file->mtx));
	//logLock("UnlockpostFD %p %ld\n", file->mtx, pthread_self());
	
	if(&(file->mtx)) pthread_mutex_destroy(&(file->mtx));	
	if(&(file->condrw)) pthread_cond_destroy(&(file->condrw));
	//si dealloca memoria del path e del contenuto del file se indicato nel flag
	if(dealloc && file){
		free(file->path);
		free(file->content);
		free(file);
	}
}

/************ operazioni per manipolazione FileStorage ************/

filestorage_t* initFStorage(size_t maxfile, size_t maxcap, size_t policy){
	//alloco spazio per file storage e suoi campi
	filestorage_t* storage = calloc(sizeof(filestorage_t), 1);
	CHECK_EQ_RETURN(storage, NULL, "calloc", NULL);
	
	//determinando il fattore di carico, rapporto tra maxfile e #buckets della hash,
	//come valore ottimale a 0.75 per avere un buon compromesso tra costo in spazio e in tempo
	long int nbuckets = lround(maxfile/0.75);
	storage->filetable = icl_hash_create((long int)nbuckets, NULL, NULL); 
	if(!storage->filetable){
		free(storage);
		return NULL;
	}
	//inizializzo campi
	if(pthread_mutex_init(&(storage->mtx), NULL) != 0){
		perror("pthread_mutex_init");
		free(storage->filetable);
		free(storage);
		return NULL;
	}
	storage->atmostfile = maxfile;
	storage->atmostsize = maxcap;
	storage->size = 0;
	storage->nfile = 0;
	storage->maxsize = 0;
	storage->maxnfile = 0;
	storage->replacepolicy = policy;
	storage->head = NULL;
	storage->tail = NULL;
	return storage;
}

static void printFileinFS(filestorage_t* fs){
	printf("File presenti attualmente nel File Storage Server : %zu\n", fs->nfile);
	file_t* tmp = fs->head;
	while(tmp){
		printf("\t- %s\n", tmp->path);
		tmp = tmp->next;
	}
	printf("---------------------------------------------------------------------------\n");
}
	
/**
 * @brief procedura per stampare a termine dell'esecuzione del server un riepilogo delle informazioni avute durante l'esecusione 
 */
void printSummary(filestorage_t* fs){
	printf("\n----------| Informazioni raggiunte a fine esecuzione del Server |----------\n");
	printf("Numero di file massimo memorizzato nel Server : %zu\n", fs->maxnfile);
	printf("Dimensione massima raggiunta dal File Storage : %zu\n", fs->maxsize);
	printf("Numero di volte in cui l'algoritmo di rimpiazzamento è stato utilizzato : %zu\n", fs->countreplace);
	
	printFileinFS(fs);
}

int deleteFStorage(filestorage_t* fs){
	if(!fs){
		return -1;
	}
	
	file_t* tmp;
	while(fs->head){
		tmp = fs->head;
		//fs->head = fs->head->next;
		deleteFile(fs, tmp, 0, 1);
	}
	
	CHECK_EQ_RETURN(icl_hash_destroy(fs->filetable, NULL, NULL), -1, "icl_hash_destroy", -1);
	
	if(&(fs->mtx)) pthread_mutex_destroy(&(fs->mtx));
	free(fs);
	return 0;
}

/************ funzionalità ausiliarie alla gestione delle richieste dei Client ************/

/**
 *
 * @brief funzione che seleziona il file da espellere dal FS tramite la politica di rimpiazzamento indicata nel FS
 * @param fs: FS da cui selezionare il file da espellere, utilizzando la politica di rimpiazzamento definita dal campo replacepolicy in FS
 * @param pathfile: path assoluto del file la cui scrittura provoca "capacity misses", per cui il file da espellere non dovrà essere tale
 * @return path del file espulso
 */
static file_t* getFileToExpell(filestorage_t* fs, const char* pathfile){
	clock_t timemax = clock(); //tempo massimo per LRU
	//time(&timemax);
	int refcount = INT_MAX; //contatore riferimento massimo per LFU
	file_t* tmp = fs->head;
	file_t* fileToReplace = (file_t*) malloc(sizeof(file_t));
	CHECK_EQ_RETURN(fileToReplace, NULL, "malloc", NULL);
						
	if(fs->replacepolicy == 0){
		//FIFO - prendo il file da rimuovere dalla testa poichè inserito per primo
		if(strncmp(tmp->path, pathfile, strlen(pathfile)) == 0)
			tmp = tmp->next; //se il file in testa è quello oggetto di scrittura si salta al successivo
		memcpy(fileToReplace, tmp, sizeof(*tmp));
		//aggiorno il contatore delle volte in cui si fa il rimpiazzamento
		fs->countreplace += 1;
		return fileToReplace; //ritorno subito il file estratto con FIFO
	}
	//altrimenti itero e controllo il campo per attuare la politica richiesta	
	while(tmp){
		switch(fs->replacepolicy){
		case 1:{//LRU
			if(tmp->referencetime < timemax && strncmp(tmp->path, pathfile, strlen(pathfile)) != 0){
				timemax = tmp->referencetime;
				memcpy(fileToReplace, tmp, sizeof(*tmp));
			}
		}
		break;
		case 2:{//LFU
			if(tmp->referencecount < refcount && strncmp(tmp->path, pathfile, strlen(pathfile)) != 0){
				refcount = tmp->referencecount;
				memcpy(fileToReplace, tmp, sizeof(*tmp));
			}
		}
		break;
		default:;
		}
		tmp = tmp->next;
	}	
	//aggiorno il contatore delle volte in cui si fa il rimpiazzamento
	fs->countreplace += 1;
	return fileToReplace;
}
/**
 * @brief funzione che effettua il rimpiazzamento dei file a seguito di "capacity misses" e l'invio di tali file espulsi al Client la cui richiesta ha causato ciò
 * @param fs: FS da cui espellere i file 
 * @param fdreq: fd del Client che ha effettuato una richiesta provocando "capacity misses"
 * @param pathfile: path assoluto del file su cui il Client ha fatto la richiesta di scrittura
 * @param size: dimensione del contenuto da scrivere che potrebbe causare "capacity misses" della cache del Server
 *
 */
static int replacement(filestorage_t* fs, int fdreq, char* pathfile, size_t size, int* nExpell, file_t** filesExpell, int pipetoM){
	int fileToSend = 0; //#file espulsi da rinviare al Client la cui richiesta ne ha causato l'espulsione
	//ciclo selezionando i file da espellere, salvandoli nella lista passata come parametro e poi eliminandoli dal Filestorage
	//fino ad ottenere lo spazio necessario per la 'size ' da aggiungere
	while(fs->size + size > fs->atmostsize){
		file_t* fileToExpell = getFileToExpell(fs, pathfile); 
		if(!fileToExpell){
			return -1;
		}
		//inserisco questo file in testa nella lista da ritornare
		fileToExpell->next = *filesExpell;
		*filesExpell = fileToExpell;
		//non deallocare memoria alla struttura del file, verrà deallocata dopo aver mandato il file espulso al client
		deleteFile(fs, fileToExpell, pipetoM, 0);
		fileToSend++;
	}
	*nExpell = fileToSend;
	
	return 0;
}

/************ funzionalità per i threads Worker per gestire le richieste dei Client ************/

int openfileH(filestorage_t* fs, int fdreq, char* pathfile, int flags,int pipetoM){
	if(!fs || fdreq <= 0 || !strlen(pathfile)){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	int create = (O_CREATE & flags);
	int lock = (O_LOCK & flags);	
	int exist;
	int ret = 0; //nessun file è stato eliminato
	
	LOCK(&(fs->mtx)); //lock su intero FileStorage 
	
	//si cerca se esiste già il file
	file_t* file = (file_t*) icl_hash_find(fs->filetable, (void*)pathfile);
	exist = (file != NULL) ? 1 : 0;
	errno = 0;	
	//caso ERRORE: (exist == 1 && create == 1) || (exist == 0 && create == 0)
	if(exist == create){
		UNLOCK(&(fs->mtx));
		errno = (exist == 1) ? EEXIST : ENOENT;
		return -1;
	}
	else{ //caso SUCCESSO
		if(!exist){ //creazione (nel FS)
			//capacià del FS raggiunta -> si elimina un file
			if(fs->nfile == fs->atmostfile){ 
				//selezionare file da espellere secondo la politica di rimpiazzamento
				file_t* fileToExpell = getFileToExpell(fs, pathfile);
				//il file espulso viene cancellato senza tornarlo al Client
				//nella fuzione openFile della API non è prevista come parametro la directory di salvataggio
				deleteFile(fs, fileToExpell, pipetoM, 1);
				ret = 1; //un file è stato eliminato
			}
			file = allocFile(pathfile);
			if(!file){
				UNLOCK(&(fs->mtx));
				errno = ENOENT; //problema nell'allocazione file
				return -1;
			}
			if(lock){
				file->olock = fdreq;
				file->ocreate = fdreq; //con openFile(O_CREATE|O_LOCK) con successo può seguire immediatamente writeFile
			}
			//fdreq ha creato ed aperto il file, lo inserisco nella lista descrittori che lo hanno aperto
			CHECK_EQ_EXIT(addFdList(&(file->fdopening), fdreq), -1, "addFdList in fdopening");				
			addFile(fs, file);
			UNLOCK(&(fs->mtx));
	
		}
		else{ //apertura 
			LOCK(&(file->mtx)); //lock su file da aprire
			
			UNLOCK(&(fs->mtx)); //posso lasciare la lock sul FS una volta acquisita la lock sul file da aprire
			
			if(lock){
				if(file->olock == -1) //file unlocked
					file->olock = fdreq;
				else{ 	//file messo in locked da qualcun altro
					UNLOCK(&(file->mtx));
					errno = EACCES; //accesso negato
					return -1;
				}
			}
			//fdreq ha aperto il file, lo inserisco nella lista
			CHECK_EQ_EXIT(addFdList(&(file->fdopening), fdreq), -1, "addFdList in fdopening");
			UNLOCK(&(file->mtx));		
		}
	}	
	return ret;
}

ssize_t readfileH(filestorage_t* fs, int fdreq, char* pathfile){
	if(!fs || fdreq <= 0 || !strlen(pathfile)){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	
	LOCK(&(fs->mtx)); //acqusisco la lock sul FS solo per cercare il file da leggere
	
	file_t* file = (file_t*) icl_hash_find(fs->filetable, (void*)pathfile);
	if(!file){
		UNLOCK(&(fs->mtx));
		errno = (!fs->filetable) ? EINVAL : ENOENT;
		return -1;		
	}
	
	LOCK(&(file->mtx));
	
	UNLOCK(&(fs->mtx)); //posso lasciare la lock sul FS una volta acquisita la lock sul file da leggere
	
	//controllo per l'accesso al file: deve averlo aperto e se bloccato deve essere bloccato da fdreq
	if(!containsFdList(file->fdopening, fdreq) || (file->olock != -1 && file->olock != fdreq)){
		UNLOCK(&(file->mtx));
		errno = EACCES; //accesso negato
		return -1;
	}
	file->nreaders++;
	UNLOCK(&(file->mtx)); 
	//è possibile effettuare più letture in contemporanea
	
	ssize_t n;
	int retOK = OK;
	SC_EXIT(n, writen(fdreq, &retOK, sizeof(int)), "writenS518");
	//si invia al client il numero di bytes letti che gli verranno inviati
	SC_EXIT(n, writen(fdreq, &file->sizefile, sizeof(size_t)), "writenS520"); 
	//si invia al client la risposta contenente il file
	SC_EXIT(n, writen(fdreq, file->content, file->sizefile), "writenS522");
		
	
	LOCK(&(file->mtx));
	
	file->nreaders--;
	file->referencetime = clock(); //per LRU
	file->referencecount++; //per LFU
	file->ocreate = -1; //ultima op READFILE
	if(file->nreaders == 0)
		SIGNAL(&(file->condrw));
	UNLOCK(&(file->mtx));	
	
	return file->sizefile;
}

ssize_t readnfilesH(filestorage_t* fs, int fdreq, int n, int *nFilesR){
	if(!fs || fdreq <= 0){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	int fileRead = 0;
	size_t byteRead = 0;
	//si leggono al massimo 'n' file (se fs->nflie < 'n') oppure tutti i file se n == 0
	int fileToRead = (n == 0 || n > fs->nfile) ? fs->nfile : n;	
	
	LOCK(&(fs->mtx)); //non ci sarà scrittura o cancellazione in contemporanea perchè si detiene il lock sull'intero FS
	ssize_t nw;
	sreturnfile_t retfile;
	memset(&retfile, 0, sizeof(sreturnfile_t));
	
	//si invia al client il numero di file che verrano letti e spediti
	SC_EXIT(nw, writen(fdreq, &fileToRead, sizeof(int)), "writenS553"); 
	file_t* reading = fs->head;
	while(reading && fileRead < fileToRead){ 
		//si legge il file corrente solo se unlocked o locked dal richiedente, altrimenti si passa al successivo
		if(reading->olock == -1 || reading->olock == fdreq){ 
			reading->nreaders++;		
			
			//lettura del file -> invio al client
			retfile.lenpath = strlen(reading->path);
			retfile.contentsize = reading->sizefile;
			//si invia al client la coppia lunghezza path e dimensione file
			SC_EXIT(nw, writen(fdreq, &retfile, sizeof(retfile)), "writenS564"); 
			//si invia al client il path del file
			SC_EXIT(nw, writen(fdreq, reading->path, strlen(reading->path)), "writenS566");
			//si invia al client il file letto
			SC_EXIT(nw, writen(fdreq, reading->content, reading->sizefile), "writenS568");
			byteRead += reading->sizefile;
			reading->referencetime = clock(); //per LRU
			reading->referencecount++; //per LFU
			reading->nreaders--;
			fileRead++;
		}
		//scorro la lista di file nel fs
		reading = reading->next;
	}
	*nFilesR = fileRead;
	UNLOCK(&(fs->mtx));
	
	return byteRead;
}

int writefileH(filestorage_t* fs, int fdreq, char* pathfile, const char* content, const size_t size, file_t** filesExpell, int pipetoM){
	if(!fs || fdreq <= 0 || !strlen(pathfile)){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	int nExpell = 0;
	
	LOCK(&(fs->mtx)); //lock su intero FileStorage  per evitare cancellazione ed altre scritture
	
	file_t* file = (file_t*) icl_hash_find(fs->filetable, (void*)pathfile);
	if(!file){
		UNLOCK(&(fs->mtx));
		errno = (!fs->filetable) ? EINVAL : ENOENT;
		return -1;
	}
	
	LOCK(&(file->mtx));
	
	//la prima scrittura può avvenire solo se l'ultima operazione è la OPENFILE, garantita se il creatore è fdreq e ne detiene la lock
	if(file->ocreate == fdreq && file->olock == fdreq){
		if(size > fs->atmostsize){
			UNLOCK(&(file->mtx));
			UNLOCK(&(fs->mtx));
			errno = E2BIG; //dimensione contenuto troppo grande
			return -1;
		}
		
		//si può scrivere se non ci sono lettori attivi sul file
		while(file->nreaders > 0)
			WAIT(&(file->condrw), &(file->mtx));
			
		//controllo per eventuale capacity misses
		CHECK_EQ_EXIT(replacement(fs, fdreq, pathfile, size, &nExpell, filesExpell, pipetoM), -1, "replacement");
		CHECK_EQ_EXIT(file->content = calloc(sizeof(char)*size+1, 1), NULL, "calloc");
		memcpy(file->content, content, size);
		file->sizefile = size;
		file->referencetime = clock(); //per LRU
		file->referencecount++; //per LFU
		file->ocreate = -1; //ultima op WRITEFILE
		fs->size += size;
		fs->maxsize = (fs->maxsize > fs->size) ? fs->maxsize : fs->size;
		fs->maxnfile = (fs->maxnfile > fs->nfile) ? fs->maxnfile : fs->nfile;
	}
	else{
		UNLOCK(&(file->mtx));
		UNLOCK(&(fs->mtx));
		errno = EACCES; //non si può effettuare la prima scrittura sul file
		return -1;
	}
	UNLOCK(&(file->mtx));
	UNLOCK(&(fs->mtx));
		
	return nExpell;
}

int appendtofileH(filestorage_t* fs, int fdreq, char* pathfile, const char* content, const size_t size, file_t** filesExpell, int pipetoM){
	if(!fs || fdreq <= 0 || !strlen(pathfile)){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	int nExpell = 0;
	
	LOCK(&(fs->mtx)); //lock su intero FileStorage  per evitare cancellazione ed altre scritture
	file_t* file = (file_t*) icl_hash_find(fs->filetable, (void*)pathfile);
	if(!file){
		UNLOCK(&(fs->mtx));
		errno = (!fs->filetable) ? EINVAL : ENOENT;
		return -1;
	}	
	
	LOCK(&(file->mtx));
	//controllo per l'accesso al file : deve averlo aperto e se bloccato deve averlo bloccato fdreq 
	if(!containsFdList(file->fdopening, fdreq) || (file->olock != -1 && file->olock != fdreq)){
		UNLOCK(&(file->mtx));
		UNLOCK(&(fs->mtx));
		errno = EACCES;
		return -1;
	}
	if(size > fs->atmostsize){
		UNLOCK(&(file->mtx));
		UNLOCK(&(fs->mtx));
		errno = E2BIG; //dimensione contenuto troppo grande
		return -1;
	}
	
	while(file->nreaders > 0)
		WAIT(&(file->condrw), &(file->mtx));
		
	//controllo per eventuale capacity misses
	CHECK_EQ_EXIT(replacement(fs, fdreq, pathfile, size, &nExpell, filesExpell, pipetoM), -1, "replacement");
	//buffer per salvataggio della concatenazione dei contenuti
	char* buffer = calloc(sizeof(char)*(file->sizefile + size) + 1, 1);
	CHECK_EQ_EXIT(buffer, NULL, "calloc");
	memcpy(buffer, file->content, file->sizefile); //salvataggio primo contenuto del file
	memcpy(buffer + file->sizefile, content, size); //salvataggio nuovo contenuto in append dopo sizefile già scritto
	free(file->content);
	CHECK_EQ_EXIT(file->content = calloc(sizeof(char)*(file->sizefile + size) + 1, 1), NULL, "calloc");
	memcpy(file->content, buffer, strlen(buffer));
	file->sizefile = strlen(buffer);
	free(buffer);
	file->referencetime = clock(); //per LRU
	file->referencecount++; //per LFU
	file->ocreate = -1; //ultima op APPENDFILE
	//dimensione FS aggiornata
	fs->size += size;
	fs->maxsize = (fs->maxsize > fs->size) ? fs->maxsize : fs->size;
	
	UNLOCK(&(file->mtx));	
	UNLOCK(&(fs->mtx));	
	
	return nExpell;
}

int lockfileH(filestorage_t* fs, int fdreq, char* pathfile){
	if(!fs || !strlen(pathfile) || fdreq <= 0){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	
	LOCK(&(fs->mtx)); 
	file_t* file = (file_t*) icl_hash_find(fs->filetable, (void*)pathfile);
	if(!file){
		UNLOCK(&(fs->mtx));
		errno = (!fs->filetable) ? EINVAL : ENOENT;
		return -1;
	}	
	LOCK(&(file->mtx));
	UNLOCK(&(fs->mtx)); //una volta acquisito il lock sul file da bloccare, posso rilasciare la lock sul fs
	
	//controllo se il file è messo in locked da un'altro richiedente
	if(file->olock != -1 && file->olock != fdreq){
		 //inserisco il richiedente nella lista di client in attesa di acquisire la lock su tale file
		 CHECK_EQ_EXIT(addFdList(&(file->fdwaiting), fdreq), -1, "addFdList in fdwaiting");
		 UNLOCK(&(file->mtx));
		 return 1;
	}
	//file era unlocked, il richiedente acquisisce la ME sul file
	file->olock = fdreq;
	file->ocreate = -1; //ultima op LOCKFILE
	UNLOCK(&(file->mtx));
	return 0;
}

int unlockfileH(filestorage_t* fs, int fdreq, char* pathfile){
	if(!fs || !strlen(pathfile) || fdreq <= 0){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	int fdlock = 0;
	LOCK(&(fs->mtx)); 
	file_t* file = (file_t*) icl_hash_find(fs->filetable, (void*)pathfile);
	if(!file){
		UNLOCK(&(fs->mtx));
		errno = (!fs->filetable) ? EINVAL : ENOENT;
		return -1;
	}
	LOCK(&(file->mtx));
	UNLOCK(&(fs->mtx)); //una volta acquisito il lock sul file da sbloccare, posso rilasciare la lock sul FS
	
	//controllare se il richiedente è colui che detiene la lock
	if(file->olock != -1 && file->olock != fdreq){
		UNLOCK(&(file->mtx));
		errno = EACCES;
		return -1;
	}
	
	file->olock = removeFdList(&(file->fdwaiting), -1); //viene data la ME sul file al fd in testa alla lista
	//se file->olock == -1 significa che non c'era nessuno in attesa di acquisire la ME sul file e tale file è unlocked
	fdlock = (file->olock == -1) ? 0 : file->olock;
	UNLOCK(&(file->mtx));
	
	return fdlock;
}

int closefileH(filestorage_t* fs, int fdreq, char* pathfile){
	if(!fs || fdreq <= 0 || !strlen(pathfile)){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	int fdlock = 0;
	
	LOCK(&(fs->mtx));
	
	errno = 0;	
	file_t* file = (file_t*) icl_hash_find(fs->filetable, (void*)pathfile);
	if(!file){
		UNLOCK(&(fs->mtx));
		errno = (!fs->filetable) ? EINVAL : ENOENT;
		return -1;
	}
	LOCK(&(file->mtx));
	
	UNLOCK(&(fs->mtx));
	
	if(!containsFdList(file->fdopening, fdreq) || (file->olock != -1 && file->olock != fdreq)){
		UNLOCK(&(file->mtx));
		errno = EACCES;
		return -1;
	}
	
	//controllo se il fdreq detiene la lock sul file, in tal caso si passa la ME sul file al prossimo fd in lista
	if(file->olock == fdreq){
		file->olock = removeFdList(&(file->fdwaiting), -1); //viene data la ME sul file al fd in testa alla lista
		//se file->olock == -1 significa che non c'era nessuno in attesa di acquisire la ME sul file e tale file è unlocked
		fdlock = (file->olock == -1) ? 0 : file->olock;
	}
	
	//chiusura del file da parte del fd richiedente
	int fd = removeFdList(&(file->fdopening), fdreq);
	CHECK_EQ_EXIT(fd, -1, "removeFdList in fdopening");
	CHECK_EQ_EXIT(fd, 0, "removeFdList in fdopening");
	UNLOCK(&(file->mtx));
	
	return fdlock;
}

int removefileH(filestorage_t* fs, int fdreq, char* pathfile, int pipetoM){
	if(!fs || fdreq <= 0 || !strlen(pathfile)){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	
	LOCK(&(fs->mtx)); //lock su intero FileStorage per rimuovere il file sia dalla lista che dalla icl_hash
	errno = 0;
	file_t* file = (file_t*) icl_hash_find(fs->filetable, (void*)pathfile);
	if(!file){
		UNLOCK(&(fs->mtx));
		errno = (!fs->filetable) ? EINVAL : ENOENT;
		return -1;
	}
	//il file per essere rimosso deve essere unlocked o locked da fdreq
	if(file->olock != -1 && file->olock != fdreq){
		UNLOCK(&(fs->mtx));
		errno = EACCES;
		return -1;
	}	
	deleteFile(fs, file, pipetoM, 1);
	UNLOCK(&(fs->mtx));
	
	return 0;
}

int closeconnectionH(filestorage_t* fs, int fdreq, int pipetoM){
	if(!fs || fdreq <= 0){
		errno = EINVAL; //argomenti non validi
		return -1;
	}
	ssize_t nw;
	int fdlock; //fd Client in attesa della ME sul file
	LOCK(&(fs->mtx));
	
	errno = 0;
	//il client deve rilasciare la lock su tutti i file che ha bloccato
	file_t* file = fs->head;
	while(file){
		LOCK(&(file->mtx));
		//rilasciare la ME sul file
		if(file->olock == fdreq){
			file->olock = removeFdList(&(file->fdwaiting), -1); //viene data la ME sul file a fd in testa alla lista
			//se file->olock == -1 significa che non c'era nessuno in attesa di acquisire la ME sul file e tale file è unlocked
			if((fdlock = file->olock) != -1)
				sendMsgME(fdlock, OK, file->path);
				//ritorno al Manager il fd del Client la cui richiesta di lockFile era stata sospesa
				SC_EXIT(nw, write(pipetoM, &fdlock, sizeof(fdlock)), "write"); 
		}
		//chiusura del file da parte del fd che richiede la chiusura della connessione
		removeFdList(&(file->fdopening), fdreq);
		UNLOCK(&(file->mtx));
		file = file->next;
	}
	UNLOCK(&(fs->mtx));
	return 0;	
}

void restorefsH(filestorage_t* fs, int fdreq, char* pathfile){
	if(!fs || fdreq <= 0 || !strlen(pathfile))
		return;
	LOCK(&(fs->mtx));
	file_t* file = (file_t*) icl_hash_find(fs->filetable, (void*)pathfile);
	if(!file){
		UNLOCK(&(fs->mtx));
		return;
	}	
	//deve essere annullata la fase di openfile (creazione)
	//cancello il file pure dalla hash table
	CHECK_EQ_EXIT(icl_hash_delete(fs->filetable, file->path, NULL, NULL), -1, "icl_hash_delete");
	removeFileList(fs, file);
	removeFdList(&(file->fdopening), fdreq);
	deallocFile(file);
	fs->nfile--;
	UNLOCK(&(fs->mtx));
}
