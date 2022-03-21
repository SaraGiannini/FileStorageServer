#if !defined(PARSING_CLIENT_H_)
#define PARSING_CLIENT_H


/**
 *
 * @file Header per la funzione di parsing degli argomenti da linea di comando del Client
 *
 */ 

/**
 * @struct di input -opt args
 */
typedef struct optarg{
	char opt;
	char* args;
	struct optarg* next;
} optarg_t;


/**
 * @brief parsing linea di comando, crea la lista per la configurazione (-p, -h, -f, -t) e quella delle richieste
 */
optarg_t* parsingCL(int argc, char* argv[], optarg_t** reqList);

void deallocList(optarg_t* list);

char* getArgs(optarg_t* list, char opt);

#endif
