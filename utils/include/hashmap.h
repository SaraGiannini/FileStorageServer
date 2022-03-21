#if !defined(HASHMAP_H)
#define HASHMAP_H

/**
 * @struct entry_t
 * @brief Struttura che rappresenta un nodo della hashmap
 */
typedef struct entry{
	char* key;
	int value;
	struct entry* next;
} entry_t;

/**
 * @struct hashmap_t
 * @brief Struttura che rappresenta una HashMap
 */
 
typedef struct hm{
	int n;
	entry_t* pair;
} hashmap_t;

/**
 * @brief Inizializza la struttura dati HashMap di dimensione size
 * @param hm HashMap da inizializzare
 * @param size numero di elementi iniziali di hashmap
 */
void initHashmap(hashmap_t *hm, int size);

/**
 * @brief Funzione per ottenere il valore di key
 * @param hm HashMap in cui è contenuto il valore da ottenere
 * @param key chiave di cui si vuole ottenere il rispettivo valore
 * @return valore della coppia con chiave key
 */
int getValue(hashmap_t hm, char *key);

/**
 * @brief Funzione per ottenere la chiave di value
 * @param hm HashMap in cui è contenuta la chiave da ottenere
 * @param value valore di cui si vuole ottenere la rispettiva chiave
 * @return chiave della coppia con valore value
 */
char* getKey(hashmap_t hm, int value);

/**
 * @brief Funzione per inserire la coppia chiave-valore all'interno dell'HashMap
 * @param hm HashMap in cui inserire la coppia chiave-valore
 * @param key chiave da inserire
 * @param value valore da inserire
 */
void hashmapPut(hashmap_t *hm, char* key, int value);
 
 /**
  * @brief Funzione per eliminare dalla memoria la struttura dati HashMap
  * @param hm HashMap da eliminare
  */
void deleteHashmap(hashmap_t *hm);

#endif /*HASHMAP_H*/
