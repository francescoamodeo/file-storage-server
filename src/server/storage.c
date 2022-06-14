
#include "server/storage.h"


int parse_config(char* config_filename, configArgs* cargs){
    FILE *ptr = NULL;
    if ((ptr = fopen(config_filename, "r")) == NULL){
        perror("fopen configfile");
        return -1;
    }
    char line[BUFSIZE];
    while(fgets(line, BUFSIZE, ptr)) {
        if(parse_configline(line, cargs) == -1){
            print_error("parsing line configfile\n");
            fclose(ptr);
            return -1;
        }
    }
    fclose(ptr);
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
            print_error("Missing socket name argument\n");
            return -1;
        }
        if (strlen(tok) >= MAX_PATH) {
            print_error("Socket name too long\n");
            return -1;
        }
        cargs->sktname = strndup(tok, strlen(tok));
        return 0;
    }

    //parsing capacità massima storage
    if (strcmp(tok, "STORAGE_CAPACITY") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            print_error("Missing storage capacity argument\n");
            return -1;
        }
        TRUNC_NEWLINE(tok)
        if (isNumber(tok, &value) != 0 || value <= 0) {
            print_error("Invalid storage capacity argument\n");
            return -1;
        }
        cargs->storagecapacity = value;
        return 0;
    }

    //parsing limite file
    if (strcmp(tok, "FILE_LIMIT") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            print_error("Missing file limit argument\n");
            return -1;
        }
        TRUNC_NEWLINE(tok)
        if (isNumber(tok, &value) != 0 || value <= 0) {
            print_error("Invalid file limit argument\n");
            return -1;
        }
        cargs->filelimit = value;
        return 0;
    }

    //parsing numero di thread workers
    if (strcmp(tok, "N_WORKERS") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            print_error("Missing thread workers argument\n");
            return -1;
        }
        TRUNC_NEWLINE(tok)
        if (isNumber(tok, &value) != 0 || value <= 0) {
            print_error("Invalid thread workers argument\n");
            return -1;
        }
        cargs->nworkers = value;
        return 0;
    }
    print_error("Unrecognized configuration\n");
    return -1;
}