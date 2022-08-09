#ifndef FILE_STORAGE_SERVER_MANAGER_H
#define FILE_STORAGE_SERVER_MANAGER_H

#define PENDING_SIZE 50

typedef struct confArgs {
    char *sktname;
    char *logfile;
    int nworkers;
    int storagecapacity;
    int filelimit;
    int replace_mode;
} configArgs;

int parse_config(const char *config_filename, configArgs *cargs);
int log_operation(const char *OP, int IDCLIENT, size_t DELETED_BYTES, size_t ADDED_BYTES, size_t SENT_BYTES,
                  const char *OBJECT_FILE, const char *OUTCOME);

#endif //FILE_STORAGE_SERVER_MANAGER_H
