
#include "server/storage.h"


int parse_config(char* config_filename, configArgs* cargs){
    FILE *ptr = NULL;
    if ((ptr = fopen(config_filename, "r")) == NULL){
        PRINT_PERROR("fopen")
        return -1;
    }
    char line[BUFSIZE];
    while(fgets(line, BUFSIZE, ptr)) {
        if(parse_configline(line, cargs) == -1){
            if(fclose(ptr) != 0) PRINT_PERROR("fclose")
            return -1;
        }
    }
    if(!feof(ptr)) {
        PRINT_ERROR("fgets")
        return -1;
    }
    if(fclose(ptr) != 0){
        PRINT_PERROR("fclose")
        return -1;
    }
    return 0;
}

int parse_configline(char* line, configArgs* cargs){
    char *tmpstr;
    char *tok = strtok_r(line, "=", &tmpstr);
    long value = 0;

    //parsing nome socket
    if (strcmp(tok, "SOCKET_NAME") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            PRINT_ERROR("Missing socket name argument")
            return -1;
        }
        if (strlen(tok) >= MAX_PATH) {
            PRINT_ERROR("Socket name too long");
            return -1;
        }
        cargs->sktname = strndup(tok, strlen(tok));
        if(cargs->sktname == NULL){
            PRINT_PERROR("strdup")
            return -1;
        }
        return 0;
    }

    //parsing capacità massima storage
    if (strcmp(tok, "STORAGE_CAPACITY") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            PRINT_ERROR("Missing storage capacity argument")
            return -1;
        }
        TRUNC_NEWLINE(tok)
        if (isNumber(tok, &value) != 0 || value <= 0) {
            PRINT_ERROR("Invalid storage capacity argument");
            return -1;
        }
        cargs->storagecapacity = value;
        return 0;
    }

    //parsing limite file
    if (strcmp(tok, "FILE_LIMIT") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            PRINT_ERROR("Missing file limit argument");
            return -1;
        }
        TRUNC_NEWLINE(tok)
        if (isNumber(tok, &value) != 0 || value <= 0) {
            PRINT_ERROR("Invalid file limit argument");
            return -1;
        }
        cargs->filelimit = value;
        return 0;
    }

    //parsing numero di thread workers
    if (strcmp(tok, "N_WORKERS") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            PRINT_ERROR("Missing thread workers argument")
            return -1;
        }
        TRUNC_NEWLINE(tok)
        if (isNumber(tok, &value) != 0 || value <= 0) {
            PRINT_ERROR("Invalid thread workers argument");
            return -1;
        }
        cargs->nworkers = value;
        return 0;
    }
    PRINT_ERROR("Unrecognized configuration option");
    return -1;
}