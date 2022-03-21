#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include <util.h>
#include <conn.h>
#include <parsingclient.h>
#include <api.h>

char *strtok_r(char *str, const char *delim, char **saveptr);
char *realpath(const char* restrict path, char* restrict resolved_path);

/** procedura di stampa delle opzioni accettate dal Client */
void printOpt(){
	printf("-----| Opzioni possibili da passare a linea di comando del Client:\n\
	-h : stampa la lista delle opzioni accettate.\n\
	-f <filename> : specifica il nome del socket AF_UNIX a cui connettersi.\n\
	-w <dirname>[,n=0] : invia al server i file contenuti nella cartella <dirname>,	ne invia al massimo 'n' se specificato;\n\
	     visita ricorsivamente le sottocartelle di <dirname>.\n\
	-W <file1>[,<file2>] : lista i nomi di file da scrivere nel server, separati da ','.\n\
	-D <dirname> : cartella in memoria secondaria dove vengono salvati (lato client) i file\n\
	     che il server rimuove a seguito di capacity misses della cache.\n\
	     Da usare congiuntamente a -w o -W.\n\
	-a <file>,<content> : invia al server il contenuto da appendere al file\n\
	-r <file1>[,<file2>] : lista di nomi di file da leggere dal server, separati da ','\n\
	-R [n=0] : permette di leggere 'n' file qualsiasi memorizzati nel server.\n\
	-d <dirname> : cartella in memoria secondaria dove vengono salvati (lato client) i file	letti dal server.\n\
	    Da usare congiuntamente a -r o -R.\n\
	-t <time> : tempo in msec che intercorre tra l'invio di due richieste successive al server.\n\
	-l <file1>[,<file2>] : lista di nomi di file su cui acquisire la mutua esclusione.\n\
	-u <file1>[,<file2>] : lista di nomi di file su cui rilasciare la mutua esclusione.\n\
	-c <file1>[,<file2>] : lista di nomi di file da rimuovere dal server.\n\
	-p : abilita le stampe sullo standard output per ogni operazione.\n\
	-----\n");
}

/** funzione che ritorna l'array di argomenti che sono separati da ',' */
char** splitArguments(char *args){
	char** splitted = NULL;
	int i = 0;
	int nargs = 1; // ad ogni ',' incontrata abbiamo un argomento in più
	if(args){	
		while(args[i] != '\0'){
			if(args[i] == ',') nargs++;
			i++;
		}
		//sapendo il numero di argomenti posso allocare l'array di argomenti
		splitted = malloc(sizeof(char*)*nargs);
		i = 0;
		//inizio a tokenizzare la stringa args formando l'array di argomenti separati
		char* save = NULL;
		splitted[i] = strtok_r(args, ",", &save);
		i++;
		while((splitted[i] = strtok_r(NULL, ",", &save)) != NULL)
			i++;
	}
	return splitted;
}


/** funzione per visitare ricorsivamente la directory passata come argomento a -w e leggere i file da inviare al Server */ 
int visitRecDirectory(char* dirname, int nFiles, char* dirSaving){
	DIR* dir = NULL;
	if((dir = opendir(dirname)) == NULL){
		perror("opendir");
		return -1;
	}
	char* filepath; //path relativo del file a cui si aggiunge la directory che lo contiene
	int filesWritten = 0;
	struct dirent* current;
	//setto errno a 0 per discriminare se l'errore è per EOF  o meno
	while((errno = 0, current = readdir(dir)) && (nFiles == 0 || filesWritten < nFiles)){
		/*if(!current && errno){
			//non è EOF
			perror("readdir");
			return -1;
		}*/
		if(strcmp(current->d_name, ".") == 0 || strcmp(current->d_name, "..") == 0)
			continue; //salto la dir corrente e la dir padre
		filepath = calloc(strlen(dirname) + strlen(current->d_name) + 2, 1);
		CHECK_EQ_RETURN(filepath, NULL, "calloc", -1);
		strncpy(filepath, dirname, strlen(dirname));
		strcat(filepath, "/");
		strcat(filepath, current->d_name);
		//recupero i dati del file corrente per verificare se è directory o no
		struct stat st;
		if(stat(filepath, &st) == -1){
			perror("stat");
			return -1;
		}
		if(S_ISDIR(st.st_mode)){ //è una directory
			//si procede ricorsivamente in questa sottodirectory
			int rec;
			if((rec = visitRecDirectory(filepath, (nFiles == 0) ? nFiles : nFiles - filesWritten, dirname)) == -1)
				return -1;
			filesWritten += rec;
		}
		else{ //è un file -> si procede con la scrittura
			//REALPATH
			char fileabspath[PATH_MAX]; //4096
			if(realpath(filepath, fileabspath) == NULL)
				perror("realpath");
			free(filepath);
			
			//OPENFILE
			if(openfileAPI(fileabspath, O_CREATE | O_LOCK) != 0){
				return -1;
			}
			//WRITEFILE
			if(writefileAPI(fileabspath, dirSaving) != 0){
				return -1;
			}
			//CLOSEFILE
			if(closefileAPI(fileabspath) != 0){
				return -1;
			}
			filesWritten++;
		}
	}
	if(!current && errno){
		//non è EOF
		perror("readdir");
		return -1;
	}
	//EOF, chiudo la dir e torno al chiamante #file scritti	
	if(closedir(dir) == -1){
		perror("closedir");
		return -1;
	}
	return filesWritten;
}
/** funzione per gestire -w */
int writedirH(char** arguments, char* dirSaving){
	char* dirname = arguments[0];
	long nFiles = 0; //se non specificato, non c'è limite alla qtà di file da inviare
	if(!dirname){
		fprintf(stderr, "C > Directory non valida perché argomento nullo\n");
		errno = EINVAL;
		return -1;
	}
	if(arguments[1])
		if(isNumber(arguments[1], &nFiles) != 0){
			errno = EINVAL;
			return -1;
		}
	if(print){
		if(nFiles != 0)
			printf("\nC > Inizio scrittura nel Server di %d file contenuti nella cartella %s\n", (int)nFiles, dirname);
		else	printf("\nC > Inizio scrittura nel Server di tutti i file contenuti nella cartella %s\n", dirname);
	}
	int filesWritten = 0;	
	//visita ricorsiva nella directory
	if((filesWritten = visitRecDirectory(dirname, nFiles, dirSaving)) == -1){ //writefileAPI incluso
		if(print) fprintf(stderr, "C > Errore scrittura file\n");	
		return -1;
	}
	if(print) printf("C > Sono stati scritti %d files nel Server\n", filesWritten);
	return 0;		
}
/** funzione per gestire -W */
int writefilesH(char** arguments, char* dirSaving){
	int i = 0;
	//ciclo sugli argomenti che sono tutti files
	while(arguments[i] != NULL){
		//REALPATH
		char fileabspath[PATH_MAX]; //4096
		if(realpath(arguments[i], fileabspath) == NULL)
			perror("realpath");
		if(print) printf("\nC > Inizio la scrittura del file %s\n", arguments[i]);
		//OPENFILE
		if(openfileAPI(fileabspath, (O_CREATE | O_LOCK)) != 0){
			i++;
			continue; //prossimo file
		}
		//WRITEFILE
		if(writefileAPI(fileabspath, dirSaving) != 0){
			i++;
			restore(fileabspath);
			continue; //prossimo file
		}
		//CLOSEFILE
		if(closefileAPI(fileabspath) != 0){
			i++;
			continue; //prossimo file
		}
		i++;
	}
	return 0;
}
/** funzione per gestire -a */
int appendH(char** arguments, char* dirSaving){
	//primo arg è il path assoluto del file, secondo arg è il contenuto da appendere
	if(print) printf("\nC > Inizio la scrittura in append del file %s\n", arguments[0]);
	//OPENFILE
	if(openfileAPI(arguments[0], 0) != 0) 
		return -1;
	//APPENDFILE
	if(appendtofileAPI(arguments[0], (void*)arguments[1], strlen(arguments[1]), dirSaving) != 0)
		return -1;
	//CLOSEFILE
	if(closefileAPI(arguments[0]) != 0)
		return -1;
	return 0;
}
/** funzione per gestire -r */
int readH(char** arguments, char* dirSaving){
	int i = 0;
	//ciclo sugli argomenti che sono tutti file (con path assoluti)
	while(arguments[i] != NULL){
		char* fileRead = NULL; 
		size_t bytesRead = 0; //salvataggio bytes letti
		if(print) printf("\nC > Inizio la lettura del file %s\n", arguments[i]);
		//OPENFILE
		if(openfileAPI(arguments[i], 0) != 0){
			i++;
			continue; //prossima lettura
		}
		//READFILE
		if(readfileAPI(arguments[i], (void*)&fileRead, &bytesRead) != 0){
			i++;
			continue; //prossima lettura
		}
		//CLOSEFILE
		if(closefileAPI(arguments[i]) != 0){
			i++;
			continue; //prossima lettura 
		}
		//salvare il file nella directory se richiesto
		if(dirSaving != NULL){
			if(saveFile(dirSaving, arguments[i], fileRead, bytesRead) == -1){
				perror("saveFile");
				if(print) fprintf(stderr, "C > Impossibile salvare il file %s nella directory\n", arguments[i]);
				free(fileRead);
				return -1;
			}
		}
		if(fileRead)
			free(fileRead);
		i++;
	}
	return 0;
}
int executeReq(optarg_t* reqList, long time){
	while(reqList){
		int skipdir = 0;
		char* dirSaving = NULL; //directory in cui salvare i file espulsi(-w/W) o richiesti(-r/R)
		char** arguments = NULL;
		if(reqList->opt != 'd' && reqList->opt != 'D' && reqList->opt != 'R'){
			//splitto gli argomenti di un'opzione separati da ',' in un'array di argomenti 
			if(reqList->args){
				arguments = splitArguments(reqList->args); 
			}
		}
		switch(reqList->opt){
		case 'w':{
			//recupero (nel opt-arg successivo) nome dir in cui salvare file espulsi
			if(reqList->next && reqList->next->opt == 'D'){
				//recupero (nel opt-arg successivo) nome dir in cui salvare file espulsi
				dirSaving = reqList->next->args;
				skipdir = 1;
			}
			if(writedirH(arguments, dirSaving) == -1){
				free(arguments);
				return -1; //oppure break;
			}
		} break;
		case 'W':{
			//recupero (nel opt-arg successivo) nome dir in cui salvare file espulsi
			if(reqList->next && reqList->next->opt == 'D'){
				dirSaving = reqList->next->args;
				skipdir = 1;
			}
			if(writefilesH(arguments, dirSaving) == -1){
				free(arguments);
				return -1; //oppure break;
			}
		} break;
		case 'a':{
			//recupero (nel opt-arg successivo) nome dir in cui salvare file espulsi
			if(reqList->next && reqList->next->opt == 'D'){
				dirSaving = reqList->next->args;
				skipdir = 1;
			}
			if(appendH(arguments, dirSaving) == -1){
				free(arguments);
				return -1; //oppure break;
			}
		} break;
		case 'r':{
			//recupero (nel opt-arg successivo) nome dir in cui salvare file letti
			if(reqList->next && reqList->next->opt == 'd'){
				dirSaving = reqList->next->args;
				skipdir = 1;
			}
			if(readH(arguments, dirSaving) == -1){
				free(arguments);
				return -1; //oppure break;
			}
		} break;
		case 'R':{
			//recupero (nel opt-arg successivo) nome dir in cui salvare file letti
			if(reqList->next && reqList->next->opt == 'd'){
				dirSaving = reqList->next->args;
				skipdir = 1;
			}
			long nFiles = 0;
			if(isNumber(reqList->args, &nFiles) != 0){
				return -1;
			}
			if(print){
				if(nFiles != 0)
					printf("\nC > Inizio la lettura di %d files\n", (int)nFiles);
				else 	printf("\nC > Inizio la lettura di tutti i files\n");
			}
			readnfileAPI(nFiles, dirSaving);
		} break;
		case 'l':{
			int i = 0;
			//ciclo sugli argomenti che sono tutti files (path assoluto)
			while(arguments[i] != NULL){
				if(lockfileAPI(arguments[i]) != 0){
					i++;
					continue; //prossimo file
				}
				i++;
			}				
		} break;
		case 'u':{
			int i = 0;
			//ciclo sugli argomenti che sono tutti files (path assoluto)
			while(arguments[i] != NULL){
				if(unlockfileAPI(arguments[i]) != 0){
					i++;
					continue; //prossimo file
				}
				i++;
			}				
		} break;
		case 'c':{
			int i = 0;
			//ciclo sugli argomenti che sono tutti files (path assoluto)
			while(arguments[i] != NULL){
				if(removefileAPI(arguments[i]) != 0){
					i++;
					continue; //prossimo file
				}
				i++;
			}			
		} break;
		default: {
			//anche per -D/d se non presenti prima -w/W/r/R
			fprintf(stderr, "Opzione non riconosciuta\n");
			free(arguments);
			return -1;
		}
		}	
		free(arguments);
		reqList = reqList->next;
		//ho incontrato D/d e già salvato il nome della directory, passo alla prossima opt
		if(reqList && skipdir == 1) 
			reqList = reqList->next; 
		usleep(time*1000);
	}
	return 0;
}

int main(int argc, char* argv[]){
	print = 0;
	char sockname[MAX_SOCKPATH_LEN] = "";
	 
	//timeout in msec che intercorre tra due richieste succ al server, se non specificato è 0	
	long timeoutReq = 0; 
	
	optarg_t* configList = NULL;
	optarg_t* requestList = NULL;
	//PARSING delle opzioni -h(eventuale stampa immediata), -p, -f, -t, e richieste
	configList = parsingCL(argc, argv, &requestList);
	if(configList == NULL){
		if(errno == HELP){
			//stampo la guida delle opzioni e termino (con successo)
			printOpt();
			deallocList(configList);
			deallocList(requestList);
			return 0;
		}
		else{
			fprintf(stderr, "Errore nel parsing dei dati da linea di comando\n");
			deallocList(configList);
			deallocList(requestList);
			return -1;
		}
	}
	if(requestList == NULL){
		fprintf(stderr, "Nessuna richiesta di esecuzione\n");
		deallocList(configList);
		deallocList(requestList);
		return -1;
	}
		
	//inizializzazione dati print, sockname, time
	char* arguments;
	//-p
	if((arguments = getArgs(configList, 'p')) != NULL){
		if(strncmp(arguments, "1", strlen(arguments)) == 0)
			print = 1; //abilito la stampa su stdout delle operazioni
	}
	free(arguments);
	//-f
	if((arguments = getArgs(configList, 'f')) != NULL)
		strncpy(sockname, arguments, MAX_SOCKPATH_LEN);
	else{
		fprintf(stderr, "Deve essere specificato il nome del socket a cui connettersi\n");
		deallocList(configList);
		deallocList(requestList);
		return -1;
	}
	free(arguments);
	//-t	
	if((arguments = getArgs(configList, 't')) != NULL){
		if(isNumber(arguments, &timeoutReq) != 0){
			fprintf(stderr, "l'argomento non è un numero\n");
			deallocList(configList);
			deallocList(requestList);
			return -1;
		}	
	}
	free(arguments);
	deallocList(configList);
		
	int msec = 1000;
	int err;
	struct timespec abstime;
	SC_EXIT(err, clock_gettime(CLOCK_REALTIME, &abstime), "clock_gettime");
	abstime.tv_sec += 10;
	
	//APERTURA della connessione
	if(openconnectionAPI(sockname, msec, abstime) == -1){
		if(print) fprintf(stderr, "C > Errore durante la connessione al server\n");
		deallocList(requestList);
		return -1;
	}
		
	//ESECUZIONE delle richieste
	if(executeReq(requestList, timeoutReq) != -1){
		deallocList(requestList);
	}
	//CHIUSURA della connessione
	if(closeconnectionAPI(sockname) == -1){
		if(print) fprintf(stderr, "C > Errore durante la chiusura della connessione con il server\n");
		return -1;
	}	
	return 0;
}
