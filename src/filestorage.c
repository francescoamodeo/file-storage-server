
#include "util.h"
#include <time.h>
#include "conn.h"
#include "filestorage.h"

bool verbose = false;
bool already_connected = false;
int socketfd = -1;
char *socketname;


int openConnection(const char* sockname, int msec, const struct timespec abstime) {

    if (already_connected) {
        errno = EISCONN;
        return -1;
    }
    if (sockname == NULL || msec < 0) {
        errno = EINVAL;
        return -1;
    }
    if (strlen(sockname) >= UNIX_PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    strncpy(sa.sun_path, sockname, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    SYSCALL_EXIT(socket, socketfd, socket(AF_UNIX, SOCK_STREAM, 0), "client socket", "");

    time_t now;
    time(&now);
    #if verbose
    printf("< Connecting to server...\n");
    #endif
    while ((already_connected = connect(socketfd, (struct sockaddr *) &sa, sizeof(sa)) == -1)
            && now < abstime.tv_sec) {
        #if verbose
        printf("< Unable to connect to server. Retry in %d msec\n", msec);
        #endif
        msleep(msec);
        time(&now);
    }
    if(already_connected != 0){
        #if verbose
        printf("< Unable to connect to server. Connection timed out\n");
        #endif
        errno = ETIMEDOUT;
        return -1;
    }
    already_connected = true;
    socketname = strndup(sockname, strlen(sockname));
    #if verbose
    printf("< Connection with server established\n");
    #endif
    return 0;
}

int closeConnection(const char* sockname){

    if(!already_connected){
        errno = ENOTCONN;
        return -1;
    }
    if(strlen(sockname) >= UNIX_PATH_MAX){
        errno = ENAMETOOLONG;
        return -1;
    }
    if(sockname == NULL || strcmp(sockname, socketname) != 0){
        errno = EINVAL;
        return -1;
    }

    __attribute__((unused)) int unused;
    SYSCALL_EXIT(close, unused, close(socketfd), "close", "")

    #if verbose
    printf("< Connection closed successfully\n");
    #endif
    return 0;
}

int openFile(const char* pathname, int flags){

    if(already_connected == false) {
        errno = ENOTCONN;
        return -1;
    }
    if(!pathname){
        errno = EINVAL;
        return -1;
    }
    if(strlen(pathname) > MAX_PATH){
        errno = ENAMETOOLONG;
        return -1;
    }
    if(flags < 0 || flags > 3) {
        errno = EINVAL;
        return -1;
    }

}

int readFile(const char* pathname, void** buf, size_t* size){

}

int readNFiles(int N, const char* dirname){

}

//TODO Se il contenuto del file è più grande del payload allora la parte restante
//verrà inviata al server usando la appendToFile
int writeFile(const char* pathname, const char* dirname){

}

int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname){

}

int lockFile(const char* pathname){

}

int unlockFile(const char* pathname){

}

int closeFile(const char* pathname){

}

int removeFile(const char* pathname){

}
