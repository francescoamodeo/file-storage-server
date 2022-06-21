#include "worker.h"

extern response_t **clients;

void requesthandler(int clientfd){
    if(clients[clientfd] == NULL) {
        request_t *request;
        MALLOC(request, 1, request_t)
        read_request(clientfd, request);
    }
    else
        write_response(clientfd);
}

void read_request(int clientfd, request_t* r){
    struct iovec request[3];
    memset(request, 0, sizeof(request));

    //TODO
    request[0].iov_base = &r->request_code;
    request[0].iov_len = sizeof(request_t);
    request[1].iov_base = &r->request_length;
    request[1].iov_len = sizeof(int);
    request[2].iov_base = r->request_args;
    request[2].iov_len = (size_t) request[1].iov_base;
}

void write_response(int clientfd){

}

