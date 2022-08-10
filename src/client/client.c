#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <assert.h>

#include <filestorage.h>
#include <protocol.h>
#include <queue.h>

typedef struct cmdlinerequest {
    int opt;
    char *arg;
} cmdrequest;

char *sktname;
cmdrequest *request = NULL;
queue_t *wfiles = NULL;
queue_t *requests = NULL;
bool optf_requested = false;
bool opth_requested = false;
bool optD_requested = false;
bool optd_requested = false;
int request_delay = 0;
extern bool Verbose;
extern bool already_connected;
extern char *username;

void sendrequests();
void destroyrequest(cmdrequest *request);
int isdot(const char dir[]);
queue_t* lsR(const char nomedir[], queue_t *files, int *n);
void printcommands(char** args);
void cleanup();

int main(int argc, char* argv[]) {

    CHECK_EQ_EXIT(atexit(cleanup), -1, "atexit")
    if (argc < 5) {
        fprintf(stderr, "< Usage: %s -a <username> -f <socketfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int opt;
    char *tmpstr;
    char *tok;
    CHECK_EQ_EXIT(requests = init_queue(), NULL, "init request queue")

    while ((opt = getopt(argc, argv, ":ha:f:w:W:Dr:dR::t::l:u:c:p")) != -1) {

        switch (opt) {
            case ':': {
                printf("< -%c option requires an argument\n", optopt);
                exit(EXIT_FAILURE);
            }
            case '?': {
                printf("< Unrecognised option -%c\n", optopt);
                exit(EXIT_FAILURE);
            }
            case 'h': {
                if (opth_requested) {
                    printf("< Option -%c already requested. Cannot repeat -%c option multiple times\n", opt, opt);
                    exit(EXIT_FAILURE);
                }
                printcommands(argv);
                opth_requested = true;
                exit(EXIT_SUCCESS);
            }
            case 'a': {
                if (username) {
                    printf("< Username already assigned. Cannot repeat -%c option multiple times\n", opt);
                    exit(EXIT_FAILURE);
                }
                size_t length;
                if ((length = strlen(optarg)) >= MAX_USERNAME)
                    printf("< Username length: %zu, exceeds maximum username length: %d\n", length, MAX_USERNAME);

                username = optarg;
                break;
            }
            case 'f': {
                if (optf_requested) {
                    printf("< Client already connected. Cannot repeat -%c option multiple times\n", opt);
                    exit(EXIT_FAILURE);
                }
                if (!checkfile_ext(optarg, "sk")) {
                    printf("< Invalid argument for -f option. %s is not a valid socket file\n", optarg);
                    exit(EXIT_FAILURE);
                }
                size_t length;
                if ((length = strlen(optarg)) >= UNIX_PATH_MAX)
                    printf("< Socket name length: %zu, exceeds maximum file name length: %d\n", length,
                           UNIX_PATH_MAX);

                //dopo aver controllato la richiesta la metto in coda
                MALLOC(request, 1, cmdrequest)
                request->opt = opt;
                CHECK_EQ_EXIT(request->arg = strndup(optarg, strlen(optarg)), NULL, "strndup")
                //l'operzione di connessione ha la precedenza rispetto alle altre richieste in coda
                CHECK_EQ_EXIT(pushfirst(requests, (void *) request), 1, "pushfirst queue")
                request = NULL; //per evitare dangling pointer

                optf_requested = true;
                break;
            }
            case 'w': {
                //separo il nome della directory dal parametro n se presente
                tok = strtok_r(optarg, ",", &tmpstr); //il primo token è dirname
                //check se è una directory
                struct stat statbuf;
                __attribute__((unused)) int unused;
                SYSCALL_EXIT(unused, stat(tok, &statbuf), "stat")
                if (!S_ISDIR(statbuf.st_mode)) {
                    printf("< Invalid argument for -w option. %s is not a directory\n", tok);
                    exit(EXIT_FAILURE);
                }
                char *dirname;
                CHECK_EQ_EXIT(dirname = strndup(tok, strlen(tok)), NULL, "strdup")
                tok = strtok_r(NULL, ",", &tmpstr);
                long n = 0;
                if (tok) { //parametro opzionale n presente
                    if (isNumber(tok, &n) != 0 || n < 0) {
                        if (errno == ERANGE)
                            printf("< Invalid argument for -w option. %s is out of range\n", tok);
                        else printf("< Invalid argument for -w option. %s must be a non negative number\n", tok);
                        exit(EXIT_FAILURE);
                    }
                }
                //se n non è stato indicato nell'opzione -w (oppure n = 0) lo settiamo al massimo valore possibile
                //in questo modo non c'è limite al numero di file per cui fare la write
                if (n == 0) n = INT_MAX;

                //controllo se è indicata l'opzione congiunta -D
                optD_requested = false;
                if (optind < argc && strcmp(argv[optind], "-D") == 0) {
                    int optD = getopt(argc, argv, ":D:");
                    switch (optD) {
                        case ':': {
                            printf("< -%c option requires an argument\n", optopt);
                            exit(EXIT_FAILURE);
                        }
                        case 'D': {
                            optD_requested = true;
                            break;
                        }
                        default: {
                            PRINT_ERROR("Unrecognised -D option") //non dovremmo arrivare qui
                            exit(EXIT_FAILURE);
                        }
                    }
                }
                //esploro ricorsivamente la dir per trovare tutti i file per cui dovrò richiedere una write
                CHECK_EQ_EXIT(wfiles = init_queue(), NULL, "files queue init")
                CHECK_EQ_EXIT(wfiles = lsR(dirname, wfiles, (int *) &n), NULL, "lsR")
                free(dirname);
                size_t filecount = wfiles->length;
                //inserisco tutte le richieste effettive in coda che verranno gestite una ad una dalla sendrequest()
                for (int i = 0; i < filecount; ++i) {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    char *file = NULL;
                    CHECK_EQ_EXIT(file = (char *) pop(wfiles), NULL, "pop file")

                    //se è stata specificata l'opzione -D concateno al nome del file da scrivere la directory di store
                    if (optD_requested) {
                        CHECK_EQ_EXIT(request->arg = strnconcat(file, ",", optarg, NULL), NULL, "strnconcat")
                    } else CHECK_EQ_EXIT(request->arg = strndup(file, strlen(file)), NULL, "strndup")
                    free(file);

                    CHECK_EQ_EXIT(push(requests, (void *) request), 1, "push request")
                }
                delete_queue(wfiles, free);
                wfiles = NULL;
                request = NULL;
                break;
            }
            case 'W': {
                //controllo se è indicata l'opzione congiunta -D
                char *filelist = optarg;
                optD_requested = false;
                if (optind < argc && strcmp(argv[optind], "-D") == 0) {
                    int optD = getopt(argc, argv, ":D:");
                    switch (optD) {
                        case ':': {
                            printf("< -%c option requires an argument\n", optopt);
                            exit(EXIT_FAILURE);
                        }
                        case 'D': {
                            optD_requested = true;
                            break;
                        }
                        default: {
                            PRINT_ERROR("Unrecognised -D option") //non dovremmo arrivare qui
                            exit(EXIT_FAILURE);
                        }
                    }
                }
                tok = strtok_r(filelist, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = 'w';
                    //tratto il caso W come se fosse w dopo l'esplorazione delle dir
                    //se è stata specificata l'opzione -D concateno al nome del file da scrivere la directory di store
                    if (optD_requested) {
                        CHECK_EQ_EXIT(request->arg = strnconcat(tok, ",", optarg, NULL), NULL, "strnconcat")
                    } else CHECK_EQ_EXIT(request->arg = strndup(tok, strlen(tok)), NULL, "strndup")

                    CHECK_EQ_EXIT(push(requests, (void *) request), 1, "push request")
                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                request = NULL;
                break;
            }
            case 'D': {
                printf("< -D option requires to be used jointly with -w or -W options\n");
                exit(EXIT_FAILURE);
            }
            case 'r': {
                //controllo se è indicata l'opzione congiunta -d
                char *filelist = optarg;
                optd_requested = false;
                if (optind < argc && strcmp(argv[optind], "-d") == 0) {
                    int optd = getopt(argc, argv, ":d:");
                    switch (optd) {
                        case ':': {
                            printf("< -%c option requires an argument\n", optopt);
                            exit(EXIT_FAILURE);
                        }
                        case 'd': {
                            optd_requested = true;
                            break;
                        }
                        default: {
                            PRINT_ERROR("Unrecognised -d option") //non dovremmo arrivare qui
                            exit(EXIT_FAILURE);
                        }
                    }
                }
                tok = strtok_r(filelist, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    //se è stata specificata l'opzione -D concateno al nome del file da scrivere la directory di store
                    if (optd_requested) {
                        CHECK_EQ_EXIT(request->arg = strnconcat(tok, ",", optarg, NULL), NULL, "strnconcat")
                    } else CHECK_EQ_EXIT(request->arg = strndup(tok, strlen(tok)), NULL, "strndup")

                    CHECK_EQ_EXIT(push(requests, (void *) request), 1, "push request")
                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                request = NULL;
                break;
            }
            case 'R': {
                long n = 0;
                char *nstr;
                if (optarg) { //parametro opzionale n presente
                    if (isNumber(optarg, &n) != 0 || n < 0) {
                        if (errno == ERANGE)
                            printf("< Invalid argument for -R option. %s is out of range\n", optarg);
                        else printf("< Invalid argument for -R option. %s must be a non negative number\n", optarg);
                        exit(EXIT_FAILURE);
                    }
                    nstr = optarg;
                } else nstr = "0";
                //se non viene indicato il numero di file da leggere uso 0 per dire che devono essere letti tutti

                optd_requested = false;
                if (optind < argc && strcmp(argv[optind], "-d") == 0) {
                    int optd = getopt(argc, argv, ":d:");
                    switch (optd) {
                        case ':': {
                            printf("< -%c option requires an argument\n", optopt);
                            exit(EXIT_FAILURE);
                        }
                        case 'd': {
                            optd_requested = true;
                            break;
                        }
                        default: {
                            printf("< Unknown error\n");
                            exit(EXIT_FAILURE);
                        }
                    }
                }

                MALLOC(request, 1, cmdrequest)
                request->opt = opt;
                if (optd_requested) {
                    CHECK_EQ_EXIT(request->arg = strnconcat(nstr, ",", optarg, NULL), NULL, "strnconcat")
                } else CHECK_EQ_EXIT(request->arg = strndup(nstr, strlen(nstr)), NULL, "strndup")

                CHECK_EQ_EXIT(push(requests, (void *) request), 1, "push request")
                request = NULL;
                break;
            }
            case 'd': {
                printf("< -d option requires to be used jointly with -r or -R options\n");
                exit(EXIT_FAILURE);
            }
            case 't': {
                long n = 0;
                if (optarg) //parametro opzionale time presente
                    if (isNumber(optarg, &n) != 0 || n < 0) {
                        if (errno == ERANGE)
                            printf("< Invalid argument for -t option. %s is out of range\n", tok);
                        else printf("< Invalid argument for -t option. %s must be a non negative number\n", tok);
                        exit(EXIT_FAILURE);
                    }

                request_delay = (int) n;
                break;
            }
            case 'l': {
                tok = strtok_r(optarg, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    CHECK_EQ_EXIT(request->arg = strndup(tok, strlen(tok)), NULL, "strndup")
                    CHECK_EQ_EXIT(push(requests, (void *) request), 1, "push request")

                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                request = NULL;
                break;
            }
            case 'u': {
                tok = strtok_r(optarg, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    CHECK_EQ_EXIT(request->arg = strndup(tok, strlen(tok)), NULL, "strndup")
                    CHECK_EQ_EXIT(push(requests, (void *) request), 1, "push request")

                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                request = NULL;
                break;
            }
            case 'c': {
                tok = strtok_r(optarg, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    CHECK_EQ_EXIT(request->arg = strndup(tok, strlen(tok)), NULL, "strndup")
                    CHECK_EQ_EXIT(push(requests, (void *) request), 1, "push request")

                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                request = NULL;
                break;
            }
            case 'p': {
                if (Verbose) {
                    printf("< Option -%c already requested. Cannot repeat -%c option multiple times\n", opt, opt);
                    exit(EXIT_FAILURE);
                }
                Verbose = true;
                break;
            }
            default: {
                printf("< Unrecognised option -%c\n", optopt);
                exit(EXIT_FAILURE);
            }
        }
    }
    if (!optf_requested) {
        printf("< Option -f not included. Cannot send requests without connecting to the server\n");
        exit(EXIT_FAILURE);
    }
    if (!username) {
        printf("< Option -a not included. Cannot send requests without authenticate\n");
        exit(EXIT_FAILURE);
    }
    sendrequests(requests);
    return 0;
}

void sendrequests() {
    assert(requests != NULL);
    char *tmpstr;
    while ((request = (cmdrequest *) pop(requests))) {
        switch (request->opt) {
            case 'f': {
                CHECK_EQ_EXIT(sktname = strndup(request->arg, strlen(request->arg)), NULL, "strdup")
                struct timespec abstime;
                time(&abstime.tv_sec);
                abstime.tv_sec += CONN_TIMEOUT_SEC;

                if (openConnection(sktname, RETRY_CONN_MSEC, abstime) == -1) exit(EXIT_FAILURE);
                break;
            }
            case 'w': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da scrivere
                char *storedir = strtok_r(NULL, ",", &tmpstr); //directory D
                char abspath[PATH_MAX];
                if (realpath(file, abspath) == NULL) {
                    PRINT_PERROR("realpath")
                    break;
                }
                if (openFile(abspath, O_CREATE | O_LOCK) == -1){
                    if (errno == EEXIST) { // se il file esiste già possiamo scriverlo solamente in append
                        if (openFile(abspath, O_NORMAL) == -1) break;
                        void *buf = NULL;
                        size_t file_size = 0;
                        if (readfile(abspath, &buf, &file_size) == -1) break;
                        if (appendToFile(abspath, buf, file_size, storedir) == -1){
                            free(buf);
                            break;
                        }
                        if (closeFile(abspath) == -1) {
                            free(buf);
                            break;
                        }
                        free(buf);
                    }
                    break;
                }
                if (writeFile(abspath, storedir) == -1) break;
                if (closeFile(abspath) == -1) break;
                break;
            }
            case 'r': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da leggere
                void *buf = NULL;
                size_t bufsize = 0;
                if (openFile(file, O_NORMAL) == -1) break;
                if (readFile(file, &buf, &bufsize) == -1) break;
                if (closeFile(file) == -1) {
                    free(buf);
                    break;
                }
                char *storedir = strtok_r(NULL, ",", &tmpstr); //directory d
                if (storedir && storefile(storedir, file, buf, bufsize) == -1) {
                    free(buf);
                    break;
                }
                free(buf);
                break;
            }
            case 'R': {
                char *nstr = strtok_r(request->arg, ",", &tmpstr);  //numero file da leggere
                long n = 0;
                if (nstr) {
                    char *e = NULL;
                    errno = 0;
                    n = strtol(nstr, &e, 10);
                    assert(errno != ERANGE);    // overflow/underflow
                    assert(e != NULL && *e == (char) 0);
                }
                char *storedir = strtok_r(NULL, ",", &tmpstr); //directory d
                if (readNFiles((int)n, storedir) == -1) break;
                break;
            }
            case 'l': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da lockare
                if (lockFile(file) == -1) break;
                break;
            }
            case 'u': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da unlockare
                if (unlockFile(file) == -1) break;
                break;
            }
            case 'c': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da cancellare
                if (removeFile(file) == -1) break;
                break;
            }
        }
        if (errno == ECONNRESET) {
            already_connected = false;
            exit(EXIT_FAILURE);
        }
        msleep(request_delay);
        destroyrequest(request);
    }
    if (closeConnection(sktname) == -1) exit(EXIT_FAILURE);
    delete_queue(requests, (void (*)(void *)) destroyrequest);
    requests = NULL;
}

int isdot(const char dir[]) {
    int l = (int)strlen(dir);

    if ( (l>0 && dir[l-1] == '.') ) return 1;
    return 0;
}

queue_t* lsR(const char nomedir[], queue_t *files, int *n) {
    //controllo che sia una directory
    struct stat sb;
    if (stat(nomedir,&sb) == -1) return NULL;

    DIR *dir;
    if ((dir = opendir(nomedir)) == NULL) return NULL;

    struct dirent *file;
    while((errno=0, file = readdir(dir)) != NULL && *n > 0) {
        struct stat statbuf;
        char filename[MAX_PATH];
        unsigned long len = strlen(nomedir) + strlen(file->d_name) + 2;
        if (len > MAX_PATH) {
            PRINT_ERROR("MAX_PATH value too short")
            return NULL;
        }

        strncpy(filename, nomedir, MAX_PATH - 1);
        strncat(filename, "/", MAX_PATH - 1);
        strncat(filename, file->d_name, MAX_PATH - 1);

        if (stat(filename, &statbuf) == -1) return NULL;
        if (S_ISDIR(statbuf.st_mode)) {
            if (!isdot(filename)) files = lsR(filename, files, n);
        } else {
            char *filepath = NULL;
            if ((filepath = strndup(filename, strlen(filename))) == NULL) return NULL;
            if (push(files, filepath) == 1) return NULL;
            (*n)--;
        }
    }
    if (errno != 0) PRINT_PERROR("readdir")
    closedir(dir);
    return files;
}


void printcommands(char **args){
    printf(
    "Usage: %s -a <username> -f <socketfile> [OPTIONS]\n"
    "-h                     Prints help message.\n"
    "-f <sockefile>         Specifies the connections socket.\n"
    "-w <dirname>[,n=0]     Requests a write for all files inside dirname. If dirname contains other subdirectories,\n"
    "                       these are recursively visited and no more than n files are written.\n"
    "                       If n was not included or n = 0 then there is no limit to the number of files\n"
    "                       that will be written.\n"
    "-W <file1>[,file2]     Requests a write for all files distinguished by ',' in the list.\n"
    "-D <dirname>           Specifies the path of the directory in which to save any files ejected from the server\n"
    "                       following a write. This option must be used jointly with -w or -W.\n"
    "-r <file1>[,file2]     Requests a read for all files distinguished by ',' in the list.\n"
    "-R[n=0]                Requests a read of any n files present on the server. If n was not included or n = 0 then\n"
    "                       all files on the server will be read. n argument must be attached to the option to work.\n"
    "-d <dirname>           Specifies the path of the directory in which to save the files received from the server\n"
    "                       following a read. This option must be used jointly with -r or -R.\n"
    "-t <time>              Sets the time in milliseconds between two consecutive requests to the server.\n"
    "-l file1[,file2]       Requests mutual exclusion access for all files distinguished by ',' in the list.\n"
    "-u file1[,file2]       Requests release of mutual exclusion for all files distinguished by ',' in the list.\n"
    "-c file1[,file2]       Requests deletion for all files distinguished by ',' in the list.\n"
    "-p                     Enables screen dialog for each operation.\n", args[0]);

}

void destroyrequest(cmdrequest *req){
    if (!req) return;
    if (req->arg) free(req->arg);
    req->arg = NULL;
    free(req);
    req = NULL;
}

void cleanup(){
    if (already_connected) closeConnection(sktname);
    if (requests) delete_queue(requests, (void (*)(void *)) destroyrequest);
    requests = NULL;
    if (sktname) free(sktname);
    sktname = NULL;
    if (request) destroyrequest(request);
    request = NULL;
    if (wfiles) delete_queue(wfiles, free);
    wfiles = NULL;
}