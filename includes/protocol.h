#ifndef FILE_STORAGE_SERVER_PROTOCOL_H
#define FILE_STORAGE_SERVER_PROTOCOL_H

#include "conn.h"
#include "util.h"

typedef enum request_code {
    OPEN,
    READ,
    READN,
    WRITE,
    APPEND,
    LOCK,
    UNLOCK,
    CLOSE,
    REMOVE
} request_c;

typedef enum response_code {

    //api success response
    OPEN_FILE_SUCCESS = 200,
    READ_FILE_SUCCESS = 201,
    READN_FILE_SUCCESS = 202,
    WRITE_FILE_SUCCESS = 203,
    APPEND_FILE_SUCCESS = 204,
    LOCK_FILE_SUCCESS = 206,
    UNLOCK_FILE_SUCCESS = 207,
    CLOSE_FILE_SUCCESS = 208,
    REMOVE_FILE_SUCCESS = 209,
    CLOSE_CONN_SUCCESS = 270,

    //client error response
    FILE_ALREADY_OPEN = 400,
    FILE_ALREADY_CLOSED = 401,
    FILE_ALREADY_EXISTS = 402,
    FILE_NOT_OPENED = 403,
    FILE_NOT_FOUND = 404,
    FILE_EMPTY = 405,
    WRITE_FAILED = 406,
    INVALID_ARGUMENT = 407,
    PERMISSION_DENIED = 408,

    //server error response
    INTERNAL_ERROR = 500

} response_c;


#endif //FILE_STORAGE_SERVER_PROTOCOL_H
