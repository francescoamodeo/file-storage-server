#include <stdlib.h>

#include <worker.h>
#include <manager.h>
#include <storage.h>

extern storage_t *storage;
extern int fdpipe[2];

void requesthandler(int *clientfd){

    int fd = *clientfd;
    free(clientfd);

    msg_t *request = NULL;
    if ((request = initmsg()) == NULL) goto fatal;
    if (readmsg(fd, request) <= 0) {
        int crashedclient = fd *(-1);
        if (write(fdpipe[1], &crashedclient, sizeof(int)) <= 0) goto fatal;
        destroymsg(request);
        return;
    }
    int rescode;
    switch (request->header->code) {
        case OPEN:
            rescode = w_openFile(request, fd);
            break;
        case READ:
            rescode = w_readFile(request, fd);
            break;
        case READN:
            rescode = w_readNFile(request, fd);
            break;
        case WRITE:
            rescode = w_writeFile(request, fd);
            break;
        case APPEND:
            rescode = w_appendToFile(request, fd);
            break;
        case LOCK:
            rescode = w_lockFile(request, fd);
            break;
        case UNLOCK:
            rescode = w_unlockFile(request, fd);
            break;
        case CLOSE:
            rescode = w_closeFile(request, fd);
            break;
        case REMOVE:
            rescode = w_removeFile(request, fd);
            break;
        case FIN:
            rescode = w_closeConnection(request, fd);
            break;
        default: {
            rescode = EBADRQC;
            msg_t *response = NULL;
            if ((response = buildmsg(request->header->username, rescode, request->header->code, request->header->pathname, 0, NULL)) == NULL)
                rescode = FIN;
            if (writemsg(fd, response) <= 0)
                rescode = FIN;
            if (response) destroymsg(response);
            break;
        }
    }

    if (rescode == ENOTRECOVERABLE) goto fatal;

    if (request->header->code == LOCK && rescode == EBUSY){
        //usiamo il campo arg per salvarci il fd da riconsiderare quando verrà unlockato il file
        request->header->arg = fd;
        if (client_waitlock(request) == ENOTRECOVERABLE) goto fatal;
        return;
    }

    if (request->header->code != FIN) {
        if(write(fdpipe[1], &fd, sizeof(int)) <= 0) goto fatal;
    } else {
        int closing_client = fd *(-1);
        if (write(fdpipe[1], &closing_client, sizeof(int)) <= 0) goto fatal;
    }

    if (request->header->code == UNLOCK && rescode == EXIT_SUCCESS){
        int client;
        if ((client = client_completelock(request)) == ENOTRECOVERABLE) goto fatal;
        if (client >= 0) { //trovata lock da completare
            if (write(fdpipe[1], &client, sizeof(int)) <= 0) goto fatal;
        }
    }

    destroymsg(request);
    return;

    fatal:
    PRINT_ERROR("fatal error")
    if (request) destroymsg(request);
    exit(EXIT_FAILURE);
}


int w_openFile(msg_t *request, int clientfd) {
    char OP[BUFSIZE] = "";
    msg_t *response = NULL;

    int rescode = fs_openFile(storage, request->header->pathname, request->header->arg, request->header->username);
    if ((response = buildmsg(request->header->username, rescode, request->header->arg, request->header->pathname, 0, NULL)) == NULL)
        goto error;
    if (writemsg(clientfd, response) <= 0)
        goto error;

    if(request->header->arg == (O_CREATE | O_LOCK)) strcpy(OP, "OPEN_CREATE_LOCK");
    else if(request->header->arg == O_LOCK) strcpy(OP, "OPEN_LOCK");
    else if(request->header->arg == O_CREATE) strcpy(OP, "OPEN_CREATE");
    else strcpy(OP, "OPEN");
    if (log_operation(OP, clientfd, 0, 0, 0, request->header->pathname, (rescode == 0 ? "OK" : "ERR")) == -1)
        goto fatal;

    destroymsg(response);
    return rescode;

    error:
    PRINT_PERROR("openFile")
    if (response) destroymsg(response);
    return FIN;
    fatal:
    PRINT_ERROR("fatal error")
    destroymsg(response);
    return ENOTRECOVERABLE;
}

int w_readFile(msg_t *request, int clientfd) {
    msg_t *response = NULL;
    void *file_content = NULL;
    size_t *file_size = NULL;
    file_size = malloc(sizeof(size_t));
    if (file_size == NULL) goto error;
    *file_size = 0;

    int rescode = fs_readFile(storage, request->header->pathname, request->header->username, &file_content, file_size);
    if ((response = buildmsg(request->header->username, rescode, request->header->arg, request->header->pathname,*file_size, file_content)) == NULL)
        goto error;
    if (writemsg(clientfd, response) <= 0)
        goto error;
    if (log_operation("READ", clientfd, 0, 0, *file_size, request->header->pathname, (rescode == 0 ? "OK" : "ERR")) == -1)
        goto fatal;

    destroymsg(response);
    if (*file_size > 0) free(file_content);
    free(file_size);
    return rescode;

    error:
    PRINT_PERROR("readFile")
    if (file_size) {
        if (*file_size > 0) free(file_content);
        free(file_size);
    }
    if (response) destroymsg(response);
    return FIN;
    fatal:
    PRINT_ERROR("fatal error")
    if (*file_size > 0) free(file_content);
    free(file_size);
    destroymsg(response);
    return ENOTRECOVERABLE;
}

int w_readNFile(msg_t *request, int clientfd) {

    msg_t *response = NULL;
    elem_t *node = NULL;
    file_t *file = NULL;
    list_t *files = NULL;
    if ((files = list_init()) == NULL) goto error;

    int rescode = fs_readNFiles(storage, request->header->username, request->header->arg, files);

    int files_read = files->length;
    if (files_read == 0) {
        if ((response = buildmsg(request->header->username, rescode, files_read, NULL, 0, NULL)) == NULL)
            goto error;
        if (writemsg(clientfd, response) <= 0)
            goto error;
        if (log_operation("READ_N", clientfd, 0, 0, 0, 0, "OK") == -1)
            goto fatal;
        destroymsg(response);
    }
    else while(files->length > 0) {
            node = list_removehead(files);
            file = node->data;
            if ((response = buildmsg(request->header->username, rescode, files_read, file->filename, file->size,file->content)) == NULL)
                goto error;
            if (writemsg(clientfd, response) <= 0)
                goto error;
            if (log_operation("READ_N", clientfd, 0, 0, file->size, 0, "OK") == -1)
                goto fatal;

            fs_filedestroy(file);
            free(node);
            destroymsg(response);
        }

    list_destroy(files, (void (*)(void *)) fs_filedestroy);
    return rescode;

    error:
    PRINT_PERROR("readNFile")
    if (files) list_destroy(files, (void (*)(void *)) fs_filedestroy);
    if (file) fs_filedestroy(file);
    if (node) free(node);
    if (response) destroymsg(response);
    return FIN;
    fatal:
    PRINT_ERROR("fatal error")
    if (file) fs_filedestroy(file);
    if (node) free(node);
    destroymsg(response);
    list_destroy(files, (void (*)(void *)) fs_filedestroy);
    return ENOTRECOVERABLE;
}

int w_writeFile(msg_t *request, int clientfd) {

    msg_t *response = NULL;
    elem_t *node = NULL;
    file_t *file = NULL;
    size_t totalbytes_ejected = 0;
    list_t *filesEjected = NULL;
    if ((filesEjected = list_init()) == NULL) goto error;

    int rescode = fs_writeFile(storage, request->header->pathname, request->header->data_size, request->data,request->header->username, filesEjected);

    int files_ejected = filesEjected->length;
    if (files_ejected == 0) {
        if ((response = buildmsg(request->header->username, rescode, files_ejected, request->header->pathname, 0,NULL)) == NULL)
            goto error;
        if (writemsg(clientfd, response) <= 0)
            goto error;
        destroymsg(response);

    } else while (filesEjected->length > 0) {
            node = list_removehead(filesEjected);
            file = node->data;
            if ((response = buildmsg(request->header->username, rescode, files_ejected, file->filename, file->size,file->content)) == NULL)
                goto error;
            if (writemsg(clientfd, response) <= 0)
                goto error;
            if (log_operation("VICTIM", clientfd, file->size, 0, file->size, file->filename, "OK") == -1)
                goto fatal;

            totalbytes_ejected += file->size;
            fs_filedestroy(file);
            free(node);
            destroymsg(response);
            node = NULL;
            file = NULL;
        }
    if (log_operation("WRITE", clientfd, totalbytes_ejected, request->header->data_size, totalbytes_ejected,request->header->pathname, (rescode == 0 ? "OK" : "ERR")) == -1)
        goto fatal;

    list_destroy(filesEjected, (void (*)(void *)) fs_filedestroy);
    return rescode;

    error:
    PRINT_PERROR("writeFile")
    if (filesEjected) list_destroy(filesEjected, (void (*)(void *)) fs_filedestroy);
    if (file) fs_filedestroy(file);
    if (node) free(node);
    if (response) destroymsg(response);
    return FIN;
    fatal:
    PRINT_ERROR("fatal error")
    if (file) fs_filedestroy(file);
    if (node) free(node);
    destroymsg(response);
    list_destroy(filesEjected, (void (*)(void *)) fs_filedestroy);
    return ENOTRECOVERABLE;
}

int w_appendToFile(msg_t *request, int clientfd) {

    msg_t *response = NULL;
    elem_t *node = NULL;
    file_t *file = NULL;
    size_t totalbytes_ejected = 0;
    list_t *filesEjected = NULL;
    if ((filesEjected = list_init()) == NULL) goto error;

    int rescode = fs_appendToFile(storage, request->header->pathname, request->header->data_size, request->data , request->header->username, filesEjected);

    int files_ejected = filesEjected->length;
    if (files_ejected == 0) {
        if ((response = buildmsg(request->header->username, rescode, files_ejected, request->header->pathname, 0,NULL)) == NULL)
            goto error;
        if (writemsg(clientfd, response) <= 0)
            goto error;
        destroymsg(response);
    }
    else while(filesEjected->length > 0) {
            node = list_removehead(filesEjected);
            file = node->data;
            if ((response = buildmsg(request->header->username, rescode, files_ejected, file->filename, file->size,file->content)) == NULL)
                goto error;
            if (writemsg(clientfd, response) <= 0)
                goto error;
            if (log_operation("VICTIM", clientfd, file->size, 0, file->size, file->filename, "OK") == -1)
                goto fatal;

            totalbytes_ejected += file->size;
            fs_filedestroy(file);
            free(node);
            destroymsg(response);
            node = NULL;
            file = NULL;
        }
    if (log_operation("WRITE_APPEND", clientfd, totalbytes_ejected, rescode == 0 ? request->header->data_size : 0,totalbytes_ejected, request->header->pathname, (rescode == 0 ? "OK" : "ERR")) == -1)
        goto fatal;

    list_destroy(filesEjected, (void (*)(void *)) fs_filedestroy);
    return rescode;

    error:
    PRINT_PERROR("appendToFile")
    if (filesEjected) list_destroy(filesEjected, (void (*)(void *)) fs_filedestroy);
    if (file) fs_filedestroy(file);
    if (node) free(node);
    if (response) destroymsg(response);
    return FIN;
    fatal:
    PRINT_ERROR("fatal error")
    if (file) fs_filedestroy(file);
    if (node) free(node);
    destroymsg(response);
    list_destroy(filesEjected, (void (*)(void *)) fs_filedestroy);
    return ENOTRECOVERABLE;
}

int w_lockFile(msg_t *request, int clientfd) {

    int rescode = fs_lockFile(storage, request->header->pathname, request->header->username);

    msg_t *response = NULL;
    if ((response = buildmsg(request->header->username, rescode, request->header->arg, request->header->pathname, 0,NULL)) == NULL)
        goto error;
    if (writemsg(clientfd, response) <= 0)
        goto error;
    if (log_operation("LOCK", clientfd, 0, 0, 0, request->header->pathname, (rescode == 0 ? "OK" : "ERR")) == -1)
        goto fatal;
    
    destroymsg(response);
    return rescode;

    error:
    PRINT_PERROR("lockFile")
    if (response) destroymsg(response);
    return FIN;
    fatal:
    PRINT_ERROR("fatal error")
    destroymsg(response);
    return ENOTRECOVERABLE;
}

int w_unlockFile(msg_t *request, int clientfd) {

    int rescode = fs_unlockFile(storage, request->header->pathname, request->header->username);

    msg_t *response = NULL;
    if ((response = buildmsg(request->header->username, rescode, request->header->arg, request->header->pathname, 0,NULL)) == NULL)
        goto error;
    if (writemsg(clientfd, response) <= 0)
        goto error;
    if (log_operation("UNLOCK", clientfd, 0, 0, 0, request->header->pathname, (rescode == 0 ? "OK" : "ERR")) == -1)
        goto fatal;

    destroymsg(response);
    return rescode;

    error:
    PRINT_PERROR("unlockFile")
    if (response) destroymsg(response);
    return FIN;
    fatal:
    PRINT_ERROR("fatal error")
    destroymsg(response);
    return ENOTRECOVERABLE;
}

int w_closeFile(msg_t *request, int clientfd) {

    int rescode = fs_closeFile(storage, request->header->pathname, request->header->username);

    msg_t *response = NULL;
    if ((response = buildmsg(request->header->username, rescode, request->header->arg, request->header->pathname, 0,NULL)) == NULL)
        goto error;
    if (writemsg(clientfd, response) <= 0)
        goto error;
    if (log_operation("CLOSE", clientfd, 0, 0, 0, request->header->pathname, (rescode == 0 ? "OK" : "ERR")) == -1)
        goto fatal;
    
    destroymsg(response);
    return rescode;

    error:
    PRINT_PERROR("closeFile")
    if (response) destroymsg(response);
    return FIN;
    fatal:
    PRINT_ERROR("fatal error")
    destroymsg(response);
    return ENOTRECOVERABLE;
}

int w_removeFile(msg_t *request, int clientfd) {
    size_t *deleted_count = NULL;
    msg_t *response = NULL;
    deleted_count = malloc(sizeof(size_t));
    if (deleted_count == NULL) goto error;
    
    *deleted_count = 0;
    int rescode = fs_removeFile(storage, request->header->pathname, request->header->username, deleted_count);

    if ((response = buildmsg(request->header->username, rescode, request->header->arg, request->header->pathname, 0,NULL)) == NULL)
        goto error;
    if (writemsg(clientfd, response) <= 0)
        goto error;
    if (log_operation("REMOVE", clientfd, *deleted_count, 0, 0, request->header->pathname,(rescode == 0 ? "OK" : "ERR")) == -1)
        goto fatal;
    
    destroymsg(response);
    free(deleted_count);
    return rescode;

    error:
    PRINT_PERROR("removeFile")
    if (response) destroymsg(response);
    if (deleted_count) free(deleted_count);
    return FIN;
    fatal:
    PRINT_ERROR("fatal error")
    free(deleted_count);
    destroymsg(response);
    return ENOTRECOVERABLE;
}

int w_closeConnection(msg_t *request, int clientfd) {

    msg_t *response = NULL;
    if ((response = buildmsg(request->header->username, EXIT_SUCCESS, request->header->arg, request->header->pathname,0, NULL)) == NULL)
        goto error;
    if (writemsg(clientfd, response) <= 0)
        goto error;
    
    destroymsg(response);
    return EXIT_SUCCESS;

    error:
    PRINT_PERROR("closeConnection")
    if (response) destroymsg(response);
    return FIN;
}


//funzioni di supporto alla lockFile
int client_waitlock(msg_t *lock_request) {
    if (lock_request == NULL)
        return ENOTRECOVERABLE;

    if (pthread_rwlock_wrlock(storage->mutex) != 0) return ENOTRECOVERABLE;
    if (list_add(storage->clients_awaiting, lock_request) == NULL) return ENOTRECOVERABLE;
    if (pthread_rwlock_unlock(storage->mutex) != 0) return ENOTRECOVERABLE;
    return EXIT_SUCCESS;
}

int compare_msg_path(void *m1, void *m2) {
    msg_t *msg1 = (msg_t*)m1;
    msg_t *msg2 = (msg_t*)m2;
    return strcmp(msg1->header->pathname, msg2->header->pathname);
}

int client_completelock(msg_t *unlock_request) {
    if (unlock_request == NULL)
        return ENOTRECOVERABLE;

    int clientfd = -1;
    msg_t *toComplete = NULL;
    elem_t *elem = NULL;
    if (pthread_rwlock_wrlock(storage->mutex) != 0) return ENOTRECOVERABLE;
    if ((elem = list_remove(storage->clients_awaiting, unlock_request, compare_msg_path)) != NULL) {
        toComplete = elem->data;
        clientfd = toComplete->header->arg;
        destroymsg(toComplete);
        free(elem);
    }
    if (pthread_rwlock_unlock(storage->mutex) != 0) return ENOTRECOVERABLE;

    return clientfd;
}
