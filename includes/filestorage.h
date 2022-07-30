
#ifndef FILE_STORAGE_SERVER_FILESTORAGE_H
#define FILE_STORAGE_SERVER_FILESTORAGE_H

#include <time.h>

int openConnection(const char* sockname, int msec, struct timespec abstime);

int closeConnection(const char* sockname);

int openFile(const char* pathname, int flags);

int readFile(const char* pathname, void** buf, size_t* size);

int readNFiles(int N, const char* dirname);

int writeFile(const char* pathname, const char* dirname);

int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);

int lockFile(const char* pathname);

int unlockFile(const char* pathname);

int closeFile(const char* pathname);

int removeFile(const char* pathname);

int storeFile(const char *dirname, char *filename, void *data, size_t data_size);

int verbose(const char * restrict format, ...);

#endif //FILE_STORAGE_SERVER_FILESTORAGE_H
