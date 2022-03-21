#if !defined(CONFIG_SERVER_H_)
#define CONFIG_SERVER_H_

/**
 *
 * @file Header per la funzionalità di configurazione del Server
 *
 */

#define MAX_LEN 500 //1024

/**
 *
 * @struct pair_t
 * @brief coppia chiave-valore
 *
 */
typedef struct pair{
	char key[MAX_LEN + 1];		//nome della chiave 
	char value[MAX_LEN + 1];	//valore rispettivo (convertito poi in intero per i valori che lo richiedono)
} pair_t;

/**
 *
 * @struct info_t
 * @brief informazioni di configurazione del server
 *
 */
typedef struct info{
	pair_t nworker;		//numero di threads Worker
	pair_t maxfile;		//numero massimo di file memorizzabili nello storage
	pair_t maxcap;		//spazio di memorizzazione dello storage (in bytes - 1MB)
	pair_t sockname;	//nome del socket file
	pair_t logpath;		//nome file dei log
	pair_t replacementp;	//politica di  rimpiazzamento dei file nella cache del server a seguito di "capacity misses" {0-FIFO, 1-LRU}
} info_t;


/** 
 *
 * @brief funzione che preleva le informazioni di configurazione dal file apposito
 * @param filename file da cui leggere le informazioni
 * @return struttura contenente tali informazioni
 *
 */
info_t* getConfigInfo(const char* filename);
	
/**
 * @brief funzione che ritorna un codice corrispondente alla politica 
 */
size_t getPolicy(char* value);


/**
 * @brief procedura per stampare la configurazione iniziale del server, indicando il file di configurazione utilizzato
 */
void printConfiguration(info_t* cinfo, char *cfile);

#endif /* CONFIG_SERVER_H_ */
