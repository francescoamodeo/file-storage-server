
#include "storage.h"
#include "filestorage.h"
#include "protocol.h"


storage_t* fs_init(int max_files, unsigned long long max_capacity, int replace_mode) {

    if (max_files <= 0 || max_capacity <= 0)
        return NULL;

    storage_t *storage = (storage_t *) malloc(sizeof(storage_t));
    if (storage == NULL)
        return NULL;
    storage->files_limit = max_files;
    storage->memory_limit = max_capacity;
    storage->replace_mode = replace_mode;
    storage->files_number = 0;
    storage->occupied_memory = 0;
    //stats
    storage->max_files_number = 0;
    storage->max_occupied_memory = 0;
    storage->times_replacement_algorithm = 0;
    if (pthread_rwlock_init(storage->mutex, NULL) != 0) return NULL;
    storage->filenames_queue = list_init();
    if (storage->filenames_queue == NULL) {
        pthread_rwlock_destroy(storage->mutex);
        return NULL;
    }
    //creo la cache dei file
    storage->files = icl_hash_create((int) max_files, hash_pjw, string_compare);
    if (storage->files == NULL) {
        pthread_rwlock_destroy(storage->mutex);
        list_destroy(storage->filenames_queue);
        return NULL;
    }

    return storage;
}

//TODO prima di distruggerlo devo stampare le stats e chiudere le connessioni con i client che aspettavano lock
void fs_destroy(storage_t* storage) {

    if (!storage) return;

    if (pthread_rwlock_wrlock(storage->mutex) != 0) return;
    if (storage->files != NULL) {
        icl_hash_destroy(storage->files, free, (void (*)(void *)) fs_filedestroy);
        storage->files = NULL;
    }
    if (storage->filenames_queue != NULL) {
        list_destroy(storage->filenames_queue);
        storage->filenames_queue = NULL;
    }
    if (storage->mutex != NULL) {
        pthread_rwlock_destroy(storage->mutex);
        storage->mutex = NULL;
    }

    free(storage);
    storage = NULL;
}

//I file vuoti appena inseriti non causano nè subiscono espulsione
//poiché sono visti come file di peso pari a 0.
//L'espulsione avviene solo al momento della scrittura.
int fs_openFile(storage_t* storage, char* filename, int flags, int client) {

    if (!storage || !filename)
        return INVALID_ARGUMENT;
    if (flags < 0 || flags > 3)
        return INVALID_ARGUMENT;

    if (flags & O_CREATE) {
        //Se è indicato il flag O_CREATE devo verificare che il file non esista già
        //Acquisisco la mutua esclusione in scrittura per eventualmente aggiungere il file
        if (pthread_rwlock_wrlock(storage->mutex) != 0) return INTERNAL_ERROR;
        if (icl_hash_find(storage->files, (void *) filename) != NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
            return FILE_ALREADY_EXISTS;
        }

        //Se il file non esiste lo creo
        file_t *newfile = (file_t *) malloc(sizeof(file_t));
        if (newfile == NULL) return INTERNAL_ERROR;
        newfile->who_opened = list_init();
        newfile->content = NULL;
        if (pthread_rwlock_init(newfile->mutex, NULL) != 0) return INTERNAL_ERROR;
        newfile->size = 0;
        newfile->filename = strndup(filename, strlen(filename));
        int *clientobj = malloc(sizeof(int));
        if (clientobj == NULL) return INTERNAL_ERROR;
        *clientobj = client;
        list_add(newfile->who_opened, (void *) clientobj);

        //se è indicato il flag O_LOCK rendo il file esclusivo per il client
        if (flags & O_LOCK)
            newfile->client_locker = client;
        else
            newfile->client_locker = -1;

        //Lo aggiungo
        icl_hash_insert(storage->files, (void *) filename, (void *) newfile);
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;

        return OPEN_FILE_SUCCESS;
    }

    //Altrimenti procedo senza creazione del file
    //Posso acquisire la mutua esclusione come lettore dato che non modifico la struttura
    if (pthread_rwlock_rdlock(storage->mutex) != 0) return INTERNAL_ERROR;
    file_t *toOpen = NULL;
    //a questo punto non è stato indicato il flag O_CREATE e pertanto fallisco se il file non esiste
    if ((toOpen = icl_hash_find(storage->files, (void *) filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_FOUND;
    }
    //acquisisco la writer lock sul file perchè andrò a modificare gli attributi
    if (pthread_rwlock_wrlock(toOpen->mutex) != 0) return INTERNAL_ERROR;
    //rilascio la lock sullo storage perchè ho finito la ricerca del file
    if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;

    if (flags & O_LOCK) {
        //Se c'è il flag O_LOCK settato controllo se è possibile fare la lock del file
        if (toOpen->client_locker == -1 || toOpen->client_locker == client) {
            toOpen->client_locker = client; //Imposto il proprietario
        } else {
            //Altrimenti fallisco
            if (pthread_rwlock_unlock(toOpen->mutex) != 0) return INTERNAL_ERROR;
            return PERMISSION_DENIED;
        }
    }
    //Aggiungo il client alla lista di chi ha aperto il file solo se non lo aveva già aperto
    if (list_get(toOpen->who_opened, (void *) &client, compare_int) == NULL) {
        if (list_add(toOpen->who_opened, (void *) &client) == NULL) return INTERNAL_ERROR;
    } else return FILE_ALREADY_OPEN;
    if (pthread_rwlock_unlock(toOpen->mutex) != 0) return INTERNAL_ERROR;

    free(filename);
    return OPEN_FILE_SUCCESS;
}

int fs_readFile(storage_t *storage, char *filename, int client, void *buf, long long int *bytes_read) {

    if (!storage || !filename) {
        return INVALID_ARGUMENT;
    }

    *bytes_read = -1;
    //Prendo la read lock sullo storage
    if (pthread_rwlock_rdlock(storage->mutex) != 0) return INTERNAL_ERROR;
    //Cerco se è presente il file
    file_t *toRead;
    if ((toRead = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_FOUND;
    }
    //Prendo la read lock sul file e rilascio quella sullo storage
    if (pthread_rwlock_rdlock(toRead->mutex) != 0) return INTERNAL_ERROR;
    if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;

    //Controllo che il client abbia aperto il file
    if (list_get(toRead->who_opened, (void *) &client, compare_int) == NULL) {
        if (pthread_rwlock_unlock(toRead->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_OPENED;
    }
    //Il client ha aperto il file
    //Controllo che il file sia unlocked oppure locked dal client che ha richiesto la read
    if (toRead->client_locker == client || toRead->client_locker == -1) {
        //Posso leggere il file solo se il contenuto è > 0
        if (toRead->size == 0) {
            if (pthread_rwlock_unlock(toRead->mutex) != 0) return INTERNAL_ERROR;
            *bytes_read = 0;
            return FILE_EMPTY;
        }

        //Memorizzo il contenuto del file nel buffer e restituisco la quantità di bytes
        buf = malloc(toRead->size);
        if (!buf) return INTERNAL_ERROR;
        memcpy(buf, toRead->content, toRead->size);
        *bytes_read = toRead->size;
        if (pthread_rwlock_unlock(toRead->mutex) != 0) return INTERNAL_ERROR;
        return READ_FILE_SUCCESS;
    } else {
        //il file appartiene ad un altro client, non si può leggere
        if (pthread_rwlock_unlock(toRead->mutex) != 0) return INTERNAL_ERROR;
        return PERMISSION_DENIED;
    }
}

//Legge solo i file con contenuto e non locked, i file vuoti non vengono considerati
int fs_readNFiles(storage_t *storage, int client, int N, list_t *files_to_send, int* filecount) {

    if (!storage && !files_to_send) {
        return INVALID_ARGUMENT;
    }
    *filecount = -1;
    //Dato che devo leggere tutti i file entro in lettura
    if (pthread_rwlock_rdlock(storage->mutex) != 0) return INTERNAL_ERROR;
    if (N > storage->files_number || N <= 0)
        N = storage->files_number;

    //Per ogni nome di file nella lista dello storage vado a prendere il corrispondente
    //file dalla tabella e lo aggiungo alla lista files_to_send
    node_t *filename = list_gethead(storage->filenames_queue);
    if (!filename) { //non ci sono file con contenuto nello storage
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        *filecount = 0;
        return STORAGE_EMPTY;
    }

    while (*filecount < N) {
        //Cerco il file nello storage
        file_t *file_to_copy = icl_hash_find(storage->files, filename->data);
        file_t *file = NULL;
        if (file_to_copy) {
            //se il file esiste ne prendo la read lock
            if (pthread_rwlock_rdlock(file_to_copy->mutex) != 0) return INTERNAL_ERROR;
            //Se il file è locked dal client che chiede la lettura o è libero allora posso leggerlo
            if (file_to_copy->client_locker == -1 || file_to_copy->client_locker == client) {
                //lo leggo
                // Creo il file di copia
                file = (file_t *) malloc(sizeof(file_t));
                if (file == NULL) return INTERNAL_ERROR;
                memset(file, 0, sizeof(file_t));

                //oscuro gli attributi non rilevanti
                file->mutex = NULL;
                file->client_locker = -1;
                file->who_opened = NULL;

                //Alloco lo spazio
                file->size = file_to_copy->size;
                file->content = malloc(file_to_copy->size);
                if (!file->content) return INTERNAL_ERROR;
                file->filename = malloc(strlen(file_to_copy->filename) + 1 * sizeof(char));
                if (!file->filename) return INTERNAL_ERROR;

                //Copio il contenuto
                memcpy(file->content, file_to_copy->content, file_to_copy->size);
                memcpy(file->filename, file_to_copy->filename, strlen(file_to_copy->filename) + 1);

                //Aggiungo il file alla lista
                if (!list_add(files_to_send, file)) {
                    //problema interno alla lista
                    if (pthread_rwlock_unlock(file_to_copy->mutex) != 0) return INTERNAL_ERROR;
                    if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
                    fs_filedestroy(file);
                    list_destroy(files_to_send);
                    return INTERNAL_ERROR;
                }
                (*filecount)++;
            }
            //Rilascio read lock
            if (pthread_rwlock_unlock(file_to_copy->mutex) != 0) return INTERNAL_ERROR;
        } else {
            //il file è presente nella lista di filename ma non nel server. Inconsistenza
            return INTERNAL_ERROR;
        }
        filename = list_getnext(storage->filenames_queue, filename);
    }
    return READN_FILE_SUCCESS;
}

int fs_writeFile(storage_t* storage, char *filename, size_t file_size, void* file_content, int client, list_t *filesEjected) {

    if (!storage || !filename || !file_content || file_size <= 0 || !filesEjected)
        return INVALID_ARGUMENT;

    //Devo controllare che il file esista e che sia aperto e locked dal client
    //Tutto in modalità scrittore perché poi potrei modificare la struttura dati
    if (pthread_rwlock_wrlock(storage->mutex) != 0) return INTERNAL_ERROR;
    file_t *toWrite = NULL;
    if ((toWrite = icl_hash_find(storage->files, (void *) filename)) == NULL) {
        //Se il file non esiste
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_FOUND;
    }
    //Se esiste prendo la write lock
    if (pthread_rwlock_wrlock(toWrite->mutex) != 0) return INTERNAL_ERROR;
    //Controllo che il file non sia già stato scritto in passato
    if (toWrite->size > 0) {
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) return INTERNAL_ERROR;
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_ALREADY_WRITTEN;
    }
    //Controllo che la dimensione da scrivere non sia superiore al limite della memoria
    if (file_size > storage->memory_limit) {
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) return INTERNAL_ERROR;
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_TOO_BIG;
    }
    //Controllo che sia stato aperto
    if ((list_get(toWrite->who_opened, &client, compare_int)) == NULL) {
        //il client non ha aperto il file
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) return INTERNAL_ERROR;
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_OPENED;
    }
    //il client ha aperto il file, controllo sia anche il locker
    if (toWrite->client_locker != client) {
        //il client non è il locker
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) return INTERNAL_ERROR;
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return PERMISSION_DENIED;
    }


    //Rimpiazzamento file
    //Se aumentando di 1 il numero di file e aggiungendo la dimensione del file rimango nei limiti
    //allora non devo fare rimpiazzamenti
    while ((storage->files_number + 1 > storage->files_limit) ||
           (storage->occupied_memory + file_size > storage->memory_limit)) {

        //non c'è abbastanza spazio e devo liberare dei file
        //prendo il nome del primo file da eliminare (FIFO)
        node_t *elem = list_removehead(storage->filenames_queue);
        if (elem == NULL) return INTERNAL_ERROR; //inconsistenza storage

        char *filename_toEject = elem->data;
        //Vado a cercarlo nello storage
        file_t *toEject = icl_hash_find(storage->files, filename_toEject);
        if (toEject == NULL) return INTERNAL_ERROR; //inconsistenza storage

        //Ora ho il file da eliminare, prendo la write lock anche su di lui
        if (pthread_rwlock_wrlock(toEject->mutex) != 0) return INTERNAL_ERROR;
        file_t *toEject_copy = (file_t *) malloc(sizeof(file_t));
        if (toEject_copy == NULL) return INTERNAL_ERROR;
        //faccio copia del file da espellere
        toEject_copy->filename = strndup(toEject->filename, strlen(toEject->filename));
        toEject_copy->size = toEject->size;
        toEject_copy->content = malloc(toEject_copy->size);
        if (toEject_copy->content == NULL) return INTERNAL_ERROR;
        memcpy(toEject_copy->content, toEject->content, toEject_copy->size);
        //oscuro gli attributi non rilevanti
        toEject_copy->who_opened = NULL;
        toEject_copy->mutex = NULL;

        //Aggiungo alla lista dei file espulsi da spedire al client
        list_add(filesEjected, toEject_copy);

        //Modifico lo storage per l'eliminazione
        storage->occupied_memory -= toEject->size;
        storage->files_number--;
        storage->times_replacement_algorithm++;

        //Lo elimino dallo storage
        if (icl_hash_delete(storage->files, toEject->filename, free, (void *) fs_filedestroy) != 0)
            return INTERNAL_ERROR;

        if (filename_toEject)
            free(filename_toEject);
        free(elem);
    }

    //A questo punto c'è sufficiente spazio per ospitare il file e quindi lo scrivo nella cache
    toWrite->content = file_content;
    toWrite->size = file_size;
    //aggiungo il nome del file alla coda
    if (list_add(storage->filenames_queue, toWrite->filename) == NULL) return INTERNAL_ERROR;

    //modifico variabili dello storage
    storage->occupied_memory += file_size;
    storage->files_number++;
    //aggiorno le stats
    if (storage->occupied_memory > storage->max_occupied_memory)
        storage->max_occupied_memory = storage->occupied_memory;
    if (storage->files_number > storage->max_files_number)
        storage->max_files_number = storage->files_number;

    if (pthread_rwlock_unlock(toWrite->mutex) != 0) return INTERNAL_ERROR;
    if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;

    return WRITE_FILE_SUCCESS;
}

int fs_appendToFile(storage_t* storage, char* filename, size_t size, void* data, int client, list_t *filesEjected) {

    if (!storage || !filename || size <= 0 || !data || !filesEjected)
        return INVALID_ARGUMENT;

    if (pthread_rwlock_wrlock(storage->mutex) != 0) return INTERNAL_ERROR;
    //Controllo che la dimensione del contenuto non sia eccessivamente grande
    if (size > storage->memory_limit) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_TOO_BIG;
    }
    //Cerco il file
    file_t *toAppend = NULL;
    if ((toAppend = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_FOUND;
    }
    if (pthread_rwlock_wrlock(toAppend->mutex) != 0) return INTERNAL_ERROR;
    //il file è presente, controllo sia stato aperto dal client
    if (list_get(toAppend->who_opened, (void *) &client, compare_int) == NULL) {
        //il client non può accedere al file
        if (pthread_rwlock_unlock(toAppend->mutex) != 0) return INTERNAL_ERROR;
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_OPENED;
    }
    //controllo sia libero o locked dal client che ha fatto la richiesta
    if (toAppend->client_locker != -1 && toAppend->client_locker != client) {
        //Non si hanno i permessi per accedere al file
        if (pthread_rwlock_unlock(toAppend->mutex) != 0) return INTERNAL_ERROR;
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return PERMISSION_DENIED;
    }
    //Rimpiazzamento file
    //Se a aggiungendo la dimensione del file rimango nei limiti
    //allora non devo fare rimpiazzamenti
    while (storage->occupied_memory + size > storage->memory_limit) {

        //non c'è abbastanza spazio e devo liberare dei file
        //prendo il nome del primo file da eliminare (FIFO)
        //Vado a rimuovere dalla lista il primo elemento diverso dal file che devo scrivere
        node_t *elem = list_gethead(storage->filenames_queue);
        if (string_compare(elem->data, toAppend->filename) != 0)
            elem = list_removehead(storage->filenames_queue);
        else //salto il filename su cui stiamo facendo la append e prendo il successivo
            elem = list_remove(storage->filenames_queue, list_getnext(storage->filenames_queue, elem), string_compare);
        if (elem == NULL) return INTERNAL_ERROR; //Non devo entrare in questo ramo. Inconsistenza storage
        char *toEject_filename = elem->data;
        //Vado a cercarlo nello storage
        file_t *toEject = icl_hash_find(storage->files, toEject_filename);
        if (toEject == NULL) return INTERNAL_ERROR; //inconsistenza storage

        //Ora ho il file da eliminare, prendo la write lock anche su di lui
        if (pthread_rwlock_wrlock(toEject->mutex) != 0) return INTERNAL_ERROR;

        //Creo e aggiungo una copia del file alla lista dei file espulsi
        file_t *toEject_copy = (file_t *) malloc(sizeof(file_t));
        if (toEject_copy == NULL) return INTERNAL_ERROR;
        //faccio copia del file da espellere
        toEject_copy->filename = strndup(toEject->filename, strlen(toEject->filename));
        toEject_copy->size = toEject->size;
        toEject_copy->content = malloc(toEject_copy->size);
        if (toEject_copy->content == NULL) return INTERNAL_ERROR;
        memcpy(toEject_copy->content, toEject->content, toEject_copy->size);
        //oscuro gli attributi non  rilevanti
        toEject_copy->who_opened = NULL;
        toEject_copy->mutex = NULL;

        //Aggiungo alla lista
        list_add(filesEjected, toEject_copy);

        //Modifico lo storage per l'eliminazione
        storage->occupied_memory -= toEject->size;
        storage->files_number--;
        storage->times_replacement_algorithm++;

        //Lo elimino dallo storage
        if (icl_hash_delete(storage->files, toEject->filename, free, (void *) fs_filedestroy) != 0)
            return INTERNAL_ERROR;

        if (toEject_filename)
            free(toEject_filename);
        free(elem);
    }

    //A questo punto c'è sufficiente spazio per ospitare i nuovi dati e quindi faccio la append
    if ((toAppend->content = realloc(toAppend->content, toAppend->size + size)) == NULL)
        return INTERNAL_ERROR;
    memcpy(toAppend->content + toAppend->size, data, size);
    toAppend->size = toAppend->size + size;

    //modifico variabili dello storage
    storage->occupied_memory += size;
    if (storage->occupied_memory > storage->max_occupied_memory)
        storage->max_occupied_memory = storage->occupied_memory;

    if (pthread_rwlock_unlock(toAppend->mutex) != 0) return INTERNAL_ERROR;
    if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;

    return APPEND_FILE_SUCCESS;
}

int fs_lockFile(storage_t* storage, char* filename, int client) {

    if (!storage || !filename || client < 0)
        return INVALID_ARGUMENT;

    file_t *toLock;
    //Acquisisco read lock sullo storage
    if (pthread_rwlock_rdlock(storage->mutex) != 0) return INTERNAL_ERROR;

    //il file non esiste
    if ((toLock = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_FOUND;
    }

    //il file esiste, prendo la lock in scrittura sul file e rilascio lo storage
    if (pthread_rwlock_wrlock(toLock->mutex) != 0) return INTERNAL_ERROR;
    if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;

    //controllo se il client ha aperto il file
    if (list_get(toLock->who_opened, (void *) &client, compare_int) == NULL) {
        if (pthread_rwlock_unlock(toLock->mutex) != 0) return INTERNAL_ERROR;
        return PERMISSION_DENIED;
    }

    //il locker è già il client richiedente o è libero
    if (toLock->client_locker == client || toLock->client_locker == -1) {
        toLock->client_locker = client;
        if (pthread_rwlock_unlock(toLock->mutex) != 0) return INTERNAL_ERROR;
        return LOCK_FILE_SUCCESS;
    }

    //Un altro client è il locker //TODO
    //Non posso completare la richiesta adesso, libero il thread da questo task
    if (pthread_rwlock_unlock(toLock->mutex) != 0) return INTERNAL_ERROR;
    return PERMISSION_DENIED;
}

int fs_unlockFile(storage_t* storage, char *filename, int client) {

    if (!storage || !filename || client < 0)
        return INVALID_ARGUMENT;

    file_t *toLock;
    //Acquisisco read lock sullo storage
    if (pthread_rwlock_rdlock(storage->mutex) != 0) return INTERNAL_ERROR;

    //il file non esiste
    if ((toLock = icl_hash_find(storage->files, (void *) filename)) == NULL) {
        if (pthread_rwlock_rdlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_FOUND;
    }

    //il file esiste, prendo la lock in scrittura sul file e rilascio lo storage
    if (pthread_rwlock_wrlock(toLock->mutex) != 0) return INTERNAL_ERROR;
    if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;

    //controllo se il client ha aperto il file
    if (list_get(toLock->who_opened, (void *) &client, compare_int) == NULL) {
        if (pthread_rwlock_unlock(toLock->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_OPENED;
    }

    //Nessuno è il locker
    if (toLock->client_locker == -1) {
        if (pthread_rwlock_unlock(toLock->mutex) != 0) return INTERNAL_ERROR;
        return FILE_ALREADY_UNLOCKED;
    }

    //Un altro client è il locker
    if (toLock->client_locker != -1 && toLock->client_locker != client) {
        if (pthread_rwlock_unlock(toLock->mutex) != 0) return INTERNAL_ERROR;
        return PERMISSION_DENIED;
    }

    //il locker era il client richiedente
    if (toLock->client_locker == client) {
        toLock->client_locker = -1;
        if (pthread_rwlock_unlock(toLock->mutex) != 0) return INTERNAL_ERROR;
        return UNLOCK_FILE_SUCCESS;
    }

    //non dovremmo arrivare qui
    return INTERNAL_ERROR;
}

int fs_closeFile(storage_t* storage, char* filename, int client) {

    if (!storage || !filename)
        return INVALID_ARGUMENT;

    if (pthread_rwlock_rdlock(storage->mutex) != 0) return INTERNAL_ERROR;
    file_t *toClose;
    //Se il file che voglio chiudere non esiste
    if ((toClose = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_FOUND;
    }
    //Prendo write lock sul file e rilascio lo storage
    if (pthread_rwlock_wrlock(toClose->mutex) != 0) return INTERNAL_ERROR;
    if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;

    //Controllo che il client sia tra quelli che hanno aperto il file
    node_t *removedElem;
    if ((removedElem = list_remove(toClose->who_opened, (void *) &client, compare_int)) == NULL) {
        //operazione già effettuata
        if (pthread_rwlock_unlock(toClose->mutex) != 0) return INTERNAL_ERROR;
        return FILE_ALREADY_CLOSED;
    }
    //client correttamente rimosso dalla lista di chi ha aperto il file
    free(removedElem->data);
    free(removedElem);
    if (pthread_rwlock_unlock(toClose->mutex) != 0) return INTERNAL_ERROR;

    return CLOSE_FILE_SUCCESS;
}

int fs_removeFile(storage_t* storage, char* filename, int client, unsigned int* deleted_bytes) {

    if (!storage || !filename) {
        return INVALID_ARGUMENT;
    }

    //Modifico la struttura dello storage, quindi acquisisco la write lock
    if (pthread_rwlock_wrlock(storage->mutex) != 0) return INTERNAL_ERROR;
    file_t *toRemove;
    //Se il file non esiste
    if ((toRemove = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return FILE_NOT_FOUND;
    }
    //Se il file esiste prendo la write lock prima
    if (pthread_rwlock_wrlock(toRemove->mutex) != 0) return INTERNAL_ERROR;

    //Controllo se il client ha fatto la lock
    if (toRemove->client_locker != client) {
        if (pthread_rwlock_unlock(toRemove->mutex) != 0) return INTERNAL_ERROR;
        if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;
        return PERMISSION_DENIED;
    }

    //il client detiene la lock quindi posso procedere all'eliminazione del file
    if (toRemove->size > 0) {
        *deleted_bytes = toRemove->size;
        //Se il file aveva effettivamente un contenuto allora modifico i numero di file e la memoria occupata
        storage->occupied_memory -= toRemove->size;
        storage->files_number--;
        node_t *toRemove_elem = list_remove(storage->filenames_queue, filename, (void *) string_compare);
        if (!toRemove_elem) return INTERNAL_ERROR;
        free(toRemove_elem->data);
        free(toRemove_elem);
    }
    //Elimino il file dallo storage
    if (icl_hash_delete(storage->files, toRemove->filename, free, (void *) fs_filedestroy) != 0)
        return INTERNAL_ERROR;
    //Rilascio la lock sullo storage solo alla fine
    if (pthread_rwlock_unlock(storage->mutex) != 0) return INTERNAL_ERROR;

    return REMOVE_FILE_SUCCESS;
}

void fs_filedestroy(file_t *file) {

    if (!file) return;
    if (file->filename) free(file->filename);
    if (file->content) free((file->content));
    if (file->who_opened) list_destroy(file->who_opened);
    if (file->mutex) pthread_rwlock_destroy(file->mutex);

    file->size = -1;
    file->client_locker = -1;

    free(file);
    file = NULL;
}

void fs_stats(storage_t* storage) {
    if (!storage)
        return;

    printf("\nFile Storage Stats:\n");
    printf("    - MAX FILES REACHED: %d\n", storage->max_files_number);
    printf("    - MAX OCCUPIED CAPACITY REACHED: %f MB\n", ((double) storage->max_occupied_memory) / 1000000);
    printf("    - REPLACEMENT ALGORITM EXECUTED: %d TIMES\n", storage->times_replacement_algorithm);
    printf("    - FILES CURRENTLY STORED: %d\n", storage->files_number);
    list_tostring(storage->filenames_queue);
}
