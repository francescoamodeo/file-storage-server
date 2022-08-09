#ifndef FILE_STORAGE_SERVER_WORKER_H
#define FILE_STORAGE_SERVER_WORKER_H

#include <protocol.h>

void requesthandler(int *clientfd);

int w_openFile(msg_t *request, int clientfd);
int w_readFile(msg_t *request, int clientfd);
int w_readNFile(msg_t *request, int clientfd);
int w_writeFile(msg_t *request, int clientfd);
int w_appendToFile(msg_t *request, int clientfd);
int w_lockFile(msg_t *request, int clientfd);
int w_unlockFile(msg_t *request, int clientfd);
int w_closeFile(msg_t *request, int clientfd);
int w_removeFile(msg_t *request, int clientfd);
int w_closeConnection(msg_t *request, int clientfd);

int compare_msg_path(void *m1, void *m2);
int client_waitlock(msg_t *lock_request);
int client_completelock(msg_t *unlock_request);

#endif //FILE_STORAGE_SERVER_WORKER_H
