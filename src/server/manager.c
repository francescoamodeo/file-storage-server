
#include "../includes/server/storage.h"


int main(int argc, char *argv[]){

    atexit(cleanup);
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
                CHECK_EQ_EXIT(parsing config, (parse_config(optarg, &cargs)), -1, "parsing config", "")
                break;
            case ':':
                print_error("-f option requires an argument\n");
                exit(EXIT_FAILURE);
                break;
            default:
                print_error("Unrecognised option\n");
        }
    }
}

void cleanup(){
    printf("cleanup\n");
}