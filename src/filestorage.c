
#include "util.h"
#include <time.h>
#include "conn.h"
#include "filestorage.h"
#include "protocol.h"

//TODO inserire tutte le stampe della verbose mode

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
    SYSCALL_EXIT(socket, socketfd, socket(AF_UNIX, SOCK_STREAM, 0), "client socket", "")

    time_t now;
    time(&now);
    int connres = 0;
    verbose("< Connecting to server...\n");
    while ((connres = connect(socketfd, (struct sockaddr *) &sa, sizeof(sa)) == -1)
           && now < abstime.tv_sec) {
        verbose("< Unable to connect to server. Retry in %d msec\n", msec);

        msleep(msec);
        time(&now);
    }
    if (connres != 0) {
        strcpy(errdesc,"connecting to server\n");
        errno = ETIMEDOUT;
        goto error;
    }
    already_connected = true;
    socketname = strndup(sockname, strlen(sockname));

    verbose("< %s (%s) completed with success: connection with server established\n", __func__, sockname);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, sockname, errdesc, strerror(errno));
    return -1;
}

int closeConnection(const char *sockname) {

    char errdesc[STRERROR_LEN] = "";

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

    //TODO send to server close request
    __attribute__((unused)) int unused;
    SYSCALL_EXIT(close, unused, close(socketfd), "close", "")

    verbose("< %s (%s) completed with success: connection closed successfully\n", __func__, sockname);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, sockname, errdesc, strerror(errno));
    return -1;
}

int openFile(const char *pathname, int flags) {

    char errdesc[STRERROR_LEN] = "";

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

    msg_t *request;
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


    msg_t *response;
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

    verbose("< %s (%s) completed with success\n", __func__, pathname);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, pathname, errdesc, strerror(errno));
    return -1;
}

int readFile(const char *pathname, void **buf, size_t *size) {

    char errdesc[STRERROR_LEN] = "";

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

    msg_t *request;
    if ((request = buildmsg(username, READ, -1, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    msg_t *response;
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
    memcpy(*buf, response->data, response->header->data_size);
    *size = response->header->data_size;

    destroymsg(request);
    destroymsg(response);
    verbose("< %s (%s) completed with success: read %d bytes\n", __func__, pathname, response->header->data_size);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, pathname, errdesc, strerror(errno));
    return -1;

    /*
    char *opstr = itostr(READ);
    char *packet = strnconcat(opstr, "|", pathname, NULL);
    printf("%s\n", packet);
    int packetsize = (int) strlen(packet);

    CHECK_EQ_EXIT(writen(socketfd, &packetsize, sizeof(int)), -1, "writen")
    CHECK_EQ_EXIT(writen(socketfd, packet, packetsize), -1, "writen")
     */
}

int readNFiles(int N, const char *dirname) {

    char errdesc[STRERROR_LEN] = "";

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
    msg_t *request;
    if ((request = buildmsg(username, READN, N, NULL, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    msg_t *response;
    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }


    if (response->header->arg <= 0) {
        destroymsg(request);
        destroymsg(response);
        //TODO
        return 0;;
    }

    int bytesread = 0;
    int filesread = 0;
    while (true) {
        if (dirname) {
            char abs_storedir[MAX_PATH];
            CHECK_EQ_EXIT(realpath(dirname, abs_storedir), NULL, "realpath")

            //estraggo dal path il nome del file da salvare in storedir/
            char *tmpfilename;
            char *path = strndup(response->header->pathname, strlen(response->header->pathname));
            char *tok;
            while ((tok = strtok_r(NULL, "/", &path)) != NULL) {
                tmpfilename = tok;
            }
#if DEBUG
            printf("il nome del file è %s\n", tmpfilename);
#endif
            strncat(abs_storedir, "/", 2);
            strncat(abs_storedir, tmpfilename, strlen(tmpfilename));
            FILE *ptr = NULL;
            CHECK_EQ_EXIT((ptr = fopen(abs_storedir, "w+")), NULL, "fopen")
            CHECK_EQ_EXIT(fwrite(response->data, response->header->data_size, 1, ptr), -1, "fwrite")
            CHECK_EQ_EXIT(fclose(ptr), EOF, "fclose")

        } else printf("file letto (READN): %s\n", (char *) response->data);

        filesread++;
        if (filesread == response->header->arg) {
            destroymsg(response);
            break;
        }

        destroymsg(response);
        if ((response = initmsg()) == NULL) {
            strcpy(errdesc, "initialising the response to be received");
            goto error;
        }
        if (readmsg(socketfd, response) <= 0) {
            strcpy(errdesc, "reading the response from server");
            goto error;
        }
    }

    destroymsg(request);
    verbose("< %s (%d) completed with success: filesread %d files, totaling %d bytes, stored in %s\n", __func__, N, response->header->arg, bytesread, dirname);
    return filesread;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, N, errdesc, strerror(errno));
}

int writeFile(const char *pathname, const char *dirname) {
    char errdesc[STRERROR_LEN] = "";

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
    if (!(file_stream = fopen(pathname, "rb")))
        goto error;

    //Leggo il contenuto del file_stream lato client
    struct stat sb;
    if (stat(pathname, &sb) == -1)
        goto error;
    off_t file_size = sb.st_size;
    if (file_size == 0) {
        errno = ENODATA;
        fclose(file_stream);
        goto error;
    }

    void *file_content = malloc(file_size);
    if (!file_content) goto error;
    while (!feof(file_stream))
        fread(file_content, 1, file_size, file_stream);
    //finito di leggere il file e chiudo lo stream
    fclose(file_stream);

    msg_t *request;
    if ((request = buildmsg(username, WRITE, -1, pathname, file_size, file_content)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    msg_t *response;
    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    int totalrecv = 0;
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }

    verbose("< %s (%s) completed with success: written %d bytes, received %d ejected files, each stored in %s, occupying a total of %d bytes\n", __func__, pathname, response->header->arg, dirname, totalrecv);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, pathname, errdesc, strerror(errno));
    return -1;
}

//TODO size_t in tutte le funzioni
int appendToFile(const char *pathname, void *buf, size_t size, const char *dirname) {

    char errdesc[STRERROR_LEN] = "";

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


    msg_t *request;
    if ((request = buildmsg(username, APPEND, -1, pathname, size, buf)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    msg_t *response;
    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    int totalrecv = 0;
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }

    verbose("< %s (%s) completed with success: appended %d bytes, received %d ejected files, each stored in %s, occupying a total of %d bytes\n", __func__, pathname, response->header->arg, dirname, totalrecv);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, pathname, errdesc, strerror(errno));
    return -1;
}

int lockFile(const char *pathname) {

    char errdesc[STRERROR_LEN] = "";

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


    msg_t *request;
    if ((request = buildmsg(username, LOCK, -1, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    msg_t *response;
    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }

    verbose("< %s (%s) completed with success\n", __func__, pathname);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, pathname, errdesc, strerror(errno));
    return -1;
}

int unlockFile(const char *pathname) {

    char errdesc[STRERROR_LEN] = "";

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

    msg_t *request;
    if ((request = buildmsg(username, UNLOCK, -1, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    msg_t *response;
    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }

    verbose("< %s (%s) completed with success\n", __func__, pathname);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, pathname, errdesc, strerror(errno));
    return -1;
}

int closeFile(const char *pathname) {

    char errdesc[STRERROR_LEN] = "";

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


    msg_t *request;
    if ((request = buildmsg(username, CLOSE, -1, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    msg_t *response;
    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }

    verbose("< %s (%s) completed with success\n", __func__, pathname);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, pathname, errdesc, strerror(errno));
    return -1;
}

int removeFile(const char *pathname) {

    char errdesc[STRERROR_LEN] = "";

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

    msg_t *request;
    if ((request = buildmsg(username, REMOVE, -1, pathname, 0, NULL)) == NULL) {
        strcpy(errdesc, "building the message to be send");
        goto error;
    }
    if (writemsg(socketfd, request) <= 0) {
        strcpy(errdesc, "writing the request to server");
        goto error;
    }

    msg_t *response;
    if ((response = initmsg()) == NULL) {
        strcpy(errdesc, "initialising the response to be received");
        goto error;
    }
    if (readmsg(socketfd, response) <= 0) {
        strcpy(errdesc, "reading the response from server");
        goto error;
    }

    verbose("< %s (%s) completed with success\n", __func__, pathname);
    return 0;

    error:
    verbose("< %s (%s) failed: there was an error %s: %s\n", __func__, pathname, errdesc, strerror(errno));
    return -1;
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