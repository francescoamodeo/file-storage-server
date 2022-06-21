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


typedef struct request_type {
    request_c request_code;
    int request_length;
    char* request_args;
} request_t;

typedef struct response_type {
    response_c response_code;
    int request_length;
    char* request_args;
} response_t;



#endif //FILE_STORAGE_SERVER_PROTOCOL_H
