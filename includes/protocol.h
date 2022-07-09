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
    CLOSE_CONN_SUCCESS = 210,

    //client error response
    FILE_ALREADY_OPEN = 400,
    FILE_ALREADY_CLOSED = 401,
    FILE_ALREADY_EXISTS = 402,
    FILE_ALREADY_WRITTEN = 403,
    FILE_NOT_FOUND = 404,
    FILE_NOT_OPENED = 405,
    FILE_EMPTY = 406,
    FILE_TOO_BIG = 407,
    STORAGE_EMPTY = 408,
    INVALID_ARGUMENT = 409,
    PERMISSION_DENIED = 410,
    FILE_ALREADY_UNLOCKED = 411,

    //server error response
    INTERNAL_ERROR = 500

} response_c;


#endif //FILE_STORAGE_SERVER_PROTOCOL_H
