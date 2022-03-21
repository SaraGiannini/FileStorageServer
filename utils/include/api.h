#if !defined(API_H)
#define API_H

#define MAX_SOCKPATH_LEN 1024


//variabile globale per abilitare la stampa sul stdout per ogni operazione
int print; 


int saveFile(const char* dirSaving, char* path, void* content, size_t contentsize);


/*----- operazioni per le richieste -----*/ 

int openconnectionAPI(const char *sockname, int msec, const struct timespec abstime);

int closeconnectionAPI(const char *sockname);

int openfileAPI(const char* path, int flags);

int readfileAPI(const char* path, void** bufRead, size_t *bytesRead);

int readnfileAPI(int nFiles, const char* dirSaving);

int writefileAPI(const char* path, const char* dirSaving);

int appendtofileAPI(const char* path, void* content, size_t size, const char* dirSaving);

int lockfileAPI(const char* path);

int unlockfileAPI(const char* path);

int closefileAPI(const char* path);

int removefileAPI(const char* path);

void restore(const char* path);

#endif
