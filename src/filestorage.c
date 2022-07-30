
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <filestorage.h>
#include <protocol.h>

bool Verbose = false;
bool already_connected = false;
char *username;
int socketfd = -1;
char *socketname;

int openConnection(const char *sockname, int msec, const struct timespec abstime) {
#if DEBUG
    printf("verbose in filestorage: %d\n", Verbose);
#endif

    char errdesc[STRERROR_LEN] = "";

    if (sockname == NULL) {
        strcpy(errdesc, "with argument sockname");
        errno = EINVAL;
        goto error;
    }
    if (msec < 0) {
        strcpy(errdesc, "with argument msec");
        errno = EINVAL;
        goto error;
    }
    if (already_connected) {
        errno = EISCONN;
        goto error;
    }
    if (strlen(sockname) >= UNIX_PATH_MAX) {
        strcpy(errdesc, "with argument sockname");
        errno = ENAMETOOLONG;
        goto error;
    }

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    strncpy(sa.sun_path, sockname, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    if ((socketfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1){
        strcpy(errdesc, "creating socket");
        goto error;
    }

    time_t now;
    time(&now);
    int connres;
    verbose("< %s: Connecting to server ...\n", username);
    while ((connres = connect(socketfd, (struct sockaddr *) &sa, sizeof(sa)) == -1)
           && now < abstime.tv_sec) {
        verbose("< %s: Unable to connect to server. Retry in %d msec\n", username, msec);

        msleep(msec);
        time(&now);
    }
    if (connres != 0) {
        strcpy(errdesc, "connecting to server\n");
        errno = ETIMEDOUT;
        goto error;
    }
    already_connected = true;
    if ((socketname = strndup(sockname, strlen(sockname))) == NULL){
        strcpy(errdesc, "duplicating socketname");
        goto error;
    }

    verbose("< %s: %s (%s) completed: connection with server established\n", username, __func__, sockname);
    return 0;

    error:
    verbose("< %s: %s (%s) failed: there was an error %s: %s\n", username, __func__, sockname, errdesc, strerror(errno));
    return -1;
}

int closeConnection(const char *sockname) {

    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;

    if (sockname == NULL || strcmp(sockname, socketname) != 0) {
        strcpy(errdesc, "with argument sockname");
        errno = EINVAL;
        goto error;
    }
    if (!already_connected) {
        errno = ENOTCONN;
        goto error;
    }
    if (strlen(sockname) >= UNIX_PATH_MAX) {
        strcpy(errdesc, "with argument sockname");
        errno = ENAMETOOLONG;
        goto error;
    }

    verbose("< %s: Closing connection ...\n", username);
    if (errno != ECONNRESET) {
        if ((request = buildmsg(username, FIN, -1, NULL, 0, NULL)) == NULL) {
            strcpy(errdesc, "building the message to be send");
            goto error;
        }
        if (writemsg(socketfd, request) <= 0) {
            strcpy(errdesc, "writing the request to server");
            goto error;
        }

        if ((response = initmsg()) == NULL) {
            strcpy(errdesc, "initialising the response to be received");
            goto error;
        }
        if (readmsg(socketfd, response) < 0) {
            strcpy(errdesc, "reading the response from server");
            goto error;
        }
        if (response->header->code != EXIT_SUCCESS) {
            errno = response->header->code;
            goto error;
        }
    }
    if (close(socketfd) == -1) {
        strcpy(errdesc, "closing socket");
        goto error;
    }
    already_connected = false;

    verbose("< %s: %s (%s) completed: connection closed successfully\n", username, __func__, sockname);
    if (request) destroymsg(request);
    if (response) destroymsg(response);
    return 0;

    error:
    verbose("< %s: %s (%s) failed: there was an error %s: %s\n", username, __func__, sockname, errdesc, strerror(errno));
    if (request) destroymsg(request);
    if (response) destroymsg(response);
    return -1;
}

int openFile(const char *pathname, int flags) {

    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;

    if (already_connected == false) {
        errno = ENOTCONN;
        goto error;
    }
    if (!pathname) {
        strcpy(errdesc, "with argument pathname");
        errno = EINVAL;
        goto error;
    }
    if (strlen(pathname) > MAX_PATH) {
        strcpy(errdesc, "with argument pathname");
        errno = ENAMETOOLONG;
        goto error;
    }
    if (flags < 0 || flags > 3) {
        strcpy(errdesc, "with argument flags");
        errno = EINVAL;
        goto error;
    }

#if DEBUG
    printf("pathname: %s\n", pathname);
#endif

    if ((request = buildmsg(username, OPEN, flags, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
#if DEBUG
    printf("username: %s\n"
           "code: %d\n"
           "pathname: %s\n",
           request->header->username, request->header->code, request->header->pathname);
#endif
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }
#if DEBUG
    printf("messaggio inviato\n");
#endif


    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }
    if (response->header->code != EXIT_SUCCESS) {
        errno = response->header->code;
        goto error;
    }

    verbose("< %s: %s (%s) completed\n", username, __func__, pathname);
    destroymsg(request);
    destroymsg(response);
    return 0;

    error:
    verbose("< %s: %s (%s) failed: there was an error %s: %s\n", username, __func__, pathname, errdesc, strerror(errno));
    if (request) destroymsg(request);
    if (response) destroymsg(response);
    return -1;
}

int readFile(const char *pathname, void **buf, size_t *size) {

    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;
    *buf = NULL;

    if (already_connected == false) {
        errno = ENOTCONN;
        goto error;
    }
    if (!pathname) {
        strcpy(errdesc, "with argument pathname");
        errno = EINVAL;
        goto error;
    }
    if (strlen(pathname) > MAX_PATH) {
        strcpy(errdesc, "with argument pathname");
        errno = ENAMETOOLONG;
        goto error;
    }


    if ((request = buildmsg(username, READ, -1, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }

    if (response->header->code != EXIT_SUCCESS) {
        errno = response->header->code;
        goto error;
    }

    *buf = malloc(response->header->data_size);
    if (*buf == NULL) {
        strcpy(errdesc, "allocating the buffer");
        goto error;
    }
    memcpy(*buf, response->data, response->header->data_size);
    *size = response->header->data_size;

    verbose("< %s: %s (%s) completed: read %d bytes\n", username, __func__, pathname, response->header->data_size);
    destroymsg(request);
    destroymsg(response);
    return 0;

    error:
    verbose("< %s: %s (%s) failed: there was an error %s: %s\n", username, __func__, pathname, errdesc, strerror(errno));
    if (*buf) free(*buf);
    if (request) destroymsg(request);
    if (response) destroymsg(response);
    return -1;
}

int readNFiles(int N, const char *dirname) {

    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;
    size_t bytes_stored = 0;
    int files_stored = 0;
    int files_recv = 0;

    if (already_connected == false) {
        errno = ENOTCONN;
        goto error;
    }
    if (dirname && strlen(dirname) > MAX_PATH) {
        strcpy(errdesc, "with argument dirname");
        errno = ENAMETOOLONG;
        goto error;
    }

#if DEBUG
    printf("buildo request\n");
#endif
    if (N <= 0) N = 0;
    if ((request = buildmsg(username, READN, N, NULL, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    do {
        if (response) destroymsg(response);
        if ((response = initmsg()) == NULL) {
            strcpy(errdesc, "initialising the response to be received");
            goto error;
        }
        if (readmsg(socketfd, response) <= 0) {
            strcpy(errdesc, "reading the response from server");
            goto error;
        }
        if (response->header->code == ENODATA) {
            errno = response->header->code;
            break;
        }
        if (response->header->code != EXIT_SUCCESS) {
            errno = response->header->code;
            goto error;
        }
        if (response->header->arg == 0) break;

        if (dirname) {
            if (storeFile(dirname, response->header->pathname, response->data, response->header->data_size) == -1) {
                files_recv++;
                verbose("< %s: %s (%s) : there was an error storing the file received: %s. File %s corrupted\n", username, __func__, N,
                        strerror(errno), response->header->pathname);
                continue;
            }
        }
        else {
            char *toPrint = malloc((response->header->data_size + 1) * sizeof(char));
            if (toPrint == NULL){
                files_recv++;
                verbose("< %s: %s (%s) : there was an error printing the file received: %s. File %s skipped\n", username, __func__, N,
                        strerror(errno), response->header->pathname);
                continue;
            }
            memcpy(toPrint, response->data, response->header->data_size);
            toPrint[response->header->data_size] = 0;
            printf("< %s: %s (%s):\n%s\n", username, __func__, response->header->pathname, toPrint);
            free(toPrint);
        }

        files_recv++;
        if (dirname) files_stored++;
        if (dirname) bytes_stored += response->header->data_size;

    } while (files_recv < response->header->arg);

    files_recv ?
        (dirname ?
             verbose("< %s: %s (%d) completed: received %d files, stored %d in %s, occupying a total of %ld bytes\n", username,
                     __func__, N,files_recv, files_stored, dirname, bytes_stored)
           : verbose("< %s: %s (%d) completed: received %d files\n", username,
                     __func__, N,files_recv))
    : verbose("< %s: %s (%d) completed: read %d files: %s\n", username,__func__, N, files_stored, strerror(errno));
    destroymsg(request);
    destroymsg(response);
    return dirname ? files_stored : files_recv;

    error:
    files_recv ?
        (dirname ?
            verbose("< %s: %s (%s) failed: there was an error %s: %s. Received %d files, stored %d in %s, occupying a total of %ld bytes\n", username,
                    __func__, N, errdesc, strerror(errno), files_recv, files_stored, dirname, bytes_stored)
          : verbose("< %s: %s (%s) failed: there was an error %s: %s. Received %d files\n", username,
                    __func__, N, errdesc, strerror(errno), files_recv))
    : verbose("< %s: %s (%s) failed: there was an error %s: %s. No files read\n", username, __func__, N, errdesc, strerror(errno));
    if (request) destroymsg(request);
    if (response) destroymsg(response);
    return -1;
}

int writeFile(const char *pathname, const char *dirname) {
    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;
    void *file_content = NULL;
    int files_recv = 0;
    int files_stored = 0;
    size_t bytes_stored = 0;

    if (already_connected == false) {
        errno = ENOTCONN;
        goto error;
    }
    if (!pathname) {
        strcpy(errdesc, "with argument pathname");
        errno = EINVAL;
        goto error;
    }
    if (strlen(pathname) > MAX_PATH) {
        strcpy(errdesc, "with argument patname");
        errno = ENAMETOOLONG;
        goto error;
    }
    if (dirname && strlen(dirname) > MAX_PATH) {
        strcpy(errdesc, "with argument dirname");
        errno = ENAMETOOLONG;
        goto error;
    }

    //lettura file da scrivere
    FILE *file_stream;
    if ((file_stream = fopen(pathname, "rb")) == NULL) {
        strcpy(errdesc, "opening the file to be send");
        goto error;
    }
    //Leggo il contenuto del file_stream lato client
    struct stat sb;
    if (stat(pathname, &sb) == -1) {
        strcpy(errdesc, "gathering file attributes");
        goto error;
    }
    off_t file_size = sb.st_size;
    if (file_size == 0) {
        if (fclose(file_stream) != 0) {
            strcpy(errdesc, "closing the file");
            goto error;
        }
        errno = ENODATA;
        strcpy(errdesc, "reading the file to be send");
        goto error;
    }

    file_content = malloc(file_size);
    if (file_content == NULL) {
        strcpy(errdesc, "allocating memory for file content");
        goto error;
    }
    while (!feof(file_stream)) {
        fread(file_content, 1, file_size, file_stream);
        if (ferror(file_stream)) {
            errno = ENOTRECOVERABLE;
            strcpy(errdesc, "reading file content");
            goto error;
        }
    }
    //finito di leggere il file e chiudo lo stream
    if (fclose(file_stream) != 0) {
        strcpy(errdesc, "closing the file");
        goto error;
    }

    if ((request = buildmsg(username, WRITE, -1, pathname, file_size, file_content)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    do {
        if (response) destroymsg(response);
        if ((response = initmsg()) == NULL) {
            strcpy(errdesc, "initialising the response to be received");
            goto error;
        }
        if (readmsg(socketfd, response) <= 0) {
            strcpy(errdesc, "reading the response from server");
            goto error;
        }
        if (response->header->code != EXIT_SUCCESS) {
            errno = response->header->code;
            goto error;
        }
        if (response->header->arg == 0) break;

        if (dirname) {
            if (storeFile(dirname, response->header->pathname, response->data, response->header->data_size) == -1) {
                files_recv++;
                verbose("< %s: %s (%s) : there was an error storing the ejected file received: %s. File %s corrupted\n", username, __func__, request->header->pathname,
                        strerror(errno), response->header->pathname);
                continue;
            }
        } else {
            char *toPrint = malloc((response->header->data_size + 1) * sizeof(char));
            if (toPrint == NULL) {
                files_recv++;
                verbose("< %s: %s (%s) : there was an error printing the file received: %s. File %s skipped\n", username, __func__,
                        request->header->pathname, strerror(errno), response->header->pathname);
                continue;
            }
            memcpy(toPrint, response->data, response->header->data_size);
            toPrint[response->header->data_size] = 0;
            printf("< %s: %s (%s):\n%s\n", username, __func__, response->header->pathname, toPrint);
        }

        files_recv++;
        if (dirname) files_stored++;
        if (dirname) bytes_stored += response->header->data_size;
    } while (files_recv < response->header->arg);

    files_recv ?
        (dirname ?
            verbose("< %s: %s (%s) completed: written %d bytes, received %d ejected files, stored %d in %s, occupying a total of %d bytes\n", username,
                    __func__, pathname, file_size, files_recv, files_stored, dirname, bytes_stored)
          : verbose("< %s: %s (%s) completed: written %d bytes, received %d ejected files\n", username,
                    __func__, pathname, file_size, files_recv))
    : verbose("< %s: %s (%s) completed: written %d bytes. No ejected files received\n", username,
              __func__, pathname, file_size);
    destroymsg(request);
    destroymsg(response);
    free(file_content);
    return 0;

    error:
    files_recv ?
        (dirname ?
            verbose("< %s: %s (%s) failed: there was an error %s: %s. Received %d ejected files, stored %d in %s, occupying a total of %d bytes\n", username,
                    __func__, pathname, errdesc, strerror(errno), files_recv, files_stored, dirname, bytes_stored)
          : verbose("< %s: %s (%s) failed: there was an error %s: %s. Received %d ejected files\n", username,
                    __func__, pathname, errdesc, strerror(errno), files_recv))
    : verbose("< %s: %s (%s) failed: there was an error %s: %s. No ejected files received\n", username,
              __func__, pathname, errdesc,strerror(errno));
    if (request) destroymsg(request);
    if (response) destroymsg(response);
    if (file_content) free(file_content);
    return -1;
}

int appendToFile(const char *pathname, void *buf, size_t size, const char *dirname) {

    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;
    int files_stored = 0;
    int files_recv = 0;
    size_t bytes_stored = 0;

    if (already_connected == false) {
        errno = ENOTCONN;
        goto error;
    }
    if (!pathname) {
        strcpy(errdesc, "with argument pathname");
        errno = EINVAL;
        goto error;
    }
    if (!buf) {
        strcpy(errdesc, "with argument buf");
        errno = EINVAL;
        goto error;
    }
    if (size <= 0) {
        strcpy(errdesc, "with argument size");
        errno = EINVAL;
        goto error;
    }
    if (strlen(pathname) > MAX_PATH) {
        strcpy(errdesc, "with argument pathname");
        errno = ENAMETOOLONG;
        goto error;
    }
    if (dirname && strlen(dirname) > MAX_PATH) {
        strcpy(errdesc, "with argument dirname");
        errno = ENAMETOOLONG;
        goto error;
    }


    if ((request = buildmsg(username, APPEND, -1, pathname, size, buf)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    do {
        if (response) destroymsg(response);
        if ((response = initmsg()) == NULL) {
            strcpy(errdesc, "initialising the response to be received");
            goto error;
        }
        if (readmsg(socketfd, response) <= 0) {
            strcpy(errdesc, "reading the response from server");
            goto error;
        }
        if (response->header->code != EXIT_SUCCESS) {
            errno = response->header->code;
            goto error;
        }
        if (response->header->arg == 0) break;

        if (dirname) {
            if (storeFile(dirname, response->header->pathname, response->data, response->header->data_size) == -1) {
                files_recv++;
                verbose("< %s: %s (%s) : there was an error storing the ejected file received: %s. File %s corrupted\n", username,
                        __func__, request->header->pathname,
                        strerror(errno), response->header->pathname);
                continue;
            }
        } else {
            char *toPrint = malloc((response->header->data_size + 1) * sizeof(char));
            if (toPrint == NULL){
                files_recv++;
                verbose("< %s: %s (%s) : there was an error printing the file received: %s. File %s skipped\n", username, __func__,
                        request->header->pathname, strerror(errno), response->header->pathname);
                continue;
            }
            memcpy(toPrint, response->data, response->header->data_size);
            toPrint[response->header->data_size] = 0;
            printf("< %s: %s (%s):\n%s\n", username, __func__, response->header->pathname, toPrint);
        }

        files_recv++;
        if (dirname) files_stored++;
        if (dirname) bytes_stored += response->header->data_size;

    } while (files_recv < response->header->arg);


    files_recv ?
        (dirname ?
            verbose("< %s: %s (%s) completed: appended %d bytes, received %d ejected files, stored %d in %s, occupying a total of %d bytes\n", username,
                    __func__, pathname, size, files_recv, files_stored, dirname, bytes_stored)
          : verbose("< %s: %s (%s) completed: appended %d bytes, received %d ejected files\n", username,
                    __func__, pathname, size, files_recv))
    : verbose("< %s: %s (%s) completed: appended %d bytes. No ejected files received\n", username,
              __func__, pathname, size);
    destroymsg(response);
    destroymsg(request);
    return 0;

    error:
    files_recv ?
        (dirname ?
            verbose("< %s: %s (%s) failed: there was an error %s: %s. Received %d ejected files, stored %d in %s, occupying a total of %d bytes\n", username,
                    __func__, pathname, errdesc, strerror(errno), files_recv, files_stored, dirname, bytes_stored)
          : verbose("< %s: %s (%s) failed: there was an error %s: %s. Received %d ejected files\n", username,
                    __func__, pathname, errdesc, strerror(errno), files_recv))
    : verbose("< %s: %s (%s) failed: there was an error %s: %s. No ejected files received\n", username,
              __func__, pathname,errdesc, strerror(errno));
    if (request) destroymsg(request);
    if (response) destroymsg(response);
    return -1;
}

int lockFile(const char *pathname) {

    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;

    if (already_connected == false) {
        errno = ENOTCONN;
        goto error;
    }
    if (!pathname) {
        strcpy(errdesc, "with argument pathname");
        errno = EINVAL;
        goto error;
    }
    if (strlen(pathname) > MAX_PATH) {
        strcpy(errdesc, "with argument pathname");
        errno = ENAMETOOLONG;
        goto error;
    }

    int rescode;
    do {
        if ((request = buildmsg(username, LOCK, -1, pathname, 0, NULL)) == NULL) {
            strcpy(errdesc, "building the message to be send");
            goto error;
        }
        if (writemsg(socketfd, request) <= 0) {
            strcpy(errdesc, "writing the request to server");
            goto error;
        }

        if ((response = initmsg()) == NULL) {
            strcpy(errdesc, "initialising the response to be received");
            goto error;
        }
        if (readmsg(socketfd, response) <= 0) {
            strcpy(errdesc, "reading the response from server");
            goto error;
        }
        rescode = response->header->code;
        destroymsg(response);
        response = NULL;
        if (rescode != EXIT_SUCCESS && rescode != EBUSY){
            errno = rescode;
            goto error;
        }
        if (rescode == EBUSY)
            verbose("< %s: %s (%s) : Unable to lock the file at the moment, another user has exclusive access. Waiting ...\n", username, __func__, pathname);

    } while (rescode != EXIT_SUCCESS);

    verbose("< %s: %s (%s) completed\n", username, __func__, pathname);
    destroymsg(request);
    return 0;

    error:
    verbose("< %s: %s (%s) failed: there was an error %s: %s\n", username, __func__, pathname, errdesc, strerror(errno));
    if (request) destroymsg(request);
    if (response) destroymsg(response);
    return -1;
}

int unlockFile(const char *pathname) {

    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;

    if (already_connected == false) {
        errno = ENOTCONN;
        goto error;
    }
    if (!pathname) {
        strcpy(errdesc, "with argument pathname");
        errno = EINVAL;
        goto error;
    }
    if (strlen(pathname) > MAX_PATH) {
        strcpy(errdesc, "with argument pathname");
        errno = ENAMETOOLONG;
        goto error;
    }

    if ((request = buildmsg(username, UNLOCK, -1, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }
    if (response->header->code != EXIT_SUCCESS) {
        errno = response->header->code;
        goto error;
    }

    verbose("< %s: %s (%s) completed\n", username, __func__, pathname);
    destroymsg(request);
    destroymsg(response);
    return 0;

    error:
    verbose("< %s: %s (%s) failed: there was an error %s: %s\n", username, __func__, pathname, errdesc, strerror(errno));
    if (request) destroymsg(request);
    if (response) destroymsg(response);

    return -1;
}

int closeFile(const char *pathname) {

    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;

    if (already_connected == false) {
        errno = ENOTCONN;
        goto error;
    }
    if (!pathname) {
        strcpy(errdesc, "with argument pathname");
        errno = EINVAL;
        goto error;
    }
    if (strlen(pathname) > MAX_PATH) {
        strcpy(errdesc, "with argument pathname");
        errno = ENAMETOOLONG;
        goto error;
    }

    if ((request = buildmsg(username, CLOSE, -1, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }
    if (response->header->code != EXIT_SUCCESS) {
        errno = response->header->code;
        goto error;
    }

    verbose("< %s: %s (%s) completed\n", username, __func__, pathname);
    destroymsg(request);
    destroymsg(response);
    return 0;

    error:
    verbose("< %s: %s (%s) failed: there was an error %s: %s\n", username, __func__, pathname, errdesc, strerror(errno));
    if (request) destroymsg(request);
    if (response) destroymsg(response);

    return -1;
}

int removeFile(const char *pathname) {

    char errdesc[STRERROR_LEN] = "";
    msg_t *request = NULL;
    msg_t *response = NULL;

    if (already_connected == false) {
        errno = ENOTCONN;
        goto error;
    }
    if (!pathname) {
        strcpy(errdesc, "with argument pathname");
        errno = EINVAL;
        goto error;
    }
    if (strlen(pathname) > MAX_PATH) {
        strcpy(errdesc, "with argument pathname");
        errno = ENAMETOOLONG;
        goto error;
    }

    if ((request = buildmsg(username, REMOVE, -1, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }
    if (response->header->code != EXIT_SUCCESS) {
        errno = response->header->code;
        goto error;
    }

    verbose("< %s: %s (%s) completed\n", username, __func__, pathname);
    destroymsg(request);
    destroymsg(response);
    return 0;

    error:
    verbose("< %s: %s (%s) failed: there was an error %s: %s\n", username, __func__, pathname, errdesc, strerror(errno));
    if (request) destroymsg(request);
    if (response) destroymsg(response);

    return -1;
}

int storeFile(const char *dirname, char *filename, void *data, size_t data_size) {

    char *storepath = NULL;
    FILE *ptr = NULL;

    if ((storepath = strnconcat(dirname, filename, NULL)) == NULL) return -1;
    if (mkdirs(storepath) == -1) return -1;
    if ((ptr = fopen(storepath, "w+")) == NULL) return -1;
    if (fwrite(data, 1, data_size, ptr) != data_size) {
        errno = ENOTRECOVERABLE;
        return -1;
    }
    if (fclose(ptr) != 0) return -1;

    free(storepath);
    return 0;
}

int verbose(const char *restrict format, ...) {
    if (!Verbose)
        return 0;

    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);

    return ret;
}