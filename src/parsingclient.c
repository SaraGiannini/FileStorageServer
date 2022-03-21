#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

#include <util.h>
#include <conn.h>
#include <parsingclient.h>
/** funzione che alloca una coppia opzione- argomenti  */
static optarg_t* allocOptArg(char opt, char* args){
	optarg_t* newopt = calloc(sizeof(optarg_t),1);
	CHECK_EQ_RETURN(newopt, NULL, "calloc", NULL);
	//OPT
	newopt->opt = opt;
	//ARGS
	newopt->args = calloc(strlen(args)+1, 1);
	CHECK_EQ_RETURN(newopt->args, NULL, "calloc", NULL);
	strncpy(newopt->args, args, strlen(args));
	//NEXT
	newopt->next = NULL;
	
	return newopt;
}

/** procedura che aggiunge in coda alla lista la nuova coppia opt-arg */
static void addOptList(optarg_t** list, optarg_t* new){
	optarg_t** tmp = list;
	while(*tmp)
		tmp = &((*tmp)->next);
	*tmp = new;
}

/** funzione che controlla se è presente l'opzione nella lista delle richieste */
static int containsOpt(optarg_t* list, char opt){
	optarg_t* tmp = list;
	while(tmp){
		if(tmp->opt == opt)
			return 1;
		tmp = tmp->next;
	}
	return 0;
}

/** funzione per ottenere l'argomento dell'opzione opt passata come parametro s*/
char* getArgs(optarg_t* list, char opt){
	char* arguments = NULL;
	optarg_t* tmp = list;
	while(tmp){
		if(tmp->opt == opt){
			arguments = calloc(strlen(tmp->args)+1, 1);
			CHECK_EQ_RETURN(arguments, NULL, "calloc", NULL);
			strncpy(arguments, tmp->args, strlen(tmp->args));	
			return arguments;
		}
		tmp = tmp->next;
	}
	return arguments;
}

/** procedura che dealloca il contenuto della lista */
void deallocList(optarg_t* list){
	if(list){
		optarg_t* tmp;
		while(list){
			tmp = list;
			list = list->next;
			if(tmp && tmp->args){
				free(tmp->args);
				free(tmp);
			}
		}
	}
}

/**
 * funzione chiamata da parsingReq per controllare che -D e -d siano presenti solo se presenti -w/W e -r/R rispettivamente
 */
static int validateOpt(optarg_t* reqList){
	if(containsOpt(reqList, 'D')){
		//-D deve essere congiunta a -w o -W (o anche -a) altrimenti è errore (tutta la sessione del Client)
		if(!(containsOpt(reqList, 'w') || containsOpt(reqList, 'W') || containsOpt(reqList, 'a'))){
			fprintf(stderr, "L'opzione -D non può essere usata senza -w o -W\n");
			return -1;
		}
	}
	if(containsOpt(reqList, 'd')){
		//- deve essere congiunta a -r o -R altrimenti è errore (tutta la sessione del Client)
		if(!(containsOpt(reqList, 'r') || containsOpt(reqList, 'R'))){
			fprintf(stderr, "L'opzione -d non può essere usata senza -r o -R\n");
			return -1;
		}
	}		
	return 0;
}

/**
 * funzione che effettua il parsing linea di comando, crea la lista per la configurazione (-p, -h, -f, -t) e quella delle richieste
 */
optarg_t* parsingCL(int argc, char* argv[], optarg_t** reqList){
	optarg_t* configOAList = NULL;
	optarg_t* requestOAList = NULL;
	int countf = 0;
	int countp = 0;
	int opt;
	errno = 0;
	while((opt = getopt(argc, argv, ":hpf:t::w:W:a:D:r:R::d:l:u:c:")) != -1){
		optarg_t* newopt = NULL;
		switch(opt){
		case 'h':
			errno = HELP; //da controllare
			return NULL; //perchè deve terminare subito dopo aver stampato, senza ritornare una lista
			break;
		case 'p':
			if(!countp){ //permette di inserire nella lista una sola volta l'opzione
				//abilito 'print' settando 1 il suo campo args
				newopt = allocOptArg(opt, "1");		
				addOptList(&configOAList, newopt);
				countp = 1;
			}
			else fprintf(stderr, "L'opzione -%c è già stata inserita\n", optopt);	
			break;
		case 'f':
			if(!countf){ //permette di inserire nella lista una sola volta l'opzione
				newopt = allocOptArg(opt, optarg);
				addOptList(&configOAList, newopt);
				countf = 1;
			}
			else fprintf(stderr, "L'opzione -%c è già stata inserita\n", optopt);	
			break;
		case 't':
			//può avere l'argomento opzionale, quindi uso ::
			if(optarg == NULL && optind < argc && argv[optind][0] != '-'){
				optarg = argv[optind++];
				newopt = allocOptArg(opt, optarg);
			}
			else 
				newopt = allocOptArg(opt, "0");
			addOptList(&configOAList, newopt);
			break;
		//opzioni il cui argomento è formato da più contenuti separati da ','
		case 'w':
		case 'W':
		case 'r':
		case 'a':
		case 'l':
		case 'u':
		case 'c':
			newopt = allocOptArg(opt, optarg);
			addOptList(&requestOAList, newopt);
			break;
		//opzione con parametro 'n' opzionale, se non c'è setto -1
		case 'R':
			//può avere l'argomento opzionale, quindi uso ::
			if(optarg == NULL && optind < argc && argv[optind][0] != '-'){
				optarg = argv[optind++];
				newopt = allocOptArg(opt, optarg);
			}
			else 	newopt = allocOptArg(opt, "0"); //se non specificato(->0) o se 0 il server leggerà tutti i file  presenti nel FS
			addOptList(&requestOAList, newopt);		
			break;
		//opzioni per le directory
		case 'D':
		case 'd':
			newopt = allocOptArg(opt, optarg);
			addOptList(&requestOAList, newopt);
			break;			
		case '?':
			fprintf(stderr, "L'opzione -%c non è gestibile\n", optopt);
			return NULL;
			break;
		case ':':
			fprintf(stderr, "L'opzione -%c richiede un argomento\n", optopt);
			return NULL;
			break;
		default: ;
		}
	}
	//dopo aver creato la lista dal parsing, effettuo i controlli per -D e -d
	if(validateOpt(requestOAList) == -1){
		deallocList(requestOAList); //se la validazione da errore dealloco la lista delle richieste
	}
	*reqList = requestOAList;
	return configOAList;
}
