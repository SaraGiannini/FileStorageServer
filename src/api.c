#define _DEFAULT_SOURCE //per usleep
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <api.h>
#include <util.h>
#include <conn.h>

/**
 *
 * @file api.c
 * @brief File di implementazione dell'interfaccia Client per interagire con il FileStorage Server
 *
 */
 
int fdsock;


/** il Client riceve dal server questi codici di errore
 *  e ne stampa un messaggio significativo */
void printMsgErr(int code){
	switch(code){
	case NOSUCHFILE :
		fprintf(stderr, "\tERR: File non trovato\n");
		break;
	case FILETOOBIG :
		fprintf(stderr, "\tERR: Dimensioni file troppo grandi\n");
		break;
	case INVALIDARGS : 
		fprintf(stderr, "\tERR: Argomenti non validi\n");
		break;
	case ALREADYEXIST : 
		fprintf(stderr, "\tERR: File già esistente, impossibile crearlo nuovamente\n");
		break;
	case PERMDENIED : 
		fprintf(stderr, "\tERR: Accesso al file negato\n");
		break;
	case FILEREMOVED : 
		fprintf(stderr, "\tERR: File rimosso\n");
	}
}

/** funzione per modificare il nome del file sostituendo '/' con '_' */
char* replaceChar(char* s, char old, char new){
	char* currchar = strchr(s, old);
	while(currchar){
		*currchar = new;
		currchar = strchr(currchar, old);	
	}
	return s;
}
/** crea la directory se non esiste */
int createDir(const char* dirname){
	errno = 0;
	DIR* dir = opendir(dirname);
	if(!dir || errno == ENOENT){
		//si cre la directory con i permessi dell'utente r,w,x attivi (0700)
		if(mkdir(dirname, S_IRWXU) != 0)
			return -1;
	} 
	else closedir(dir);
	return 0;
}
/** funzione per salvare i file nella directory
 *  sia per file espulsi da scrittura che per file letti dal FSServer */
int saveFile(const char* dirSaving, char* path, void* content, size_t contentsize){
	//creazione della directory di salvataggio dei file espulsi o letti
	if(createDir(dirSaving) != 0)
		return -1;
	//salvataggio della cwd
	char originalPath[PATH_MAX];
	getcwd(originalPath, PATH_MAX);
	//cambio posizione nella directory dirSaving 
	if(chdir(dirSaving) == -1){
		perror("chdir");
		return -1;
	}
	//sostiuisco '/' con '_' nel path assoluto del file
	replaceChar(path, '/', '_');
	
	//salvataggio file in cui scrivere il contenuto ricevuto dal server
	//aperto per leggere e per scrivere, creato se non esiste, altrimenti viene troncato(sovrascritto)
	FILE* file = fopen(path, "w+"); 
	if(!file){
		perror("fopen");
		fprintf(stderr, "C > Apertura file %s in cui salvare il contenuto fallita\n", path);
		chdir(originalPath);
		return -1;
	}
	if(fwrite(content, 1, contentsize, file) <= 0){
		int errnocpy = errno;
		if(ferror(file)){ 
			errno = errnocpy;
			perror("fwrite");
			chdir(originalPath);
			return -1;
		}
	}
	if(fclose(file) != 0){
		fprintf(stderr, "C > Impossibile chiudere il file\n");
		chdir(originalPath);
		return -1;
	}
	//torno alla directory di partenza
	chdir(originalPath);
	return 0;
}

/** funzione che riceve i file spediti dal server a seguito di lettura o espulsione,
 *  in base all'indicazione della directory di salvataggio chiama la saveFile che effettuerà il salvataggio nella directory 
 *  altrimenti non farà nulla
 */
static int receiveFiles(int fd, const char* dirname, int nFiles){
	ssize_t n;
	for(int i = 0; i < nFiles; i++){
		sreturnfile_t retfile;
		memset(&retfile, 0, sizeof(retfile));
		SC_EXIT(n, readn(fd, &retfile, sizeof(retfile)), "readnC127");
		if(dirname){
			char* filepath = calloc(sizeof(char)*retfile.lenpath + 1, 1);
			CHECK_EQ_RETURN(filepath, NULL, "calloc", -1);
			SC_EXIT(n, readn(fd, filepath, retfile.lenpath), "readnC131");
			char* content = calloc(sizeof(char)*retfile.contentsize + 1, 1);
			CHECK_EQ_RETURN(content, NULL, "calloc", -1);
			SC_EXIT(n, readn(fd, content, retfile.contentsize), "readnC134");
			if(saveFile(dirname, filepath, content, retfile.contentsize) == -1){
				free(filepath);
				return -1;
			}
			free(filepath);
			free(content);
		}
	 }
	 return nFiles;
}

/************ implementazione interfaccia API per interagire con il FileStorageServer ************/

int openconnectionAPI(const char *sockname, int msec, const struct timespec abstime){
	if(!strlen(sockname)){
		errno = EINVAL;
		return -1;
	}
	int err;
	int ret = 0;
	struct sockaddr_un sa;
	strncpy(sa.sun_path, sockname, UNIX_PATH_MAX);
	sa.sun_family = AF_UNIX;
	
	struct timespec timecurrent;
	SC_RETURN(err, clock_gettime(CLOCK_REALTIME, &timecurrent), "clock_gettime"); //setta errno
	
	SC_RETURN(fdsock, socket(AF_UNIX, SOCK_STREAM, 0), "socketC"); //setta errno
	errno = 0;
	while((ret = connect(fdsock, (struct sockaddr*)&sa, sizeof(sa))) == -1 && abstime.tv_sec > timecurrent.tv_sec){ //connect setta errno
		if(print) fprintf(stderr, "C > Impossibile connettersi al socket. Si riprova tra %d msec\n", msec);
		usleep(1000*msec);
		ret = 0;
		SC_RETURN(err, clock_gettime(CLOCK_REALTIME, &timecurrent), "clock_gettime"); //setta errno
	}
	if(ret == 0)
		return 0;
	
	//scaduto il tempo abstime
	errno = ETIME;
	return -1;
}

int closeconnectionAPI(const char *sockname){
	if(!sockname){
		errno = EINVAL;
		return -1;
	}
	int err;
	SC_RETURN(err, close(fdsock), "closeC");
	return 0;
}

int openfileAPI(const char* path, int flags){
	if(!path || !strlen(path)){
		errno = EINVAL;
		return -1;
	}
	ssize_t n = 0;
	//RICHIESTA
	crequest_t req;
	memset(&req, 0, sizeof(req));
	req.op = OPENFILE;
	req.lenpath = strlen(path);
	req.flags = flags;
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writenC199");
	SC_EXIT(n, writen(fdsock, (void*)path, strlen(path)), "writenC200");
	
	//RISPOSTA
	int reply;
	SC_RETURN(n, readn(fdsock, &reply, sizeof(reply)), "readnC204");
	if(reply < OK){
		if(print){
			fprintf(stderr, "C > Errore nella gestione della richiesta di OPEN sul file %s\n", path);
			printMsgErr(reply);
		}
		return -1;
	}
	if(print) fprintf(stdout, "C > File %s aperto\n", path);
	if(reply > OK && print)	fprintf(stdout, "C > E' stato rimosso un file nel FileStorageServer\n");
	return 0;
}

int writefileAPI(const char* path, const char* dirSaving){
	if(path == NULL || !strlen(path)){
		errno = EINVAL;
		return -1;
	}
	//si recupera il file per prenderene dimensione e contenuto da inviare al server
	FILE* file = fopen(path, "r");
	if(!file){
		perror("fopen");
		fprintf(stderr, "C > Aperura file %s da cui prendere il contenuto fallita\n", path);
		return -1;	
	}
	int size;
	//si rileva la dimensione del file per poter allocare il buffer per la lettura
	CHECK_EQ_RETURN(fseek(file, 0, SEEK_END), -1, "fseek", -1); //mi sposto alla fine del file
	CHECK_EQ_RETURN(size = ftell(file), -1, "ftell", -1); //ottengo #byte dall'inizio del file
	rewind(file); //torna all'inizio del file 
	char* content = calloc(sizeof(char)*size, 1);
	CHECK_EQ_RETURN(content, NULL, "calloc", -1);
	if(fread(content, sizeof(char), size, file) <= 0){
		int errnocpy = errno;
		if(ferror(file)){  
			errno = errnocpy;
			perror("fread");
			free(content);
			return -1;
		}
	}
	if(fclose(file) != 0){
		fprintf(stderr, "C > Impossibile chiudere il file\n");
		return -1;
	}	
	ssize_t n = 0;
	//RICHIESTA
	crequest_t req;
	memset(&req, 0, sizeof(req));
	req.op = WRITEFILE;
	req.lenpath = strlen(path);
	req.contentsize = size;
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writenC256");
	SC_EXIT(n, writen(fdsock, (void*)path, strlen(path)), "writenC257");
	SC_EXIT(n, writen(fdsock, content, size), "writenC258");
	free(content);
	
	//RISPOSTA
	int reply = 0;
	SC_RETURN(n, readn(fdsock, &reply, sizeof(reply)), "readnC263");
	if(reply < OK){
		if(print){
			fprintf(stderr, "C > Errore nella gestione della richiesta di WRITE del file %s\n", path);
			printMsgErr(reply);
		}
		return -1;	
	}
	if(print) fprintf(stdout, "C > File %s scritto. Scritti %d bytes.\n", path, size);
	if(reply > OK){ // in reply è stato scritto il n file espulsi
		//lettura dei file e salvataggio diretto nella directory se specificata
		int nFilesEx; //n file effettivamente arrivati
		if((nFilesEx = receiveFiles(fdsock, dirSaving, reply)) == -1){
			if(print) fprintf(stderr, "C > Errore nel salvataggio dei file espulsi con WRITE\n");
			return -1;
		}
		if(print) fprintf(stdout, "C > Salvataggio di %d file espulsi effettuato\n", nFilesEx);
	}
	return 0;
}

int appendtofileAPI(const char* path, void* content, size_t size, const char* dirSaving){
	if(path == NULL || !strlen(path) || content == NULL || size == 0){
		errno = EINVAL;
		return -1;
	}
	ssize_t n = 0;
	//RICHIESTA
	crequest_t req;
	req.op = APPENDFILE;
	req.lenpath = strlen(path);
	req.contentsize = size;
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writenC295");
	SC_EXIT(n, writen(fdsock, (void*)path, strlen(path)), "writenC296");
	SC_EXIT(n, writen(fdsock, content, size), "writenC297");	
	
	//RISPOSTA
	int reply = 0;
	SC_RETURN(n, readn(fdsock, &reply, sizeof(reply)), "readnC301");
	if(reply < OK){
		if(print){
			fprintf(stderr, "C > Errore nella gestione della richiesta di APPEND del file %s\n", path);
			printMsgErr(reply);
		}
		return -1;	
	}
	if(print) fprintf(stdout, "C > File %s scritto. Scritti %zu bytes.\n", path, size);
	if(reply > OK){
		//lettura dei file espulsi e salvataggio diretto nella directory se specificata
		int nFilesEx;
		if((nFilesEx = receiveFiles(fdsock, dirSaving, reply)) == -1){
			if(print){
				fprintf(stderr, "C > Errore nel salvataggio dei file espulsi con APPEND\n");
			}
			return -1;
		}
		if(print) fprintf(stdout, "C > Salvataggio di %d file espulsi effettuato\n", nFilesEx);
	}
	return 0;
}

int readfileAPI(const char* path, void** bufRead, size_t *bytesRead){
	if(!path || !strlen(path) || !bufRead){
		errno = EINVAL;
		return -1;
	} 
	ssize_t n = 0;
	
	//RICHIESTA
	crequest_t req;
	memset(&req, 0, sizeof(req));
	req.op = READFILE;
	req.lenpath = strlen(path);
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writenC336");
	SC_EXIT(n, writen(fdsock, (void*)path, strlen(path)), "writenC337");
	
	//RISPOSTA
	int reply = 0;
	SC_RETURN(n, readn(fdsock, &reply, sizeof(int)), "readnC341");
	if(reply < OK){
		if(print){
			fprintf(stderr, "C > Errore nella gestione della richiesta di READ del file %s\n", path);
			printMsgErr(reply);
		}
		return -1;	
	}
	//OK
	//leggo la riposta contenente il file letto
	size_t bread = 0;
	SC_EXIT(n, readn(fdsock, &bread, sizeof(size_t)), "readnC352");
	*bytesRead = bread;
	char* fileRead = calloc(sizeof(char)*bread + 1, 1);
	CHECK_EQ_RETURN(fileRead, NULL, "calloc", -1);
	SC_RETURN(n, readn(fdsock, fileRead, bread), "readnC356");
	*bufRead = (void*)fileRead;
	if(print) fprintf(stdout, "C > File %s letto. Letti %zu bytes\n", path, bread);
	return 0;								
}

int readnfileAPI(int nFiles, const char* dirSaving){
	ssize_t n = 0;
	//RICHIESTA
	crequest_t req;
	memset(&req, 0, sizeof(req));
	req.op = READNFILE;
	req.flags = nFiles;
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writenC369");
	
	//RISPOSTA
	int reply = 0;
	SC_RETURN(n, readn(fdsock, &reply, sizeof(int)), "readnC373");
	if(reply < OK){
		if(print){
			fprintf(stderr, "C > Errore nella gestione della richiesta di READNFILES\n");
			printMsgErr(reply);
		}
		return -1;	
	} 
	if(print) fprintf(stdout, "C > Files %d letti\n", reply);
	
	if(reply > OK) { // in reply è stato scritto il n di file letti
		//lettura dei file e salvataggio diretto nella directory se specificata
		int nFilesR = receiveFiles(fdsock, dirSaving, reply);
		if(nFilesR == -1){
			if(print){
				fprintf(stderr, "C > Errore nel salvataggio dei file letti per READNFILES\n");
				printMsgErr(reply);
			}
			return -1;
		}
		if(print) fprintf(stdout, "C > Salvataggio di %d file effettuato\n", nFilesR);
	}
	return 0;
}

int lockfileAPI(const char* path){
	if(!path || !strlen(path)){
		errno = EINVAL;
		return -1;
	}
	ssize_t n = 0;
	//RICHIESTA
	crequest_t req;
	memset(&req, 0, sizeof(req));
	req.op = LOCKFILE;
	req.lenpath = strlen(path);
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writenC409");
	SC_EXIT(n, writen(fdsock, (void*)path, strlen(path)), "writenC410");
	
	//RISPOSTA	
	int reply;
	SC_RETURN(n, readn(fdsock, &reply, sizeof(reply)), "readnC414");
	if(reply < OK){
		if(print){
			fprintf(stderr, "C > Errore nella gestione della richiesta di acquisizione ME sul file %s\n", path);
			printMsgErr(reply);
		}
		return -1;
	}
	if(print) fprintf(stdout, "C > ME su file %s acquisita\n", path);
	return 0;
}

int unlockfileAPI(const char* path){
	if(!path || !strlen(path)){
		errno = EINVAL;
		return -1;
	}
	ssize_t n = 0;
	//RICHIESTA
	crequest_t req;
	memset(&req, 0, sizeof(req));
	req.op = UNLOCKFILE;
	req.lenpath = strlen(path);
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writenC437");
	SC_EXIT(n, writen(fdsock, (void*)path, strlen(path)), "writenC438");
	
	//RISPOSTA
	int reply;
	SC_RETURN(n, readn(fdsock, &reply, sizeof(reply)), "readnC442");
	if(reply < OK){
		if(print){
			fprintf(stderr, "C > Errore nella gestione della richiesta di rilascio ME sul file %s\n", path);
			printMsgErr(reply);
		}
		return -1;
	}
	if(print) fprintf(stdout, "C > ME su file %s rilasciata\n", path);
	return 0;
}

int closefileAPI(const char* path){
	if(!path || !strlen(path)){
		errno = EINVAL;
		return -1;
	}
	ssize_t n = 0;
	//RICHIESTA
	crequest_t req;
	memset(&req, 0, sizeof(req));
	req.op = CLOSEFILE;
	req.lenpath = strlen(path);
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writenC465");
	SC_EXIT(n, writen(fdsock, (void*)path, strlen(path)), "writenC466");
	
	//RISPOSTA
	int reply;
	SC_RETURN(n, readn(fdsock, &reply, sizeof(reply)), "readnC470");
	if(reply < OK){
		if(print){
			fprintf(stderr, "C > Errore nella gestione della richiesta di CLOSE sul file %s\n", path);
			printMsgErr(reply);
		}
		return -1;
	}
	if(print) fprintf(stdout, "C > File %s chiuso\n", path);
	return 0;
}

int removefileAPI(const char* path){
	if(!path || !strlen(path)){
		errno = EINVAL;
		return -1;
	}
	ssize_t n = 0;
	//RICHIESTA
	crequest_t req;
	memset(&req, 0, sizeof(req));
	req.op = REMOVEFILE;
	req.lenpath = strlen(path);
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writenC493");
	SC_EXIT(n, writen(fdsock, (void*)path, strlen(path)), "writenC494");
	
	//RISPOSTA
	int reply;
	SC_RETURN(n, readn(fdsock, &reply, sizeof(reply)), "readnC498");
	if(reply < OK){
		if(print){
			fprintf(stderr, "C > Errore nella gestione della richiesta di REMOVE sul file %s\n", path);
			printMsgErr(reply);
		}
		return -1;
	}
	if(print) fprintf(stdout, "C > File %s rimosso\n", path);
	return 0;
}

void restore(const char* path){
	ssize_t n;
	//RICHIESTA
	crequest_t req;
	memset(&req, 0, sizeof(req));
	req.op = RESTORE;
	req.lenpath = strlen(path);
	SC_EXIT(n, writen(fdsock, &req, sizeof(req)), "writen517");
	SC_EXIT(n, writen(fdsock, (void*)path, strlen(path)), "writen518");
}
