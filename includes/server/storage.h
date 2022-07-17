
#ifndef FILE_STORAGE_SERVER_STORAGE_H
#define FILE_STORAGE_SERVER_STORAGE_H

#include <pthread.h>

#include "icl_hash.h"
#include "util.h"
#include "conn.h"
#include "list.h"
#include "queue.h"

typedef struct file_{
    char *filename;
    unsigned int size;
    void *content;
    int client_locker;
    list_t *who_opened;
    pthread_rwlock_t *mutex;
}file_t;

typedef struct storage_{
    int files_limit;
    unsigned long long memory_limit; //bytes

    int files_number;
    unsigned long long occupied_memory; //bytes
    icl_hash_t *files;
    list_t* filenames_queue;
    pthread_rwlock_t *mutex;
    list_t *clients_awaiting;

    //Statistiche
    int max_files_number;
    unsigned long long max_occupied_memory;
    int replace_mode;
    int times_replacement_algorithm;

}storage_t;

/**
 * @brief Crea ed inizializza nuovo storage con i valori passati come parametro
 * @param max_files     massimo numero di file consentiti, deve essere > 0
 * @param max_capacity  massima memoria raggiungibile dallo storage (in bytes), deve essere > 0
 * @param replace_mode  modalità di rimpiazzamento dei file a causa di capacity miss
 * @return puntatore allo storage creato, NULL in caso di errore
 */
storage_t* fs_init(int max_files, unsigned long long max_capacity, int replace_mode);

/**
 * @brief Dealloca lo storage
 * @param storage  puntatore allo storage da deallocare
 */
void fs_destroy(storage_t* storage);

/**
 * @brief Apre un file sullo storage. Se non è presente il file viene creato solo se è stato passato
 * il flag O_CREATE, altrimenti se il client ha i permessi viene aperto con i flag indicati.
 * @param storage   storage su cui eseguire l'operazione
 * @param filename  nome del file da aprire
 * @param flags     flag che indicano come si vuole aprire il file (O_NORMAL, O_CREATE, O_LOCK)
 * @param client    id del client che ha richiesto l'operzione
 * @return un intero che indica se l'operazione è stata completata con successo oppure il tipo di errore verficatosi
 */
int fs_openFile(storage_t* storage, char* filename, int flags, int client);

/**
 * @brief Legge un file dallo storage se esiste e se l'utente ha i permessi richiesti.
 * Il contenuto letto dal file viene memorizzato nel buffer buf.
 * @param storage     storage su cui effettuare l'operazione
 * @param filename    nome del file da leggere
 * @param client      id del client
 * @param buf         buffer dove memorizzare il contenuto del file
 * @param bytes_read  numero di byte letti dal file
 * @return un intero che indica se l'operazione è stata completata con successo oppure il tipo di errore verficatosi
 */
int fs_readFile(storage_t *storage, char *filename, int client, void *buf, unsigned long *bytes_read);

/**
 * @brief Legge N file qualsiasi dallo storage (che hanno un contenuto > 0), se N <= 0 vengono letti tutti quelli
 * presenti nello storage.
 * @param storage        storage su cui effettuare l'operazione
 * @param client         id del client che ha richiesto l'operazione
 * @param N              Numero di file da leggere
 * @param files_to_send  lista in cui memorizzare i file letti
 * @param filecount      numero di file letti
 * @return un intero che indica se l'operazione è stata completata con successo oppure il tipo di errore verficatosi
 */
int fs_readNFiles(storage_t *storage, int client, int N, list_t *files_to_send, int *filecount);

/**
 * @brief Effettua la prima scrittura sul file filename. Il file deve essere vuoto e in modalità "locked" da parte
 * del client che ha richiesto l'operazione. Scritture future sullo stesso file devono essere effettuate utilizzando la
 * appendToFile. Può causare l'espulsione di altri file che vengono memorizzati nella lista filesEjected.
 * @param storage       storage su cui effettuare l'operazione
 * @param filename      nome del file da scrivere
 * @param file_size     dimensione del contenuto da scrivere
 * @param file_content  contenuto
 * @param client        id del client
 * @param filesEjected  lista in cui memorizzare eventuali file espulsi
 * @return un intero che indica se l'operazione è stata completata con successo oppure il tipo di errore verficatosi
 */
int fs_writeFile(storage_t* storage, char *filename, size_t file_size, void* file_content, int client, list_t *filesEjected);

/**
 * @brief Effettua una scrittura in append al file. Può causare l'espulsione di altri file che vengono
 * memorizzati nella lista filesEjected
 * @param storage       storage su cui effettuare l'operazione
 * @param filename      nome del file da scrivere
 * @param size          dimensione del contenuto da aggiungere in bytes
 * @param data          contenuto da aggiungere al file
 * @param client        id client
 * @param filesEjected  lista in cui memorizzare i file espulsi
 * @return un intero che indica se l'operazione è stata completata con successo oppure il tipo di errore verficatosi
 */
int fs_appendToFile(storage_t* storage, char* filename, size_t size, void* data, int client, list_t *filesEjected);

/**
 * @brief Tenta di acquisire la mutua esclusione sul file filename. Se la lock sul file
 * è al momento detenuta da un altro client la richiesta fallisce.
 * @param storage   storage su cui effettuare l'operazione
 * @param filename  nome del file
 * @param client    id del client
 * @return un intero che indica se l'operazione è stata completata con successo oppure il tipo di errore verficatosi
 */
int fs_lockFile(storage_t* storage, char* filename, int client);

/**
 * @brief Rilascia la mutua esclusione sul file filename
 * @param storage   storage su cui effettuare l'operazione
 * @param filename  nome del file
 * @param client    id del client
 * @return un intero che indica se l'operazione è stata completata con successo oppure il tipo di errore verficatosi
 */
int fs_unlockFile(storage_t* storage, char *filename, int client);

/**
 * @brief Chiude il file per il client che ne ha fatto richiesta
 * @param storage   storage su cui eseguire la richiesta
 * @param filename  nome del file da chiudere
 * @param client    id del client
 * @return un intero che indica se l'operazione è stata completata con successo oppure il tipo di errore verficatosi
 */
int fs_closeFile(storage_t* storage, char* filename, int client);

/**
 * @brief Rimuove il file dallo storage verificando che l'utente abbia i permessi necessari.
 * Restituisce bytes rimossi viene restituito in deleted_bytes
 * @param storage        storage su cui eseguire l'operazione
 * @param filename       nome del file da cancellare
 * @param client         id del client
 * @param deleted_bytes  bytes rimossi
 * @return un intero che indica se l'operazione è stata completata con successo oppure il tipo di errore verficatosi
 */
int fs_removeFile(storage_t* storage, char* filename, int client, unsigned long *deleted_bytes);

/**
 * @brief Dealloca un file
 * @param file - puntatore al file da deallocare
 */
void fs_filedestroy(file_t *file);

/**
 * @brief Crea un file
 * @param filename      nome del file
 * @param size          dimensione del file
 * @param content       contenuto del file
 * @param flags         flags che indicano come si vuole creare il file (O_CREATE deve essere obbligatoriamente indicato)
 * @param client client che crea il file in modalità esclusiva (valore valido solo se è stato indicato il flag O_LOCK)
 * @return un puntatore al file appena creato
 */
file_t *fs_filecreate(char *filename, unsigned int size, void *content, int flags, int client);

/**
 * @brief Stampa le statistiche dello storage
 * @param storage storage di cui si vogliono stampare le statistiche
 */
void fs_stats(storage_t *storage);


#endif //FILE_STORAGE_SERVER_STORAGE_H
