#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <stdlib.h>
#include "filestorage.h"
#include "util.h"
#include "conn.h"
#include "queue.h"

typedef struct cmdlinerequest {
    int opt;
    char *arg;
} cmdrequest;

char *sktname;
bool optf_requested = false;
bool opth_requested = false;
bool optD_requested = false;
bool optd_requested = false;
int request_delay = 0;
extern bool Verbose;
extern bool already_connected;

void printcommands(char** args);
void sendrequests(queue_t *requests);
int isdot(const char dir[]);
queue_t* lsR(const char nomedir[], queue_t *files, int n);
void cleanup();

int main(int argc, char* argv[]) {

    CHECK_EQ_EXIT(atexit(cleanup), -1, "atexit")
    if (argc < 3) {
        fprintf(stderr, "Usage: %s -f <socketfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    queue_t *requests = init_queue();
    int opt;
    char *tmpstr;
    char *tok;
    int requested = 0;
    cmdrequest *request;
    while (requested < MAX_CONSECUTIVE_REQUESTS
           && (opt = getopt(argc, argv, ":hf:w:W:Dr:dR::t::l:u:c:p")) != -1) {
#if DEBUG
        printf("optind generale: %d\n", optind);
        printf("opt: %c\n", opt);
        printf("optarg: %s\n", optarg);
#endif
        switch (opt) {
            case ':':
                printf("< -%c option requires an argument\n", optopt);
                exit(EXIT_FAILURE);
            case '?':
                printf("< Unrecognised option -%c\n", optopt);
                exit(EXIT_FAILURE);
            case 'h':
                if (opth_requested) {
                    printf("< Option -%c already requested. Cannot repeat -%c option multiple times\n", opt, opt);
                    exit(EXIT_FAILURE);
                }
                printcommands(argv);
                opth_requested = true;
                exit(EXIT_SUCCESS);
            case 'f': {
                if (optf_requested) {
                    printf("< Client already connected. Cannot repeat -%c option multiple times\n", opt);
                    exit(EXIT_FAILURE);
                }
                if (!checkfile_ext(optarg, "sk")){
                    printf("< Invalid argument for -f option. %s is not a valid socket file\n", optarg);
                    exit(EXIT_FAILURE);
                }
                size_t length;
                if ((length = strlen(optarg)) > UNIX_PATH_MAX)
                    printf("< Socket name length: %zu, exceeds maximum file name length: %d\n", length,
                           UNIX_PATH_MAX);

                //dopo aver controllato la richiesta la metto in coda
                MALLOC(request, 1, cmdrequest)
                request->opt = opt;
                request->arg = optarg;
                //l'operzione di connessione ha la precedenza rispetto alle altre richieste in coda
                pushfirst(requests, (void *) request);
                requested++;
                optf_requested = true;
                break;
            }
            case 'w': {
                //separo il nome della directory dal parametro n se presente
                tok = strtok_r(optarg, ",", &tmpstr); //il primo token è dirname
                //check se è una directory
                struct stat statbuf;
                int unused;
                SYSCALL_EXIT(stat, unused, stat(tok, &statbuf), "stat", "")
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

                //controllo se è indicata l'opzione congiunta -D
                optD_requested = false;
                if (optind < argc && strcmp(argv[optind], "-D") == 0) {
                    int optD = getopt(argc, argv, ":D:");
                    switch (optD) {
                        case ':':
                            printf("< -%c option requires an argument\n", optopt);
                            exit(EXIT_FAILURE);
                        case 'D': { //TODO check directory
                            optD_requested = true;
                            break;
                        }
                        default:
                            printf("< Unknown error\n");
                            exit(EXIT_FAILURE);
                    }
                }
                //esploro ricorsivamente la dir per trovare tutti i file per cui dovrò richiedere una write
                queue_t *files = init_queue();
                files = lsR(dirname, files, (int) n);
                //inserisco tutte le richieste effettive in coda che verranno gestite una ad una dalla sendrequest()
                for (int i = 0; i < files->length; ++i) {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    char *file = (char *) pop(files);

                    //se è stata specificata l'opzione -D concateno al nome del file da scrivere la directory di store
                    if (optD_requested) {
                        int arglen = (int) (strlen(file) + strlen(optarg) + 3);
                        MALLOC(request->arg, arglen, char)
                        strncpy(request->arg, file, strlen(file));
                        strncat(request->arg, ",", 2);
                        strncat(request->arg, optarg, strlen(optarg));
                    } else request->arg = strndup(file, strlen(file));
                    push(requests, (void *) request);
                    requested++;
                }
                break;
            }
            case 'W': {
                //TODO controllo optarg
                //controllo se è indicata l'opzione congiunta -D
                char *filelist = optarg;
                optD_requested = false;
                if (optind < argc && strcmp(argv[optind], "-D") == 0) {
                    int optD = getopt(argc, argv, ":D:");
                    switch (optD) {
                        case ':':
                            printf("< -%c option requires an argument\n", optopt);
                            exit(EXIT_FAILURE);
                        case 'D':
                            optD_requested = true;
                            break;
                        default:
                            printf("< Unknown error\n");
                            exit(EXIT_FAILURE);
                    }
                }
                tok = strtok_r(filelist, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = 'w'; //tratto il caso W come se fosse w dopo l'esplorazione delle dir
                    //se è stata specificata l'opzione -D concateno al nome del file da scrivere la directory di store
                    if (optD_requested) {
                        int arglen = (int) (strlen(tok) + strlen(optarg) + 3);
                        MALLOC(request->arg, arglen, char)
                        strncpy(request->arg, tok, strlen(tok));
                        strncat(request->arg, ",", 2);
                        strncat(request->arg, optarg, strlen(optarg));
                    } else request->arg = strndup(tok, strlen(tok));
                    push(requests, (void *) request);
                    requested++;
                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                break;
            }
            case 'D':
                printf("< -D option requires to be used jointly with -w or -W options\n");
                exit(EXIT_FAILURE);
            case 'r': {
                //TODO controllo optarg
                //controllo se è indicata l'opzione congiunta -d
                char *filelist = optarg;
                optd_requested = false;
                if (optind < argc && strcmp(argv[optind], "-d") == 0) {
                    int optd = getopt(argc, argv, ":d:");
                    switch (optd) {
                        case ':':
                            printf("< -%c option requires an argument\n", optopt);
                            exit(EXIT_FAILURE);
                        case 'd': //TODO check directory esistente
                            optd_requested = true;
                            break;
                        default:
                            printf("< Unknown error\n");
                            exit(EXIT_FAILURE);
                    }
                }
                tok = strtok_r(filelist, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    //se è stata specificata l'opzione -D concateno al nome del file da scrivere la directory di store
                    if (optd_requested) {
                        int arglen = (int) (strlen(tok) + strlen(optarg) + 3);
                        MALLOC(request->arg, arglen, char)
                        strncpy(request->arg, tok, strlen(tok));
                        strncat(request->arg, ",", 2);
                        strncat(request->arg, optarg, strlen(optarg));
                    } else request->arg = strndup(tok, strlen(tok));
                    push(requests, (void *) request);
                    requested++;
                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                break;
            }
            case 'R': {
                long n = 0;
                char *nstr = "\0";
                if (optarg) { //parametro opzionale n presente
                    if (isNumber(optarg, &n) != 0 || n < 0) {
                        if (errno == ERANGE)
                            printf("< Invalid argument for -w option. %s is out of range\n", optarg);
                        else printf("< Invalid argument for -w option. %s must be a non negative number\n", optarg);
                        exit(EXIT_FAILURE);
                    }
                    nstr = optarg;
                }


                optd_requested = false;
                if (optind < argc && strcmp(argv[optind], "-d") == 0) {
                    int optd = getopt(argc, argv, ":d:");
                    switch (optd) {
                        case ':':
                            printf("< -%c option requires an argument\n", optopt);
                            exit(EXIT_FAILURE);
                        case 'd': //TODO check directory
                            optd_requested = true;
                            break;
                        default:
                            printf("< Unknown error\n");
                            exit(EXIT_FAILURE);
                    }
                }

                MALLOC(request, 1, cmdrequest)
                request->opt = opt;
                if (optd_requested) {
                    int arglen = (int) (strlen(nstr) + strlen(optarg) + 3);
                    MALLOC(request->arg, arglen, char)
                    strncpy(request->arg, nstr, strlen(nstr));
                    strncat(request->arg, ",", 2);
                    strncat(request->arg, optarg, strlen(optarg));
                } else request->arg = nstr;
                push(requests, (void *) request);
                requested++;
                break;
            }
            case 'd':
                printf("< -d option requires to be used jointly with -r or -R options\n");
                exit(EXIT_FAILURE);
            case 't':{ //TODO check timer
                long n = 0;
                if (optarg) //parametro opzionale time presente
                    if (isNumber(optarg, &n) != 0 || n < 0) {
                        if (errno == ERANGE)
                            printf("< Invalid argument for -w option. %s is out of range\n", tok);
                        else printf("< Invalid argument for -w option. %s must be a non negative number\n", tok);
                        exit(EXIT_FAILURE);
                    }
                request_delay = (int)n;
                break;
            }
            case 'l': {
                tok = strtok_r(optarg, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    request->arg = strndup(tok, strlen(tok));
                    push(requests, (void *) request);
                    requested++;
                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                break;
            }
            case 'u': {
                tok = strtok_r(optarg, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    request->arg = strndup(tok, strlen(tok));
                    push(requests, (void *) request);
                    requested++;
                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                break;
            }
            case 'c': {
                tok = strtok_r(optarg, ",", &tmpstr);
                do {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    request->arg = strndup(tok, strlen(tok));
                    push(requests, (void *) request);
                    requested++;
                } while ((tok = strtok_r(NULL, ",", &tmpstr)) != NULL);
                break;
            }
            case 'p': {
                if (Verbose) {
                    printf("< Option -%c already requested. Cannot repeat -%c option multiple times\n", opt, opt);
                    exit(EXIT_FAILURE);
                }
                Verbose = true;
#if DEBUG
                printf("verbose = %d\n", Verbose);
#endif
                break;
            }
            default:
                printf("< Unrecognised option -%c\n", optopt);
                exit(EXIT_FAILURE);
        }
    }
    if(!optf_requested) {
        printf("< Option -f not included. Cannot send requests without connecting to the server\n");
        exit(EXIT_FAILURE);
    }
    if (requested > MAX_CONSECUTIVE_REQUESTS) {
        cmdrequest last = *(cmdrequest *) requests->tail->data;
        printf("< Maximum consecutive requests limit reached. Sending the first %d request in order, until -%c %s\n",
               MAX_CONSECUTIVE_REQUESTS, last.opt, last.arg);
    }
    sendrequests(requests);
}

void sendrequests(queue_t *requests) {
    assert(requests != NULL);
    cmdrequest *request;
    char *tmpstr;
    while ((request = (cmdrequest *) pop(requests))) {
#if DEBUG
        printf("request: %c\n", request->opt);
        printf("requestarg: %s\n", request->arg);
#endif
        switch (request->opt) {
            case 'f': {
                CHECK_EQ_EXIT(sktname = strndup(request->arg, strlen(request->arg)), NULL, "strdup")
                struct timespec abstime;
                time(&abstime.tv_sec);
                abstime.tv_sec += CONN_TIMEOUT_SEC;

                CHECK_EQ_EXIT(openConnection(sktname, RETRY_CONN_MSEC, abstime), -1, "openConnection")
                break;
            }
            case 'w': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da scrivere
                char *storedir = strtok_r(NULL, ",", &tmpstr); //directory D
                char abspath[PATH_MAX];
                CHECK_EQ_EXIT(realpath(file, abspath), NULL, "realpath")
                CHECK_EQ_EXIT(openFile(abspath, O_CREATE | O_LOCK), -1, "openFile")
                CHECK_EQ_EXIT(writeFile(abspath, storedir), -1, "writeFile")
                CHECK_EQ_EXIT(closeFile(abspath), -1, "closeFile")
                break;
            }
            case 'r': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da leggere
                char abspath[PATH_MAX];
                void *buf;
                size_t bufsize;
                CHECK_EQ_EXIT(realpath(file, abspath), NULL, "realpath")
                CHECK_EQ_EXIT(openFile(file, O_CREATE | O_LOCK), -1, "openFile")
                CHECK_EQ_EXIT(readFile(abspath, &buf, &bufsize), -1, "writeFile")
                char *storedir = strtok_r(NULL, ",", &tmpstr); //directory d
                if (storedir) {
                    //estraggo dal percorso assoluto il nome del file da salvare in storedir/
                    char *tmpfilename;
                    while ((file = strtok_r(abspath, "/", &tmpstr)) != NULL) {
                        tmpfilename = file;
                    }
                    char abs_storedir[PATH_MAX];
                    CHECK_EQ_EXIT(realpath(storedir, abs_storedir), NULL, "realpath")
                    strncat(abs_storedir, "/", 2);
                    strncat(abs_storedir, tmpfilename, strlen(tmpfilename));
                    FILE *ptr = NULL;
                    CHECK_EQ_EXIT((ptr = fopen(abs_storedir, "w+")), NULL, "fopen")
                    CHECK_EQ_EXIT(fwrite(buf, bufsize, 1, ptr), -1, "fwrite")
                    CHECK_EQ_EXIT(fclose(ptr), EOF, "fclose")
                } else {
                    //se non è stata indicata la directory di store stampo il file letto sullo stdout
                    printf("%s", (char *) buf);
                }
                CHECK_EQ_EXIT(closeFile(abspath), -1, "closeFile")
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
                char abs_storedir[PATH_MAX];
                char *storedir = strtok_r(NULL, ",", &tmpstr); //directory d
                if (storedir) { //directory d inclusa
                    CHECK_EQ_EXIT(realpath(storedir, abs_storedir), NULL, "realpath")
                    CHECK_EQ_EXIT(readNFiles(n, abs_storedir), -1, "readNFiles")
                } else CHECK_EQ_EXIT(readNFiles(n, NULL), -1, "readNFiles")
                break;
            }
            case 'l': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da lockare
                char abspath[PATH_MAX];
                CHECK_EQ_EXIT(realpath(file, abspath), NULL, "realpath")
                CHECK_EQ_EXIT(lockFile(abspath), -1, "lockFile")
                break;
            }
            case 'u': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da unlockare
                char abspath[PATH_MAX];
                CHECK_EQ_EXIT(realpath(file, abspath), NULL, "realpath")
                CHECK_EQ_EXIT(unlockFile(abspath), -1, "unlockFile")
                break;
            }
            case 'c': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da cancellare
                char abspath[PATH_MAX];
                CHECK_EQ_EXIT(realpath(file, abspath), NULL, "realpath")
                CHECK_EQ_EXIT(removeFile(abspath), -1, "removeFile")
                break;
            }
        }
        msleep(request_delay);
    }
#if DEBUG
    printf("richieste esaurite\n");
#endif
}

int isdot(const char dir[]) {
    int l = (int)strlen(dir);

    if ( (l>0 && dir[l-1] == '.') ) return 1;
    return 0;
}


queue_t* lsR(const char nomedir[], queue_t *files, int n) {
    assert(nomedir != NULL);
    //se n non è stato indicato nell'opzione -w (oppure n = 0) lo settiamo al massimo valore possibile
    //in questo modo non c'è limite al numero di file per cui fare la write
    if(n == 0) n = INT_MAX;
    //controllo che sia una directory
    struct stat sb;
    int unused;
    SYSCALL_EXIT(stat,unused,stat(nomedir,&sb),"stat","")

    DIR *dir;
    CHECK_EQ_EXIT(dir = opendir(nomedir), NULL, "opendir")

    struct dirent *file;
    while((errno=0, file =readdir(dir)) != NULL && n > 0) {
        struct stat statbuf;
       char *filename; //TODO PATH_MAX in limit.h
        int len = ((int) strlen(nomedir) + (int) strlen(file->d_name))+10;
        if (len > MAX_PATH) {
            PRINT_ERROR("MAX_PATH troppo piccolo")
            exit(EXIT_FAILURE);
        }

        MALLOC(filename, MAX_PATH, char)
        strncpy(filename, nomedir, MAX_PATH - 1); //TODO controllare boundaries stringhe
        strncat(filename, "/", MAX_PATH - 1);
        strncat(filename, file->d_name, MAX_PATH - 1);

        SYSCALL_EXIT(stat, unused, stat(filename, &statbuf), "stat", "")
        if (S_ISDIR(statbuf.st_mode)) {
            if (!isdot(filename)) files = lsR(filename, files, n);
        } else {
            push(files, (void *) filename);
            n--;
        }
    }
    if (errno != 0) PRINT_PERROR("readdir")
    closedir(dir);
    return files;
}

void printcommands(char **args){
    printf(
    "Usage: %s -f <socketfile> [OPTIONS]\n"
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

void cleanup(){
#if DEBUG
    printf("cleanup\n");
#endif
    //if(already_connected) closeConnection(sktname);
    //TODO frees
}