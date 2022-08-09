#ifndef FILE_STORAGE_SERVER_PROTOCOL_H
#define FILE_STORAGE_SERVER_PROTOCOL_H

#include <conn.h>
#include <util.h>

typedef struct header {
    char pathname[MAX_PATH];
    char username[MAX_USERNAME];
    size_t data_size;
    int code;
    int arg;
} msg_header;

typedef struct message {
    msg_header *header;
    void *data;
} msg_t;

typedef enum request_code {
    OPEN,
    READ,
    READN,
    WRITE,
    APPEND,
    LOCK,
    UNLOCK,
    CLOSE,
    REMOVE,
    FIN
} request_c;

typedef enum open_flag {
    O_NORMAL,
    O_CREATE,
    O_LOCK
} flag ;

static inline int writemsg(int to, msg_t* message) {
    if (to < 0 || !message) {
        errno = EINVAL;
        return -1;
    }

    int wres;
    if ((wres = writen(to, message->header, sizeof(msg_header))) == -1) return -1;
    if (message->header->data_size > 0) {
        if ((wres = writen(to, message->data, message->header->data_size)) == -1) return -1;
    }

    return wres;
}

static inline int readmsg(int from, msg_t *message) {
    if (from < 0 || !message) {
        errno = EINVAL;
        return -1;
    }

    int read = 0;
    if ((read += readn(from, message->header, sizeof(msg_header))) == -1) return -1;
    message->data = NULL;
    if (message->header->data_size > 0) {
        message->data = malloc(message->header->data_size);
        if (!message->data) return -1;
        if ((read += readn(from, message->data, message->header->data_size)) == -1) {
            free(message->data);
            return -1;
        }
    }
    return read;
}

static inline msg_t *buildmsg(char *username, int code, int arg, const char *pathname, size_t data_size, void *data) {

    if (pathname && strlen(pathname) >= MAX_PATH) {
        errno = EINVAL;
        return NULL;
    }
    if (!username || strlen(username) >= MAX_USERNAME){
        errno = EINVAL;
        return NULL;
    }

    msg_t *message = calloc(1, sizeof(msg_t));
    if (!message) return NULL;
    message->header = calloc(1, sizeof(msg_header));
    if (!message->header) return NULL;

    memset(message->header, 0, sizeof(msg_header));
    if(pathname) {
        strncpy(message->header->pathname, pathname, strlen(pathname));
        message->header->pathname[strlen(pathname)] = 0;
    }
    strncpy(message->header->username, username, strlen(username));
    message->header->username[strlen(username)] = 0;

    message->header->code = code;
    message->header->arg = arg;
    message->header->data_size = data_size;
    if (data_size > 0) {
        message->data = malloc(data_size);
        if (!message->data) return NULL;
        memcpy(message->data, data, data_size);
    }
    return message;
}

static inline msg_t* initmsg() {
    msg_t *message = malloc(sizeof(msg_t));
    if (!message) return NULL;
    message->header = malloc(sizeof(msg_header));
    if (!message->header) return NULL;

    message->header->code = -1;
    message->header->arg = -1;
    memset(message->header->username, 0, MAX_USERNAME);
    memset(message->header->pathname, 0, MAX_PATH);
    message->header->data_size = 0;
    message->data = NULL;

    return message;
}

static inline void destroymsg(msg_t *message) {
    free(message->header);
    if (message->data) free(message->data);
    free(message);
}

#endif //FILE_STORAGE_SERVER_PROTOCOL_H
