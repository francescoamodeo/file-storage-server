
#include "storage.h"
#include "filestorage.h"
#include "protocol.h"

storage_t *fs_init(int max_files, unsigned long long max_capacity, int replace_mode) {

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
#if DEBUG
    printf("prima di inzializzare la lock\n");
#endif
    storage->mutex = malloc(sizeof(pthread_rwlock_t));
    if (pthread_rwlock_init(storage->mutex, NULL) != 0) {
        free(storage->mutex);
        free(storage);
        return NULL;
    }
#if DEBUG
    printf("lock inizializzata\n");
#endif
    storage->filenames_queue = list_init();
    if (storage->filenames_queue == NULL) {
        fs_destroy(storage);
        return NULL;
    }
    //creo la cache dei file
    storage->files = icl_hash_create((int) max_files, hash_pjw, string_compare);
    if (storage->files == NULL) {
        fs_destroy(storage);
        return NULL;
    }

    storage->clients_awaiting = list_init();
    if (storage->clients_awaiting == NULL) {
        fs_destroy(storage);
        return NULL;
    }

    return storage;
}

//TODO prima di distruggerlo devo stampare le stats e chiudere le connessioni con i client che aspettavano lock
void fs_destroy(storage_t *storage) {

    if (!storage) return;

    //se fallisce la trylock vuol dire che qualcuno ha la lock sullo storage,
    //ma a questo punto quando vogliamo distruggerlo, nessuno dovrebbe più accedervi dato che
    //distruggiamo prima il threadpool
    if (pthread_rwlock_trywrlock(storage->mutex) != 0) return;
    if (storage->files != NULL) {
        icl_hash_destroy(storage->files, free, (void (*)(void *)) fs_filedestroy);
        storage->files = NULL;
    }
    if (storage->filenames_queue != NULL) {
        list_destroy(storage->filenames_queue, free);
        storage->filenames_queue = NULL;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) return;
    if (pthread_rwlock_destroy(storage->mutex) != 0) return;
    free(storage->mutex);

    free(storage);
    storage = NULL;
}

//I file vuoti appena inseriti non causano nè subiscono espulsione
//poiché sono visti come file di peso pari a 0.
//L'espulsione avviene solo al momento della scrittura.
int fs_openFile(storage_t *storage, char *pathname, int flags, int client) {

    if (!storage || !pathname || client < 0)
        return INVALID_ARGUMENT;
    if (flags < 0 || flags > 3)
        return INVALID_ARGUMENT;

    int returnc;
    char *filename = NULL;
    if ((filename = strndup(pathname, strlen(pathname))) == NULL)
        return OPEN_FILE_FAIL;

    int *clientobj = NULL;
    file_t *newfile = NULL;

    if (flags & O_CREATE) {
        //Se è indicato il flag O_CREATE devo verificare che il file non esista già
        //Acquisisco la mutua esclusione in scrittura per eventualmente aggiungere il file
        if (pthread_rwlock_wrlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (icl_hash_find(storage->files, (void *) filename) != NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = FILE_ALREADY_EXISTS;
            goto error;
        }
        //Se il file non esiste lo creo
        if ((newfile = fs_filecreate(filename, 0, NULL, flags, client)) == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = OPEN_FILE_FAIL;
            goto error;
        }
        clientobj = malloc(sizeof(int));
        if (clientobj == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = OPEN_FILE_FAIL;
            goto error;
        }
        *clientobj = client;
        if (list_add(newfile->who_opened, (void *) clientobj) == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = OPEN_FILE_FAIL;
            goto error;
        }
        //Lo aggiungo
        if (icl_hash_insert(storage->files, (void *) newfile->filename, (void *) newfile) == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = OPEN_FILE_FAIL;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
    }
    else { //O_CREATE non indicato (il file dovrebbe già esistere per aprirlo)
        //Posso acquisire la mutua esclusione come lettore dato che non modifico la struttura
        if (pthread_rwlock_rdlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        file_t *toOpen = NULL;
        if ((toOpen = icl_hash_find(storage->files, (void *) filename)) == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = FILE_NOT_FOUND;
            goto error;
        }
        //acquisisco la writer lock sul file perchè andrò a modificare gli attributi
        if (pthread_rwlock_wrlock(toOpen->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        //rilascio la lock sullo storage perchè ho finito la ricerca del file
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        //controllo che il file sia unlocked oppure il client possieda la lock sul file
        if (toOpen->client_locker != -1 && toOpen->client_locker != client) {
            //il file appartiene ad un altro client, non si può leggere
            if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = PERMISSION_DENIED;
            goto error;
        }
        //Aggiungo il client alla lista di chi ha aperto il file solo se non lo aveva già aperto
        if (list_get(toOpen->who_opened, (void *) &client, compare_int) != NULL) {
            if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = FILE_ALREADY_OPEN;
            goto error;
        }
        clientobj = malloc(sizeof(int));
        if (clientobj == NULL) {
            if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = OPEN_FILE_FAIL;
            goto error;
        }
        if (list_add(toOpen->who_opened, (void *) clientobj) == NULL) {
            if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = OPEN_FILE_FAIL;
            goto error;
        }
        if (flags & O_LOCK) toOpen->client_locker = client;

        if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
    }

    free(filename);
    return OPEN_FILE_SUCCESS;

    error:
    if (filename) free(filename);
    if (clientobj) free(clientobj);
    if (newfile) fs_filedestroy(newfile);
    return returnc;
}

int fs_readFile(storage_t *storage, char *pathname, int client, void *buf, unsigned long *bytes_read) {

    if (!storage || !pathname || !bytes_read || client < 0)
        return INVALID_ARGUMENT;

    int returnc;
    char *filename = NULL;
    if ((filename = strndup(pathname, strlen(pathname))) == NULL)
        return READ_FILE_FAIL;

    *bytes_read = -1;

    //Prendo la read lock sullo storage
    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //Cerco se è presente il file
    file_t *toRead;
    if ((toRead = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_FOUND;
        goto error;
    }
    //Prendo la read lock sul file e rilascio quella sullo storage
    if (pthread_rwlock_rdlock(toRead->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //Controllo che il client abbia aperto il file
    if (list_get(toRead->who_opened, (void *) &client, compare_int) == NULL) {
        if (pthread_rwlock_unlock(toRead->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_OPENED;
        goto error;
    }
    //controllo che il file sia unlocked oppure il client possieda la lock sul file
    if (toRead->client_locker != -1 && toRead->client_locker != client) {
        //il file appartiene ad un altro client, non si può leggere
        if (pthread_rwlock_unlock(toRead->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = PERMISSION_DENIED;
        goto error;
    }
    //posso procedere alla lettura
    //posso leggere il file solo se il contenuto è > 0
    if (toRead->size == 0) {
        if (pthread_rwlock_unlock(toRead->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        *bytes_read = 0;
        returnc = FILE_EMPTY;
        goto error;
    }
    //Memorizzo il contenuto del file nel buffer e restituisco la quantità di bytes
    buf = malloc(toRead->size);
    if (buf == NULL) {
        if (pthread_rwlock_unlock(toRead->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = READ_FILE_FAIL;
        goto error;
    }
    memcpy(buf, toRead->content, toRead->size);
    *bytes_read = toRead->size;
    if (pthread_rwlock_unlock(toRead->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }

    free(filename);
    return READ_FILE_SUCCESS;

    error:
    if (filename) free(filename);
    if (buf) free(buf);
    return returnc;
}

//Legge solo i file con contenuto e non locked, i file vuoti non vengono considerati
int fs_readNFiles(storage_t *storage, int client, int N, list_t *files_to_send, int *filecount) {

    if (!storage || !files_to_send || !filecount || client < 0)
        return INVALID_ARGUMENT;

    int returnc;
    file_t *file = NULL;
    //mi aspetto gli argomenti filecount e filetosend già inizializzati e pertanto sarà compito del chiamante
    //liberare la memoria
    *filecount = -1;

    //Dato che devo leggere tutti i file entro in lettura
    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    if (N > storage->files_number || N <= 0) N = storage->files_number;

    //Per ogni nome di file nella lista dello storage vado a prendere il corrispondente
    //file dalla cache e lo aggiungo alla lista files_to_send
    elem_t *filename = list_gethead(storage->filenames_queue);
    if (filename == NULL) { //non ci sono file con contenuto nello storage
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        *filecount = 0;
        returnc = STORAGE_EMPTY;
        goto error;
    }

    while (filename && *filecount < N) {
        //Cerco il file nello storage
        file_t *file_to_copy = icl_hash_find(storage->files, filename->data);
        if (file_to_copy == NULL) {
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        //se il file esiste ne prendo la read lock
        if (pthread_rwlock_rdlock(file_to_copy->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        //Se il file è locked dal client che chiede la lettura o è libero allora posso leggerlo
        if (file_to_copy->client_locker == -1 || file_to_copy->client_locker == client) {
            //faccio la copia del file
            if ((file = fs_filecreate(file_to_copy->filename, file_to_copy->size, file_to_copy->content, O_CREATE,
                                      -1)) == NULL) {
                if (pthread_rwlock_unlock(file_to_copy->mutex) != 0) {
                    returnc = INTERNAL_ERROR;
                    goto error;
                }
                if (pthread_rwlock_unlock(storage->mutex) != 0) {
                    returnc = INTERNAL_ERROR;
                    goto error;
                }
                returnc = READN_FILE_FAIL;
                goto error;
            }
            //Aggiungo il file alla lista
            if (list_add(files_to_send, file) == NULL) {
                //problema interno alla lista
                if (pthread_rwlock_unlock(file_to_copy->mutex) != 0) {
                    returnc = INTERNAL_ERROR;
                    goto error;
                }
                if (pthread_rwlock_unlock(storage->mutex) != 0) {
                    returnc = INTERNAL_ERROR;
                    goto error;
                }
                returnc = READN_FILE_FAIL;
                goto error;
            }
            (*filecount)++;
        }
        //Rilascio read lock
        if (pthread_rwlock_unlock(file_to_copy->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        filename = list_getnext(storage->filenames_queue, filename);
    }
    //rilascio la read lock sullo storage
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }

    return READN_FILE_SUCCESS;

    error:
    if (file) fs_filedestroy(file);
    return returnc;
}

int fs_writeFile(storage_t *storage, char *pathname, size_t file_size, void *file_content, int client,
                 list_t *filesEjected) {

    if (!storage || !pathname || !file_content || file_size <= 0 || !filesEjected || client < 0)
        return INVALID_ARGUMENT;

    int returnc;
    char *filename = NULL;
    if ((filename = strndup(pathname, strlen(pathname))) == NULL)
        return WRITE_FILE_FAIL;

    file_t *toEject_copy = NULL;

    //Devo controllare che il file esista e che sia aperto e locked dal client
    //Tutto in modalità scrittore perché poi potrei modificare la struttura dati
    if (pthread_rwlock_wrlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    file_t *toWrite = NULL;
#if DEBUG
    printf("pathname dentro writeFile: %s\n", filename);
#endif
    if ((toWrite = icl_hash_find(storage->files, (void *) filename)) == NULL) {
        //Se il file non esiste
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_FOUND;
        goto error;
    }
    //Se esiste prendo la write lock
    if (pthread_rwlock_wrlock(toWrite->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //Controllo che il file non sia già stato scritto in passato
    if (toWrite->size > 0) {
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_ALREADY_WRITTEN;
        goto error;
    }
    //Controllo che la dimensione da scrivere non sia superiore al limite della memoria
    if (file_size > storage->memory_limit) {
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_TOO_BIG;
        goto error;
    }
    //Controllo che sia stato aperto dal client che ha richiesto la scrittura
    if ((list_get(toWrite->who_opened, &client, compare_int)) == NULL) {
        //il client non ha aperto il file
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_OPENED;
        goto error;
    }
    //il client ha aperto il file, controllo sia anche il locker
    if (toWrite->client_locker != client) {
        //il client non è il locker
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = PERMISSION_DENIED;
        goto error;
    }

    //Rimpiazzamento file
    //Se aumentando di 1 il numero di file e aggiungendo la dimensione del file rimango nei limiti
    //allora non devo fare rimpiazzamenti
    elem_t * possible_victim = list_gethead(storage->filenames_queue);
    while ((storage->files_number + 1 > storage->files_limit) ||
           (storage->occupied_memory + file_size > storage->memory_limit)) {

        //non c'è abbastanza spazio e devo liberare dei file
        //prendo il nome del primo file da eliminare (FIFO)
        if (possible_victim == NULL) {
            //se siamo arrivati qui avevamo bisogno di liberare spazio, ma non abbiamo file da espellere -> inconsistenza
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        //Vado a cercarlo nello storage
        file_t *toEject = icl_hash_find(storage->files, possible_victim->data);
        if (toEject == NULL) {
            //presente nella lista ma non nello storage -> inconsistenza
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        if (pthread_rwlock_rdlock(toEject->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if ((toEject_copy = fs_filecreate(toEject->filename, toEject->size, toEject->content, O_CREATE, -1)) == NULL) {
            if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            if (pthread_rwlock_unlock(toEject->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = WRITE_FILE_FAIL;
            goto error;
        }
        if (pthread_rwlock_unlock(toEject->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        //Aggiungo alla lista dei file espulsi da spedire al client
        if (list_add(filesEjected, toEject_copy) == NULL) {
            //problema interno alla lista
            if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            returnc = WRITE_FILE_FAIL;
            goto error;
        }
        possible_victim = list_getnext(storage->filenames_queue, possible_victim);
    }
    possible_victim = NULL;

    //dopo che ho selezionato le vittime e preparato la lista da spedire al client senza errori posso
    //procedere all'eliminazione
    elem_t *toEject_file_elem = list_gethead(filesEjected);
    elem_t *victim = NULL;
    while (toEject_file_elem != NULL) {

        file_t *toEject_file = (file_t*) toEject_file_elem->data;
        victim = list_remove(storage->filenames_queue, toEject_file->filename, string_compare);
        if (victim == NULL) {
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        //Prendo il riferimento nello storage
        file_t *toEject = icl_hash_find(storage->files, victim->data);
        if (toEject == NULL) {
            //presente nella lista ma non nello storage -> inconsistenza
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        if (pthread_rwlock_wrlock(toEject->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        //Lo elimino dallo storage
        if (icl_hash_delete(storage->files, toEject->filename, free, NULL) != 0) {
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        //posso fare la unlock tranquillamente perchè adesso nessun thread avrà il riferimento al file appena espulso
        if (pthread_rwlock_unlock(toEject->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        //Modifico lo storage per l'eliminazione
        storage->occupied_memory -= toEject->size;
        storage->files_number--;
        storage->times_replacement_algorithm++;

        fs_filedestroy(toEject);
        free(victim->data);
        free(victim);

        toEject_file_elem = list_getnext(filesEjected, toEject_file_elem);
    }

    //allochiamo lo spazio necessario per il contenuto del file
    if((toWrite->content = malloc(sizeof(file_size))) == NULL) {
        //inconsistenza perchè a questo punto abbiamo già espulso gli eventuali file per fare spazio al contenuto da
        //scrivere, quindi ci troveremo senza file scritto e con i file già espulsi
        returnc = INCONSISTENT_STATE;
        goto error;
    }
    //A questo punto c'è sufficiente spazio per ospitare il file e quindi lo scrivo nella cache
    memcpy(toWrite->content, file_content, file_size);
    toWrite->size = file_size;
    //aggiungo il nome del file alla coda
    if (list_add(storage->filenames_queue, toWrite->filename) == NULL) {
        //il file è già all'interno dello storage ma non riusciamo a inserirlo nella lista
        returnc = INCONSISTENT_STATE;
        goto error;
    }
    //modifico variabili dello storage
    storage->occupied_memory += file_size;
    storage->files_number++;

    //aggiorno le stats
    if (storage->occupied_memory > storage->max_occupied_memory)
        storage->max_occupied_memory = storage->occupied_memory;
    if (storage->files_number > storage->max_files_number)
        storage->max_files_number = storage->files_number;

    if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }

    free(filename);
    return WRITE_FILE_SUCCESS;

    error:
    if (filename) free(filename);
    if (toEject_copy) fs_filedestroy(toEject_copy);
    return returnc;
}

int fs_appendToFile(storage_t *storage, char *pathname, size_t size, void *data, int client, list_t *filesEjected) {

    if (!storage || !pathname || size <= 0 || !data || !filesEjected || client < 0)
        return INVALID_ARGUMENT;

    int returnc;
    char *filename = NULL;
    if ((filename = strndup(pathname, strlen(pathname))) == NULL)
        return APPEND_FILE_FAIL;

    file_t *toEject_copy = NULL;

    if (pthread_rwlock_wrlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //Cerco il file
    file_t *toAppend = NULL;
    if ((toAppend = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_FOUND;
        goto error;
    }
    if (pthread_rwlock_wrlock(toAppend->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //Controllo che la dimensione finale non sia eccessivamente grande
    //in questo modo siamo sicuri che alla peggio rimpiazzeremo tutti i file e lasceremo dentro
    // solo toAppend con il nuovo contenuto
    if (toAppend->size + size > storage->memory_limit) {
        if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_TOO_BIG;
        goto error;
    }
    //il file è presente, controllo sia stato aperto dal client
    if (list_get(toAppend->who_opened, (void *) &client, compare_int) == NULL) {
        if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_OPENED;
        goto error;
    }
    //controllo sia libero o locked dal client che ha fatto la richiesta
    if (toAppend->client_locker != -1 && toAppend->client_locker != client) {
        //Non si hanno i permessi per accedere al file
        if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = PERMISSION_DENIED;
        goto error;
    }

    //Rimpiazzamento file
    //Se a aggiungendo la dimensione del file rimango nei limiti
    //allora non devo fare rimpiazzamenti
    elem_t *possible_victim = list_gethead(storage->filenames_queue);
    while (storage->occupied_memory + size > storage->memory_limit) {

        //non c'è abbastanza spazio e devo liberare dei file
        //prendo il nome del primo file da eliminare (FIFO)
        //Vado a rimuovere dalla lista il primo elemento diverso dal file che devo scrivere
        if (possible_victim == NULL) {
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        if (string_compare(possible_victim->data, toAppend->filename) != 0) {
            //Vado a cercarlo nello storage
            file_t *toEject = icl_hash_find(storage->files, possible_victim->data);
            if (toEject == NULL) {
                returnc = INCONSISTENT_STATE;
                goto error;
            }
            if (pthread_rwlock_rdlock(toEject->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            //Creo e aggiungo una copia del file alla lista dei file espulsi
            if ((toEject_copy = fs_filecreate(toEject->filename, toEject->size, toEject->content, O_CREATE, -1)) ==
                NULL) {
                if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
                    returnc = INTERNAL_ERROR;
                    goto error;
                }
                if (pthread_rwlock_unlock(toEject->mutex) != 0) {
                    returnc = INTERNAL_ERROR;
                    goto error;
                }
                if (pthread_rwlock_unlock(storage->mutex) != 0) {
                    returnc = INTERNAL_ERROR;
                    goto error;
                }
                returnc = APPEND_FILE_FAIL;
                goto error;
            }
            if (pthread_rwlock_unlock(toEject->mutex) != 0) {
                returnc = INTERNAL_ERROR;
                goto error;
            }
            //Aggiungo alla lista
            if (list_add(filesEjected, toEject_copy) == NULL) {
                if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
                    returnc = INTERNAL_ERROR;
                    goto error;
                }
                if (pthread_rwlock_unlock(storage->mutex) != 0) {
                    returnc = INTERNAL_ERROR;
                    goto error;
                }
                returnc = APPEND_FILE_FAIL;
                goto error;
            }
        }
        possible_victim = list_getnext(storage->filenames_queue, possible_victim);
    }
    possible_victim = NULL;

    //procedo all'eliminazione
    elem_t *toEject_file_elem = list_gethead(filesEjected);
    elem_t *victim = NULL;
    while (toEject_file_elem != NULL) {

        file_t *toEject_file = (file_t *) toEject_file_elem->data;
        victim = list_remove(storage->filenames_queue, toEject_file->filename, string_compare);
        if (victim == NULL) {
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        //prendo il riferimento nello storage
        file_t *toEject = icl_hash_find(storage->files, victim->data);
        if (toEject == NULL) {
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        if (pthread_rwlock_wrlock(toEject->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (icl_hash_delete(storage->files, toEject->filename, free, NULL) != 0) {
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        //posso fare la unlock tranquillamente perchè adesso nessun thread avrà il riferimento al file appena espulso
        if (pthread_rwlock_unlock(toEject->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        //Modifico lo storage per l'eliminazione
        storage->occupied_memory -= toEject->size;
        storage->files_number--;
        storage->times_replacement_algorithm++;

        fs_filedestroy(toEject);
        free(victim->data);
        free(victim);

        toEject_file_elem = list_getnext(filesEjected, toEject_file_elem);
    }

    //A questo punto c'è sufficiente spazio per ospitare i nuovi dati e quindi faccio la append
    if ((toAppend->content = realloc(toAppend->content, toAppend->size + size)) == NULL) {
        //inconsistenza perchè ci ritroviamo con una append impossibile da completare
        //e gli eventuali file espulsi per fare spazio al nuovo contenuto del file
        returnc = INCONSISTENT_STATE;
        goto error;
    }
    memcpy(toAppend->content + toAppend->size, data, size);
    toAppend->size = toAppend->size + size;

    //modifico variabili dello storage
    storage->occupied_memory += size;
    //modifico stats
    if (storage->occupied_memory > storage->max_occupied_memory)
        storage->max_occupied_memory = storage->occupied_memory;

    if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }

    free(filename);
    return APPEND_FILE_SUCCESS;

    error:
    if (filename) free(filename);
    if (toEject_copy) fs_filedestroy(toEject_copy);
    return returnc;
}

int fs_lockFile(storage_t *storage, char *pathname, int client) {

    if (!storage || !pathname || client < 0)
        return INVALID_ARGUMENT;

    int returnc;
    char *filename = NULL;
    if ((filename = strndup(pathname, strlen(pathname))) == NULL)
        return LOCK_FILE_FAIL;

    file_t *toLock;
    //Acquisisco read lock sullo storage
    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //il file non esiste
    if ((toLock = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_FOUND;
        goto error;
    }
    //il file esiste, prendo la lock in scrittura sul file e rilascio lo storage
    if (pthread_rwlock_wrlock(toLock->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //controllo se il client ha aperto il file
    if (list_get(toLock->who_opened, (void *) &client, compare_int) == NULL) {
        if (pthread_rwlock_unlock(toLock->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = PERMISSION_DENIED;
        goto error;
    }
    //il locker è già il client richiedente o è libero
    if (toLock->client_locker == -1 && toLock->client_locker != client) {
        //Un altro client è il locker //TODO completare nel worker il fallimento lock
        //Non posso completare la richiesta adesso, libero il thread da questo task
        if (pthread_rwlock_unlock(toLock->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }

        returnc = UNLOCK_FILE_FAIL;
        goto error;
    }

    toLock->client_locker = client;
    if (pthread_rwlock_unlock(toLock->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }

    free(filename);
    return LOCK_FILE_SUCCESS;

    error:
    if (filename) free(filename);
    return returnc;
}

int fs_unlockFile(storage_t *storage, char *pathname, int client) {

    if (!storage || !pathname || client < 0)
        return INVALID_ARGUMENT;

    int returnc;
    char *filename = NULL;
    if ((filename = strndup(pathname, strlen(pathname))) == NULL)
        return UNLOCK_FILE_FAIL;

    file_t *toUnlock;
    //Acquisisco read lock sullo storage
    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //il file non esiste
    if ((toUnlock = icl_hash_find(storage->files, (void *) filename)) == NULL) {
        if (pthread_rwlock_rdlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_FOUND;
        goto error;
    }
    //il file esiste, prendo la lock in scrittura sul file e rilascio lo storage
    if (pthread_rwlock_wrlock(toUnlock->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //controllo se il client ha aperto il file
    if (list_get(toUnlock->who_opened, (void *) &client, compare_int) == NULL) {
        if (pthread_rwlock_unlock(toUnlock->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_OPENED;
        goto error;
    }
    //Nessuno è il locker
    if (toUnlock->client_locker == -1) {
        if (pthread_rwlock_unlock(toUnlock->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_ALREADY_UNLOCKED;
        goto error;
    }
    //Un altro client è il locker
    if (toUnlock->client_locker != -1 && toUnlock->client_locker != client) {
        if (pthread_rwlock_unlock(toUnlock->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = PERMISSION_DENIED;
        goto error;
    }
    //il locker era il client richiedente
    if (toUnlock->client_locker == client) {
        toUnlock->client_locker = -1;
        if (pthread_rwlock_unlock(toUnlock->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        free(filename);
        return UNLOCK_FILE_SUCCESS;
    }

    //non dovremmo arrivare qui
    free(filename);
    return UNLOCK_FILE_FAIL;

    error:
    if (filename) free(filename);
    return returnc;
}

int fs_closeFile(storage_t *storage, char *pathname, int client) {

    if (!storage || !pathname || client < 0)
        return INVALID_ARGUMENT;

    int returnc;
    char *filename = NULL;
    if ((filename = strndup(pathname, strlen(pathname))) == NULL)
        return CLOSE_FILE_FAIL;

    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    file_t *toClose;
    //Se il file che voglio chiudere non esiste
    if ((toClose = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_FOUND;
        goto error;
    }
    //Prendo write lock sul file e rilascio lo storage
    if (pthread_rwlock_wrlock(toClose->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //Controllo che il client sia tra quelli che hanno aperto il file
    elem_t *removedElem;
    if ((removedElem = list_remove(toClose->who_opened, (void *) &client, compare_int)) == NULL) {
        //operazione già effettuata
        if (pthread_rwlock_unlock(toClose->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_ALREADY_CLOSED;
        goto error;
    }
    //client correttamente rimosso dalla lista di chi ha aperto il file
    free(removedElem->data);
    free(removedElem);
    if (pthread_rwlock_unlock(toClose->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }

    free(filename);
    return CLOSE_FILE_SUCCESS;

    error:
    if (filename) free(filename);
    return returnc;
}

int fs_removeFile(storage_t *storage, char *pathname, int client, unsigned long *deleted_bytes) {

    if (!storage || !pathname || !deleted_bytes || client < 0)
        return INVALID_ARGUMENT;

    int returnc;
    char *filename = NULL;
    if ((filename = strndup(pathname, strlen(pathname))) == NULL)
        return REMOVE_FILE_FAIL;

    //Modifico la struttura dello storage, quindi acquisisco la write lock
    if (pthread_rwlock_wrlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    file_t *toRemove;
    //Se il file non esiste
    if ((toRemove = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = FILE_NOT_FOUND;
        goto error;
    }
    //Se il file esiste prendo la write lock
    if (pthread_rwlock_wrlock(toRemove->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    //Controllo se il client ha fatto la lock
    if (toRemove->client_locker != client) {
        if (pthread_rwlock_unlock(toRemove->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = INTERNAL_ERROR;
            goto error;
        }
        returnc = PERMISSION_DENIED;
        goto error;
    }

    //il client detiene la lock quindi posso procedere all'eliminazione del file
    if (toRemove->size > 0) {
        *deleted_bytes = toRemove->size;
        //Se il file aveva effettivamente un contenuto allora modifico il numero dei file e la memoria occupata
        storage->occupied_memory -= toRemove->size;
        storage->files_number--;
        elem_t *toRemove_elem = list_remove(storage->filenames_queue, filename, string_compare);
        if (toRemove_elem == NULL) {
            returnc = INCONSISTENT_STATE;
            goto error;
        }
        free(toRemove_elem->data);
        free(toRemove_elem);
    }
    //Elimino il file dallo storage
    if (icl_hash_delete(storage->files, toRemove->filename, free, NULL) != 0) {
        returnc = INCONSISTENT_STATE;
        goto error;
    }
    if (pthread_rwlock_unlock(toRemove->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = INTERNAL_ERROR;
        goto error;
    }

    fs_filedestroy(toRemove);
    free(filename);
    return REMOVE_FILE_SUCCESS;

    error:
    if (filename) free(filename);
    return returnc;
}

file_t *fs_filecreate(char *filename, unsigned int size, void *content, int flags, int client) {
    if (!filename || size < 0 || flags < 0 || flags > 3)
        return NULL;
    if (!(flags & O_CREATE))
        return NULL;

    file_t *file = (file_t *) malloc(sizeof(file_t));
    if (file == NULL) return NULL;

    file->filename = strndup(filename, strlen(filename));
    file->size = size;
    file->who_opened = list_init();

    file->mutex = malloc(sizeof(pthread_rwlock_t));
    if (!file->mutex) {
        fs_filedestroy(file);
        return NULL;
    }
    if (pthread_rwlock_init(file->mutex, NULL) != 0) {
        free(file->mutex); //in questo modo, evito di fare nella filedestroy la mutexdestroy di un mutex non inizializzato
        fs_filedestroy(file);
        return NULL;
    }

    file->content = NULL;
    if (size > 0) {
        file->content = malloc(size);
        if (file->content == NULL) {
            fs_filedestroy(file);
            return NULL;
        }
        memcpy(file->content, content, size);
    }

    file->client_locker = -1;
    if (flags & O_LOCK)
        file->client_locker = client;

    return file;
}

void fs_filedestroy(file_t *file) {

    if (!file) return;
    if (file->filename) free(file->filename);
    if (file->content) free((file->content));
    if (file->who_opened) list_destroy(file->who_opened, free);
    if (file->mutex) {
        pthread_rwlock_destroy(file->mutex);
        free(file->mutex);
    }

    file->size = -1;
    file->client_locker = -1;

    free(file);
    file = NULL;
}

void fs_stats(storage_t *storage) {
    if (!storage)
        return;

    printf("\nFile Storage Stats:\n");
    printf("    - MAX FILES REACHED: %d\n", storage->max_files_number);
    printf("    - MAX OCCUPIED CAPACITY REACHED: %f MB\n", ((double) storage->max_occupied_memory) / 1000000);
    printf("    - REPLACEMENT ALGORITM EXECUTED: %d TIMES\n", storage->times_replacement_algorithm);
    printf("    - FILES CURRENTLY STORED: %d\n", storage->files_number);
    list_tostring(storage->filenames_queue);
}

