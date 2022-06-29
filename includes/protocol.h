#ifndef FILE_STORAGE_SERVER_PROTOCOL_H
#define FILE_STORAGE_SERVER_PROTOCOL_H

typedef enum request_code {
    OPEN,
    READ,
    WRITE
} request_c;

typedef enum response_code {
    INVALID,
    UNKNOWN,
} response_c;


//TODO allineamento
typedef struct request_type {
    request_c request_code;
    char *filepath;
    int flags;
    int payload_size;
    char* payload;
} request_t;

typedef struct response_type {
    response_c response_code;
    int request_length;
    char* request_args;
} response_t;



#endif //FILE_STORAGE_SERVER_PROTOCOL_H
