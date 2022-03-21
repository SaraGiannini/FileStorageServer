#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <configserver.h>
#include <util.h>

/**
 *
 * @file configserver.c
 * @brief File di implementazione dell'interfaccia server per la configurazione iniziale
 *
 */

char *strtok_r(char *str, const char *delim, char **saveptr);

info_t* getConfigInfo(const char* filename){	
	if(!filename)
		return NULL;
		
	info_t* cinfo  = (info_t*) calloc(sizeof(info_t), 1);
	CHECK_EQ_RETURN(cinfo, NULL, "calloc", NULL);
	//memset(cinfo, 0, sizeof(*cinfo)); // per malloc
	
	char buf[BUFSIZ];
	memset(&buf, 0, BUFSIZ);
	pair_t* kvpair;
	
	FILE* cfile;
	if((cfile = fopen(filename, "r")) == NULL){
		perror("fopen");
		int err = errno;
		fprintf(stderr, "Errore aprendo input file %s: errno = %d\n", filename, err);
		free(cinfo);
		return NULL;
	}
	
	while(fgets(buf, BUFSIZ, cfile) != NULL){
		//salto commenti e righe vuote
		if(buf[0] == '#' || buf[0] == '\n') continue;
		
		//coppia chiave-valore per salvataggio valori tokenizzati
		kvpair = calloc(sizeof(*kvpair), 1);
		CHECK_EQ_RETURN(kvpair, NULL, "calloc", NULL); 
		
		//tokenizzo tramite ' : '
		char *tmp = NULL, *token;
		//chiave
		token = strtok_r(buf, " : ", &tmp);
		if(!token){
			free(kvpair);
			free(cinfo);
			return NULL;
		}
		strncpy(kvpair->key, token, sizeof kvpair->key);
		kvpair->key[sizeof(kvpair->key) - 1] = '\0';
		//valore
		token = strtok_r(NULL, " : ", &tmp);
		if(!token){
			free(kvpair);
			free(cinfo);
			return NULL;
		}
		strncpy(kvpair->value, token, strlen(token));
		kvpair->value[strcspn(kvpair->value, "\n")] = '\0';
		
		if(strncmp(kvpair->key, "NWORKER", strlen(kvpair->key)) == 0){
			strncpy(cinfo->nworker.key, "NWORKER", strlen(kvpair->key));
			strncpy(cinfo->nworker.value, kvpair->value, strlen(kvpair->value));
		}
		else if(strncmp(kvpair->key, "MAXFILE", strlen(kvpair->key)) == 0) {
			strncpy(cinfo->maxfile.key, "MAXFILE", strlen(kvpair->key));
			strncpy(cinfo->maxfile.value, kvpair->value, strlen(kvpair->value));
		} 
		else if(strncmp(kvpair->key, "MAXCAPACITY", strlen(kvpair->key)) == 0) {
			strncpy((cinfo->maxcap).key, "MAXCAPACITY", strlen(kvpair->key));
			strncpy((cinfo->maxcap).value, kvpair->value, strlen(kvpair->value));
		} 
		else if(strncmp(kvpair->key, "SOCKETFILENAME", strlen(kvpair->key)) == 0) {
			strncpy(cinfo->sockname.key, "SOCKETFILENAME", strlen(kvpair->key));
			strncpy(cinfo->sockname.value, kvpair->value, strlen(kvpair->value));
		}
		else if(strncmp(kvpair->key, "LOGFILENAME", strlen(kvpair->key)) == 0) {
			strncpy(cinfo->logpath.key, "LOGFILENAME", strlen(kvpair->key));
			strncpy(cinfo->logpath.value, kvpair->value, strlen(kvpair->value));
		}
		else if(strncmp(kvpair->key, "REPLACEMENTPOLICY", strlen(kvpair->key)) == 0) {
			strncpy(cinfo->replacementp.key, "REPLACEMENTPOLICY", strlen(kvpair->key));
			strncpy(cinfo->replacementp.value, kvpair->value, strlen(kvpair->value));
		}
		memset(&buf, 0, BUFSIZ);
		free(kvpair);
	}
	fclose(cfile);
	return cinfo;
}

size_t getPolicy(char* value){
	size_t policy = 0;
	if(strncmp(value, "FIFO", strlen(value)) == 0)
		policy = 0;
	if(strncmp(value, "LRU", strlen(value)) == 0)
		policy = 1;
	if(strncmp(value, "LFU", strlen(value)) == 0)
		policy = 2;
	return policy;
}

void printConfiguration(info_t* cinfo, char *cfile){
	printf("----------| Configurazione iniziale del Server : %s |----------\n", cfile);
	printf("Numero di thread Worker : %s\n", cinfo->nworker.value);
	printf("Numero massimo di file memorizzabili nel FS : %s\n", cinfo->maxfile.value);
	printf("Dimensione massima del FS : %s\n", cinfo->maxcap.value);
	printf("Socket : %s\n", cinfo->sockname.value);
	printf("File di logging : %s\n", cinfo->logpath.value);
	printf("Politica di rimpiazzamento : %s\n", cinfo->replacementp.value);
	printf("-----------------------------------------------------------------------\n");
}

/*void deleteCInfo(info_t* cinfo){
	if(!cinfo) return;
	
	if(cinfo->nworker)
		free(cinfo->nworker);
	if(cinfo->maxfile)
		free(cinfo->maxfile);
	if(cinfo->maxcap)
		free(cinfo->maxcap);
	if(cinfo->sockname)
		free(cinfo->sockname);
	if(cinfo->logpath)
		free(cinfo->logpath);
	if(cinfo->replacementp)
		free(cinfo->replacementp);
			
	free(cinfo);
}*/
