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

}

void write_response(int clientfd){

}

