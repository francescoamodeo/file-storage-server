#include <stdlib.h>

#include <storage.h>
#include <protocol.h>

int select_victims(int op, storage_t *storage, file_t *file, size_t file_size, list_t *filesEjected);
int eject_victims(storage_t *storage, list_t *filesEjected);

storage_t *fs_init(int max_files, size_t max_capacity, int replace_mode) {

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
int fs_openFile(storage_t *storage, char *filename, int flags, char *client) {

    if (!storage || !filename || !client)
        return EINVAL;
    if (flags < 0 || flags > 3)
        return EINVAL;

    int returnc;
    file_t *newfile = NULL;
    char *username = NULL;

    if (flags & O_CREATE) {
#if DEBUG
        printf("creo il file\n");
#endif
        //Se è indicato il flag O_CREATE devo verificare che il file non esista già
        //Acquisisco la mutua esclusione in scrittura per eventualmente aggiungere il file
        if (pthread_rwlock_wrlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        if (icl_hash_find(storage->files, (void *) filename) != NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            returnc = EEXIST;
            goto error;
        }
        //Se il file non esiste lo creo
        if ((newfile = fs_filecreate(filename, 0, NULL, flags, client)) == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            returnc = ECANCELED;
            goto error;
        }
        username = strndup(client, strlen(client));
        if (username == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            returnc = ECANCELED;
            goto error;
        }
        if (list_add(newfile->who_opened, username) == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            returnc = ECANCELED;
            goto error;
        }
#if DEBUG
        printf("%s aggiunto alla lista di chi ha aperto il file\n", username);
#endif
        //Lo aggiungo
        char *filename_key = NULL;
        if ((filename_key = strndup(filename, strlen(filename))) == NULL)
            return ECANCELED;
        if (icl_hash_insert(storage->files, (void *) filename_key, (void *) newfile) == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            free(filename_key);
            returnc = ECANCELED;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
    } else {
#if DEBUG
        printf("apro un file già esistente\n");
#endif
        //O_CREATE non indicato (il file dovrebbe già esistere per aprirlo)
        //Posso acquisire la mutua esclusione come lettore dato che non modifico la struttura
        if (pthread_rwlock_rdlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        file_t *toOpen = NULL;
        if ((toOpen = icl_hash_find(storage->files, filename)) == NULL) {
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            returnc = ENOENT;
            goto error;
        }
        //acquisisco la writer lock sul file perchè andrò a modificare gli attributi
        if (pthread_rwlock_wrlock(toOpen->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        //rilascio la lock sullo storage perchè ho finito la ricerca del file
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        //controllo che il file sia unlocked oppure il client possieda la lock sul file
        if (toOpen->client_locker != NULL && strcmp(toOpen->client_locker, client) != 0) {
            //il file appartiene ad un altro client, non si può leggere
            if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            returnc = EACCES;
            goto error;
        }
        //Aggiungo il client alla lista di chi ha aperto il file solo se non lo aveva già aperto
        if (list_get(toOpen->who_opened, client, (int (*)(void *, void *)) strcmp) != NULL) {
            if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            returnc = EALREADY;
            goto error;
        }
        username = strndup(client, strlen(client));
        if (username == NULL) {
            if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            returnc = ECANCELED;
            goto error;
        }

        if (list_add(toOpen->who_opened, username) == NULL) {
            if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            returnc = ECANCELED;
            goto error;
        }
#if DEBUG
        printf("%s aggiunto alla lista di chi ha aperto il file\n", username);
#endif
        if (flags & O_LOCK && toOpen->client_locker == NULL)
            toOpen->client_locker = username;

        if (pthread_rwlock_unlock(toOpen->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
    }

    return EXIT_SUCCESS;

    error:
    if (username) free(username);
    if (newfile) fs_filedestroy(newfile);
    return returnc;
}

int fs_readFile(storage_t *storage, char *filename, char *client, void **buf, size_t *bytes_read) {

    if (!storage || !filename || !bytes_read || !client)
        return EINVAL;

    int returnc;
    *bytes_read = 0;

    //Prendo la read lock sullo storage
    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Cerco se è presente il file
    file_t *toRead;
    if ((toRead = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ENOENT;
        goto error;
    }
    //Prendo la read lock sul file e rilascio quella sullo storage
    if (pthread_rwlock_rdlock(toRead->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Controllo che il client abbia aperto il file
    if (list_get(toRead->who_opened, client, (int (*)(void *, void *)) strcmp) == NULL) {
        if (pthread_rwlock_unlock(toRead->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EPERM;
        goto error;
    }
    //controllo che il file sia unlocked oppure il client possieda la lock sul file
    if (toRead->client_locker != NULL && strcmp(toRead->client_locker, client) != 0) {
        //il file appartiene ad un altro client, non si può leggere
        if (pthread_rwlock_unlock(toRead->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EACCES;
        goto error;
    }
    //posso procedere alla lettura
    //posso leggere il file solo se il contenuto è > 0
    if (toRead->size == 0) {
        if (pthread_rwlock_unlock(toRead->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ENODATA;
        goto error;
    }
    //Memorizzo il contenuto del file nel buffer e restituisco la quantità di bytes
    *buf = malloc(toRead->size);
    if (*buf == NULL) {
        if (pthread_rwlock_unlock(toRead->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ECANCELED;
        goto error;
    }
    memcpy(*buf, toRead->content, toRead->size);
    *bytes_read = toRead->size;
    if (pthread_rwlock_unlock(toRead->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }

    return EXIT_SUCCESS;

    error:
    if (*buf) free(*buf);
    return returnc;
}

//Legge solo i file con contenuto e non locked, i file vuoti non vengono considerati
int fs_readNFiles(storage_t *storage, char *client, int N, list_t *files_to_send) {

    if (!storage || !files_to_send || !client)
        return EINVAL;

    int returnc;
    file_t *file = NULL;

    //Dato che devo leggere tutti i file entro in lettura
    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    if (N > storage->files_number || N <= 0) N = storage->files_number;

    //Per ogni nome di file nella lista dello storage vado a prendere il corrispondente
    //file dalla cache e lo aggiungo alla lista files_to_send
    elem_t *filename = list_gethead(storage->filenames_queue);
    if (filename == NULL) { //non ci sono file con contenuto nello storage
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ENODATA;
        goto error;
    }

#if DEBUG
    printf("devo leggere %d files, lista length: %d, primo elemento: %s\n", N, files_to_send->length, (char*)filename->data);
#endif
    while (filename && files_to_send->length < N) {
        //Cerco il file nello storage
        file_t *file_to_copy = icl_hash_find(storage->files, filename->data);
        if (file_to_copy == NULL) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
#if DEBUG
        printf("vado a prendere la lock sul file %s\n", file_to_copy->filename);
#endif
        //se il file esiste ne prendo la read lock
        if (pthread_rwlock_rdlock(file_to_copy->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
#if DEBUG
        printf("leggo file %s\n", file_to_copy->filename);
        printf("client: %s, locker: %s\n",client, file_to_copy->client_locker);
#endif

        //Se il file è locked dal client che chiede la lettura o è libero allora posso leggerlo
        if (file_to_copy->client_locker == NULL || strcmp(file_to_copy->client_locker, client) == 0) {
            //faccio la copia del file
            if ((file = fs_filecreate(file_to_copy->filename, file_to_copy->size, file_to_copy->content, O_CREATE,
                                      NULL)) == NULL) {
                if (pthread_rwlock_unlock(file_to_copy->mutex) != 0) {
                    returnc = ENOTRECOVERABLE;
                    goto error;
                }
                if (pthread_rwlock_unlock(storage->mutex) != 0) {
                    returnc = ENOTRECOVERABLE;
                    goto error;
                }
                returnc = ECANCELED;
                goto error;
            }
#if DEBUG
            printf("copia creata\n");
#endif
            //Aggiungo il file alla lista
            if (list_add(files_to_send, file) == NULL) {
                //problema interno alla lista
                if (pthread_rwlock_unlock(file_to_copy->mutex) != 0) {
                    returnc = ENOTRECOVERABLE;
                    goto error;
                }
                if (pthread_rwlock_unlock(storage->mutex) != 0) {
                    returnc = ENOTRECOVERABLE;
                    goto error;
                }
                returnc = ECANCELED;
                goto error;
            }
#if DEBUG
            printf("aggiunto alla lista, list length: %d\n", files_to_send->length);
#endif
        }
        //Rilascio read lock
        if (pthread_rwlock_unlock(file_to_copy->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
#if DEBUG
        printf("unlock file %s\n", file_to_copy->filename);
#endif
        filename = list_getnext(storage->filenames_queue, filename);
    }
#if DEBUG
    printf("fuori dal while\n");
#endif

    //rilascio la read lock sullo storage
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }

    return EXIT_SUCCESS;

    error:
    if (file) fs_filedestroy(file);
    return returnc;
}

int fs_writeFile(storage_t *storage, char *filename, size_t file_size, void *file_content, char *client,
                 list_t *filesEjected) {

    if (!storage || !filename || !file_content || file_size <= 0 || !filesEjected || !client)
        return EINVAL;

    int returnc;
    char *toWrite_filename = NULL;

    //Devo controllare che il file esista e che sia aperto e locked dal client
    //Tutto in modalità scrittore perché poi potrei modificare la struttura dati
    if (pthread_rwlock_wrlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    file_t *toWrite = NULL;
    if ((toWrite = icl_hash_find(storage->files, filename)) == NULL) {
        //Se il file non esiste
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ENOENT;
        goto error;
    }
    //Se esiste prendo la write lock
    if (pthread_rwlock_wrlock(toWrite->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Controllo che il file non sia già stato scritto in passato
    if (toWrite->size > 0) {
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EALREADY;
        goto error;
    }
    //Controllo che la dimensione da scrivere non sia superiore al limite della memoria
    if (file_size > storage->memory_limit) {
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EFBIG;
        goto error;
    }
#if DEBUG
    printf("controllo che %s sia nella lista di chi ha aperto il file\n", client);
#endif
    //Controllo che sia stato aperto dal client che ha richiesto la scrittura
    if ((list_get(toWrite->who_opened, client, (int (*)(void *, void *)) strcmp)) == NULL) {
        //il client non ha aperto il file
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EPERM;
        goto error;
    }
    //il client ha aperto il file, controllo sia anche il locker
    if (strcmp(toWrite->client_locker, client) != 0) {
        //il client non è il locker
        if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EACCES;
        goto error;
    }

    printf("PRIMA DEL RIMPIAZZAMENTO: storage: %zu, storagefiles: %d, filesize: %zu\n", storage->occupied_memory, storage->files_number, file_size);
    //Rimpiazzamento file
    //Se aumentando di 1 il numero di file e aggiungendo la dimensione del file rimango nei limiti
    //allora non devo fare rimpiazzamenti
    if (storage->files_number + 1 > storage->files_limit || storage->occupied_memory + file_size > storage->memory_limit) {
        if ((returnc = select_victims(WRITE, storage, toWrite, file_size, filesEjected)) != EXIT_SUCCESS
            || (returnc = eject_victims(storage, filesEjected)) != EXIT_SUCCESS) {
            if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            goto error;
        }
        printf("VITTIME ELIMINATE\n");
    }
    //allochiamo lo spazio necessario per il contenuto del file
    if ((toWrite->content = malloc(file_size)) == NULL) {
        //inconsistenza perchè a questo punto abbiamo già espulso gli eventuali file per fare spazio al contenuto da
        //scrivere, quindi ci troveremo senza file scritto e con i file già espulsi
        returnc = ENOTRECOVERABLE;
        goto error;
    }
#if DEBUG
    printf("prima del disastro\n");
#endif
    //A questo punto c'è sufficiente spazio per ospitare il file e quindi lo scrivo nella cache
    memcpy(toWrite->content, file_content, file_size);
    toWrite->size = file_size;
    //aggiungo il nome del file alla coda
    if ((toWrite_filename = strndup(filename, strlen(filename))) == NULL)
        return ECANCELED;
    if (list_add(storage->filenames_queue, toWrite_filename) == NULL) {
        //il file è già all'interno dello storage ma non riusciamo a inserirlo nella lista
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    printf("HO SCRITTO IL FILE %s\n", toWrite_filename);
    printf("lunghezza lista: %d\n", storage->filenames_queue->length);
    //modifico variabili dello storage
    storage->occupied_memory += file_size;
    storage->files_number++;

    //aggiorno le stats
    if (storage->occupied_memory > storage->max_occupied_memory)
        storage->max_occupied_memory = storage->occupied_memory;
    if (storage->files_number > storage->max_files_number)
        storage->max_files_number = storage->files_number;

    if (pthread_rwlock_unlock(toWrite->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }

    return EXIT_SUCCESS;

    error:
    if (toWrite_filename) free(toWrite_filename);
    return returnc;
}

int fs_appendToFile(storage_t *storage, char *filename, size_t size, void *data, char *client, list_t *filesEjected) {

    if (!storage || !filename || size <= 0 || !data || !filesEjected || !client)
        return EINVAL;

    int returnc;

    if (pthread_rwlock_wrlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Cerco il file
    file_t *toAppend = NULL;
    if ((toAppend = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ENOENT;
        goto error;
    }
    if (pthread_rwlock_wrlock(toAppend->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Controllo che la dimensione finale non sia eccessivamente grande
    //in questo modo siamo sicuri che alla peggio rimpiazzeremo tutti i file e lasceremo dentro
    // solo toAppend con il nuovo contenuto
    if (toAppend->size + size > storage->memory_limit) {
        if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EFBIG;
        goto error;
    }
    //il file è presente, controllo sia stato aperto dal client
    if (list_get(toAppend->who_opened, client, (int (*)(void *, void *)) strcmp) == NULL) {
        if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EPERM;
        goto error;
    }
    //controllo sia libero o locked dal client che ha fatto la richiesta
    if (toAppend->client_locker != NULL && strcmp(toAppend->client_locker, client) != 0) {
        //Non si hanno i permessi per accedere al file
        if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EACCES;
        goto error;
    }

    //Rimpiazzamento file
    //Se a aggiungendo la dimensione del file rimango nei limiti
    //allora non devo fare rimpiazzamenti
    if (storage->occupied_memory + size > storage->memory_limit) {
        if ((returnc = select_victims(APPEND, storage, toAppend, size, filesEjected)) != EXIT_SUCCESS
            || (returnc = eject_victims(storage, filesEjected)) != EXIT_SUCCESS) {
            if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            if (pthread_rwlock_unlock(storage->mutex) != 0) {
                returnc = ENOTRECOVERABLE;
                goto error;
            }
            goto error;
        }
        printf("VITTIME ELIMINATE\n");
    }

    //A questo punto c'è sufficiente spazio per ospitare i nuovi dati e quindi faccio la append
    if ((toAppend->content = realloc(toAppend->content, toAppend->size + size)) == NULL) {
        //inconsistenza perchè ci ritroviamo con una append impossibile da completare
        //e gli eventuali file espulsi per fare spazio al nuovo contenuto del file
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    memcpy((unsigned char *) toAppend->content + toAppend->size, data, size);
    toAppend->size = toAppend->size + size;

    //modifico variabili dello storage
    storage->occupied_memory += size;
    //modifico stats
    if (storage->occupied_memory > storage->max_occupied_memory)
        storage->max_occupied_memory = storage->occupied_memory;

    if (pthread_rwlock_unlock(toAppend->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }

    return EXIT_SUCCESS;

    error:
    return returnc;
}

int fs_lockFile(storage_t *storage, char *filename, char *client) {

    if (!storage || !filename || !client)
        return EINVAL;

    int returnc;
    char *username = NULL;

    file_t *toLock;
    //Acquisisco read lock sullo storage
    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //il file non esiste
    if ((toLock = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ENOENT;
        goto error;
    }
    //il file esiste, prendo la lock in scrittura sul file e rilascio lo storage
    if (pthread_rwlock_wrlock(toLock->mutex) != 0) {
        returnc = ENOLCK;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }

    if (toLock->client_locker != NULL && strcmp(toLock->client_locker, client) != 0) {
        //Un altro client è il locker
        //Non posso completare la richiesta adesso, libero il thread da questo task
        if (pthread_rwlock_unlock(toLock->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }

        returnc = EBUSY;
        goto error;
    }

    username = strndup(client, strlen(client));
    if (username == NULL) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    toLock->client_locker = username;

    if (pthread_rwlock_unlock(toLock->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    
    return EXIT_SUCCESS;

    error:
    if (username) free(username);
    return returnc;
}

int fs_unlockFile(storage_t *storage, char *filename, char *client) {

    if (!storage || !filename || !client)
        return EINVAL;

    int returnc;
    file_t *toUnlock;

    //Acquisisco read lock sullo storage
    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //il file non esiste
    if ((toUnlock = icl_hash_find(storage->files, (void *) filename)) == NULL) {
        if (pthread_rwlock_rdlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ENOENT;
        goto error;
    }
    //il file esiste, prendo la lock in scrittura sul file e rilascio lo storage
    if (pthread_rwlock_wrlock(toUnlock->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Nessuno è il locker
    if (toUnlock->client_locker == NULL) {
        if (pthread_rwlock_unlock(toUnlock->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EALREADY;
        goto error;
    }
    //Un altro client è il locker
    if (toUnlock->client_locker != NULL && strcmp(toUnlock->client_locker, client) != 0) {
        if (pthread_rwlock_unlock(toUnlock->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EACCES;
        goto error;
    }
    //il locker era il client richiedente
    if (strcmp(toUnlock->client_locker, client) == 0) {
        free(toUnlock->client_locker);
        toUnlock->client_locker = NULL;
        if (pthread_rwlock_unlock(toUnlock->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }

        return EXIT_SUCCESS;
    }

    //non dovremmo arrivare qui
    return ECANCELED;

    error:
    return returnc;
}

int fs_closeFile(storage_t *storage, char *filename, char *client) {

    if (!storage || !filename || !client)
        return EINVAL;

    int returnc;
    file_t *toClose;

    if (pthread_rwlock_rdlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Se il file che voglio chiudere non esiste
    if ((toClose = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ENOENT;
        goto error;
    }
    //Prendo write lock sul file e rilascio lo storage
    if (pthread_rwlock_wrlock(toClose->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Controllo che il client sia tra quelli che hanno aperto il file
    elem_t *removedElem;
    if ((removedElem = list_remove(toClose->who_opened, client, (int (*)(void *, void *)) strcmp)) == NULL) {
        //operazione già effettuata
        if (pthread_rwlock_unlock(toClose->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EALREADY;
        goto error;
    }
    //client correttamente rimosso dalla lista di chi ha aperto il file
    free(removedElem->data);
    free(removedElem);
    if (pthread_rwlock_unlock(toClose->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }

    return EXIT_SUCCESS;

    error:
    return returnc;
}

int fs_removeFile(storage_t *storage, char *filename, char *client, size_t *deleted_bytes) {

    if (!storage || !filename || !deleted_bytes || !client)
        return EINVAL;

    int returnc;
    file_t *toRemove;

    //Modifico la struttura dello storage, quindi acquisisco la write lock
    if (pthread_rwlock_wrlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Se il file non esiste
    if ((toRemove = icl_hash_find(storage->files, filename)) == NULL) {
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = ENOENT;
        goto error;
    }
    //Se il file esiste prendo la write lock
    if (pthread_rwlock_wrlock(toRemove->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    //Controllo se il client ha fatto la lock
    if (strcmp(toRemove->client_locker, client) != 0) {
        if (pthread_rwlock_unlock(toRemove->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        if (pthread_rwlock_unlock(storage->mutex) != 0) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        returnc = EACCES;
        goto error;
    }

    //il client detiene la lock quindi posso procedere all'eliminazione del file
    if (toRemove->size > 0) {
        *deleted_bytes = toRemove->size;
        //Se il file aveva effettivamente un contenuto allora modifico il numero dei file e la memoria occupata
        storage->occupied_memory -= toRemove->size;
        storage->files_number--;
        elem_t *toRemove_elem = list_remove(storage->filenames_queue, filename, (int (*)(void *, void *)) strcmp);
        if (toRemove_elem == NULL) {
            returnc = ENOTRECOVERABLE;
            goto error;
        }
        free(toRemove_elem->data);
        free(toRemove_elem);
    }
    //Elimino il file dallo storage
    if (icl_hash_delete(storage->files, toRemove->filename, free, NULL) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    if (pthread_rwlock_unlock(toRemove->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) {
        returnc = ENOTRECOVERABLE;
        goto error;
    }

    fs_filedestroy(toRemove);
    return EXIT_SUCCESS;

    error:
    return returnc;
}

file_t *fs_filecreate(char *filename, unsigned int size, void *content, int flags, char *locker) {
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

    file->client_locker = NULL;
    if (flags & O_LOCK) {
        if (locker == NULL) return NULL;
        file->client_locker = strndup(locker, strlen(locker));
    }

    return file;
}

void fs_filedestroy(file_t *file) {

    if (!file) return;
    if (file->filename) free(file->filename);
    if (file->client_locker) free(file->client_locker);
    if (file->content) free((file->content));
    if (file->who_opened) list_destroy(file->who_opened, free);
    if (file->mutex) {
        pthread_rwlock_destroy(file->mutex);
        free(file->mutex);
    }
    file->size = -1;

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

int select_victims(int op, storage_t *storage, file_t *file, size_t file_size, list_t *filesEjected) {
    if (!storage || !file || !filesEjected || file_size < 0)
        return EINVAL;
    if (file_size > storage->memory_limit)
        return EINVAL;

    file_t *toEject_copy = NULL;
    int curr_files_number = storage->files_number;
    size_t curr_occupied_memory = storage->occupied_memory;
    elem_t *possible_victim;
    for ( possible_victim = list_gethead(storage->filenames_queue)
        ; (op == WRITE && curr_files_number + 1 > storage->files_limit) || (curr_occupied_memory + file_size > storage->memory_limit)
        ; possible_victim = list_getnext(storage->filenames_queue, possible_victim) ) {
#if DEBUG
        printf("cerco vittime\n");
#endif

        if (possible_victim == NULL) {
            printf("VITTIMA NULL!!!\n");
            //se siamo arrivati qui avevamo bisogno di liberare spazio, ma non abbiamo file da espellere -> inconsistenza
            return ENOTRECOVERABLE;
        }
        if (op == APPEND && strcmp(possible_victim->data, file->filename) == 0) continue;
        //non c'è abbastanza spazio e devo liberare dei file
        //prendo il nome del primo file da eliminare (FIFO)
        //Vado a cercarlo nello storage
        file_t *toEject = icl_hash_find(storage->files, possible_victim->data);
        //presente nella lista ma non nello storage -> inconsistenza
        if (toEject == NULL) return ENOTRECOVERABLE;
        if (pthread_rwlock_rdlock(toEject->mutex) != 0) return ENOTRECOVERABLE;
        if ((toEject_copy = fs_filecreate(toEject->filename, toEject->size, toEject->content, O_CREATE, NULL)) == NULL) {
            if (pthread_rwlock_unlock(toEject->mutex) != 0) return ENOTRECOVERABLE;
            return ECANCELED;
        }
        if (pthread_rwlock_unlock(toEject->mutex) != 0) {
            fs_filedestroy(toEject_copy);
            return ENOTRECOVERABLE;
        }
        //Aggiungo alla lista dei file espulsi da spedire al client
        if (list_add(filesEjected, toEject_copy) == NULL) {
            fs_filedestroy(toEject_copy);
            return ECANCELED;
        }
        curr_files_number--;
        curr_occupied_memory -= toEject_copy->size;
    }
    possible_victim = NULL;
    printf("VITTIME SELEZIONATE, list length: %d\n", filesEjected->length);
    return EXIT_SUCCESS;
}

int eject_victims(storage_t *storage, list_t *filesEjected){
    if (!storage || !filesEjected)
        return EINVAL;

    elem_t *toEject_file_elem = list_gethead(filesEjected);
    elem_t *victim = NULL;
    while (toEject_file_elem != NULL) {
#if DEBUG
        printf("cancello vittime\n");
#endif

        file_t *toEject_file = (file_t *) toEject_file_elem->data;
        victim = list_remove(storage->filenames_queue, toEject_file->filename, (int (*)(void *, void *)) strcmp);
        //presente nella lista da espellere ma non nella lista dello storage -> inconsistenza
        if (victim == NULL) return ENOTRECOVERABLE;

        //Prendo il riferimento nello storage
        file_t *toEject = icl_hash_find(storage->files, victim->data);
        //presente nella lista dello storage ma non nella cache dello storage -> inconsistenza
        if (toEject == NULL) return ENOTRECOVERABLE;

        if (pthread_rwlock_wrlock(toEject->mutex) != 0) return ENOTRECOVERABLE;
        //Lo elimino dallo storage
        if (icl_hash_delete(storage->files, toEject->filename, free, NULL) != 0)
            return ENOTRECOVERABLE;
        if (pthread_rwlock_unlock(toEject->mutex) != 0) return ENOTRECOVERABLE;

        //Modifico lo storage in seguito all'eliminazione
        storage->occupied_memory -= toEject->size;
        storage->files_number--;
        storage->times_replacement_algorithm++;

        fs_filedestroy(toEject);
        free(victim->data);
        free(victim);

        toEject_file_elem = list_getnext(filesEjected, toEject_file_elem);
    }
    return EXIT_SUCCESS;
}

