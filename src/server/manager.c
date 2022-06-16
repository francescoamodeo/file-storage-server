
#include "../includes/server/storage.h"


int main(int argc, char *argv[]){

    CHECK_EQ_EXIT(atexit(cleanup), -1, "atexit")
    if(argc < 3){
        fprintf(stderr, "Usage: %s -f <configfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //lettura configfile
    configArgs cargs = {"", 0, 0, 0};
    int opt;
    while ((opt = getopt(argc, argv, ":f:")) != -1){
        switch (opt) {
            case 'f':
                CHECK_ERROR_EXIT((parse_config(optarg, &cargs)), -1)
                break;
            case ':':
                PRINT_ERROR("-f option requires an argument")
                exit(EXIT_FAILURE);
            default:
                PRINT_ERROR("Unrecognised option");
                exit(EXIT_FAILURE);
        }
    }

    //creazione threadpool
    //threadpool_t *tpool = createThreadPool((int)cargs.nworkers, PENDING_SIZE);

}

void cleanup(){
    printf("cleanup\n");
}