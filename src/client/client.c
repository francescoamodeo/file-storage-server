#include <dirent.h>
#include <time.h>
#include <limits.h>
#include "filestorage.h"
#include "util.h"
#include "conn.h"
#include "queue.h"

typedef struct cmdlinerequest {
    int opt;
    char *arg;
} cmdrequest;

char *sktname;
bool verbose_mode = false;
bool already_connected = false;
bool optD_requested = false;
bool optd_requested = false;


void printcommands();
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
           && (opt = getopt(argc, argv, ":hf:w:W:D:r:d:R::t:l:u:c:p")) != -1) {
        switch (opt) {
            case ':':
                printf("< -%c option requires an argument\n", optopt);
                exit(EXIT_FAILURE);
            case '?':
                printf("< Unrecognised option -%c\n", optopt);
                exit(EXIT_FAILURE);
            case 'h':
                printcommands();
                exit(EXIT_SUCCESS);
            case 'f': {
                if (already_connected) {
                    printf("< Client already connected. Cannot repeat -f option multiple times\n");
                    exit(EXIT_FAILURE);
                }
                size_t length;
                if ((length = strlen(optarg)) > MAX_FILE_NAME)
                    printf("< Socket name length: %zu, exceeds maximum file name length: %d\n", length,
                           MAX_FILE_NAME);

                //dopo aver controllato la richiesta la metto in coda
                MALLOC(request, 1, cmdrequest)
                request->opt = opt;
                request->arg = optarg;
                push(requests, (void *) request);
                requested++;
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
                if (tok) //parametro opzionale n presente
                    if (isNumber(tok, &n) != 0 || n < 0) {
                        if (errno == ERANGE)
                            printf("< Invalid argument for -w option. %s is out of range\n", tok);
                        else printf("< Invalid argument for -w option. %s must be a non negative number\n", tok);
                        exit(EXIT_FAILURE);
                    }

                //controllo se è indicata l'opzione congiunta -D
                optD_requested = false;
                if(strcmp(argv[optind], "D") == 0){
                    int optD = getopt(1, argv, ":D:");
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

                //esploro ricorsivamente la dir per trovare tutti i file per cui dovrò richiedere una write
                queue_t *files = init_queue();
                files = lsR(dirname, files, (int) n);
                //inserisco tutte le richieste effettive in coda che verranno gestite una ad una dalla sendrequest()
                for (int i = 0; i < files->length; ++i) {
                    MALLOC(request, 1, cmdrequest)
                    request->opt = opt;
                    char *file = (char *) pop(files);
                    //se è stata specificata l'opzione -D concateno al nome del file da scrivere la directory di store
                    if(optD_requested) {
                        int arglen = (int)(strlen(file) + strlen(optarg) + 3);
                        MALLOC(request->arg, arglen, char)
                        strncpy(request->arg, file, strlen(file));
                        strncat(request->arg, ",", 2);
                        strncat(request->arg, optarg, strlen(optarg));
                    }
                    else request->arg = strndup(file, strlen(file));
                    push(requests, (void *) request);
                    requested++;
                }
            }
            case 'W':
                tok = strtok_r(optarg, ",", &tmpstr);
                while((tok = strtok_r(NULL, ",", &tmpstr)) != NULL) //TODO

            case 'D':
            case 'r':
            case 'R':
            case 'd':
            case 't':
            case 'l':
            case 'u':
            case 'c':
            case 'p':;
        }
    }
    if (requested == MAX_CONSECUTIVE_REQUESTS) {
        cmdrequest last = *(cmdrequest *) requests->tail->data;
        printf("< Maximum consecutive requests limit reached. Sending the first %d request in order, until -%c %s\n",
               MAX_CONSECUTIVE_REQUESTS, last.opt, last.arg);
    }
    sendrequests(requests);
}

void sendrequests(queue_t *requests) {
    assert(requests != NULL);
    cmdrequest *request;
    int unused;
    char *tok, *tmpstr;
    while((request = (cmdrequest*)pop(requests))) { //TODO aggiungere gestione tempo tra una richiesta e l'altra
        switch (request->opt) {
            case 'f': { //TODO precedenza rispetto alle altre richieste
                CHECK_EQ_EXIT(sktname = strndup(request->arg, strlen(request->arg)), NULL, "strdup")
                struct timespec abstime;
                SYSCALL_EXIT(clock_gettime, unused, clock_gettime(CLOCK_REALTIME, &abstime), "clock_gettime", "")
                abstime.tv_sec += CONN_TIMEOUT_SEC;

                CHECK_EQ_EXIT(openConnection(sktname, RETRY_CONN_MSEC, abstime), -1, "openConnection")
                already_connected = true;
                break;
            }
            case 'w': {
                char *file = strtok_r(request->arg, ",", &tmpstr);  //file da scrivere
                char *storedir = strtok_r(NULL, ",", &tmpstr); //directory D
                CHECK_EQ_EXIT(openFile(file, O_CREATE | O_LOCK), -1, "openFile")
                CHECK_EQ_EXIT(writeFile(file, storedir), -1, "writeFile")
            }
            case 'W':
            case 'r':
            case 'R':
            case 'l':
            case 'u':
            case 'c':;
        }
    }
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
    SYSCALL_EXIT(stat,unused,stat(nomedir,&sb),"stat","");

    DIR *dir;
    CHECK_EQ_EXIT(dir = opendir(nomedir), NULL, "opendir")

    struct dirent *file;
    while((errno=0, file =readdir(dir)) != NULL && n > 0) {
        struct stat statbuf;
        char filename[MAX_PATH];
        int len1 = (int) strlen(nomedir);
        int len2 = (int) strlen(file->d_name);
        if ((len1 + len2 + 2) > MAX_PATH) {
            PRINT_ERROR("MAX_PATH troppo piccolo")
            exit(EXIT_FAILURE);
        }
        strncpy(filename, nomedir, MAX_PATH - 1);
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
