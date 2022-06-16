
#ifndef FILE_STORAGE_SERVER_STORAGE_H
#define FILE_STORAGE_SERVER_STORAGE_H

#include "util.h"

typedef struct confArgs {
    char *sktname;
    size_t nworkers;
    size_t storagecapacity;
    size_t filelimit;
} configArgs;

int parse_configline(char* line, configArgs* cargs);
int parse_config(char* config_filename, configArgs* cargs);
void cleanup();

#endif //FILE_STORAGE_SERVER_STORAGE_H
