#ifndef FILE_STORAGE_SERVER_WORKER_H
#define FILE_STORAGE_SERVER_WORKER_H

#include "protocol.h"
#include "conn.h"
#include "util.h"

void requesthandler(int clientfd);
void read_request(int clientfd, request_t* request);
void write_response(int clientfd);

#endif //FILE_STORAGE_SERVER_WORKER_H
