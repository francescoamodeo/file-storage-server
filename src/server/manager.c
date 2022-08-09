#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <stdlib.h>

#include <manager.h>
#include <worker.h>
#include <storage.h>
#include <threadpool.h>

configArgs confargs = {"", "", 0, 0, 0, 0};
storage_t *storage = NULL;
threadpool_t *tpool = NULL;
FILE *logfile = NULL;
pthread_mutex_t logfile_mutex = PTHREAD_MUTEX_INITIALIZER;
int listenfd = -1;
int fdpipe[2], signalpipe[2];
pthread_t signal_thread = 0;
bool signal_thread_activated = false;
bool shutdown_ = false;
bool shutdown_now = false;
bool logfile_opened = false;

void cleanup();
void signalhandler(void *arg);
int parse_configline(char* line, configArgs* cargs);
int updatemax(fd_set set, int fdmax);

int main(int argc, char *argv[]){

    CHECK_EQ_EXIT(atexit(cleanup), -1, "atexit")
    if(argc < 3){
        fprintf(stderr, "< Usage: %s -f <configfile>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct sigaction sig_action;
    sigset_t sigset;
    memset(&sig_action, 0, sizeof(sig_action));
    sig_action.sa_handler = SIG_IGN;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGQUIT);
    signal(SIGPIPE, SIG_IGN);
    sig_action.sa_mask = sigset;
    CHECK_EQ_EXIT(pipe(signalpipe), -1, "create signalpipe")
    CHECK_NEQ_EXIT(pthread_sigmask(SIG_BLOCK, &sigset, NULL), 0, "pthread_sigmask")

    //lettura configfile
    int opt;
    while ((opt = getopt(argc, argv, ":f:")) != -1) {
        switch (opt) {
            case 'f': {
                if (parse_config(optarg, &confargs) == -1)
                    exit(EXIT_FAILURE);
                break;
            }
            case ':': {
                PRINT_ERROR("-f option requires an argument")
                exit(EXIT_FAILURE);
            }
            default: {
                PRINT_ERROR("Unrecognised option")
                exit(EXIT_FAILURE);
            }
        }
    }

    CHECK_EQ_EXIT(mkdirs(confargs.logfile),  -1, "mkdir log dir")
    CHECK_EQ_EXIT(logfile = fopen(confargs.logfile, "w"), NULL, "fopen logfile")
    logfile_opened = true;
    CHECK_NEQ_EXIT(pthread_mutex_init(&logfile_mutex, NULL), 0, "logfile mutex init")

    CHECK_EQ_EXIT(pipe(fdpipe), -1, "create fdpipe")

    //creazione storage
    CHECK_EQ_EXIT(storage = fs_init(confargs.filelimit, confargs.storagecapacity, 0), NULL, "fs_init")
    CHECK_EQ_EXIT(tpool = createThreadPool((int)confargs.nworkers, PENDING_SIZE), NULL,"create threadpool")

    //creazione socket
    __attribute__((unused)) int unused;
    unlink(confargs.sktname);
    SYSCALL_EXIT(listenfd, socket(AF_UNIX, SOCK_STREAM, 0), "socket")
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, confargs.sktname, strlen(confargs.sktname));
    SYSCALL_EXIT(unused, bind(listenfd, (struct sockaddr*)&sa, sizeof(sa)), "bind")
    SYSCALL_EXIT(unused, listen(listenfd, MAXBACKLOG), "listen")

    fd_set set, rset;
    FD_ZERO(&set);
    FD_SET(listenfd, &set);
    FD_SET(fdpipe[0], &set);
    FD_SET(signalpipe[0], &set);
    //Adesso che ho assegnato la pipe faccio partire il thread dei segnali
    CHECK_NEQ_EXIT(pthread_create(&signal_thread, NULL, (void *(*)(void *))signalhandler, (void*)&sigset), 0, "signal thread create")
    signal_thread_activated = true;

    int connected = 0;
    int fd_max, fd;
    fd_max = max(3, listenfd, fdpipe[0], signalpipe[0]);
    while(!shutdown_now && (!shutdown_ || connected)) {
        rset = set;

        if (select(fd_max + 1, &rset, NULL, NULL, NULL) == -1) {
            if (errno == EINTR) continue; //segnali terminazione
            PRINT_PERROR("select")
            exit(EXIT_FAILURE);
        }

        for (fd = 0; fd <= fd_max; fd++) {

            if (shutdown_now) {
                CHECK_EQ_EXIT(destroyThreadPool(tpool, 1), -1, "threadpool destroy")
                tpool = NULL;
                break; //esco immediatamente
            }

            if (FD_ISSET(fd, &rset)) {
                if (fd == listenfd && !shutdown_) { //nuova richiesta di connessione

                    int newclient;
                    SYSCALL_EXIT(newclient, accept(listenfd, NULL, NULL), "accept")
                    connected++;
                    FD_SET(newclient, &set); //aggiungo il fd al set generale
                    if (newclient > fd_max) fd_max = newclient;
                    if (log_operation("CONNECT", newclient, 0, 0, 0, 0, "OK") == -1)
                        exit(EXIT_FAILURE);

                } else if (fd == signalpipe[0]) { //segnali terminazione

                    //tolgo la pipe dall'insieme
                    FD_CLR(fd, &set);
                    if (shutdown_now) break;
                    if (shutdown_) { //chiudo il listenfd e continuo a servire richieste finché ci sono ancora client connessi
                        FD_CLR(listenfd, &set);
                        close(listenfd);
                        if (listenfd == fd_max) fd_max = updatemax(set, fd_max);
                    }
                } else if (fd == fdpipe[0]) {

                    int completedclient;
                    CHECK_EQ_EXIT(read(fdpipe[0], &completedclient, sizeof(int)), -1, "read fdpipe")

                    if (completedclient >= 0) {
                        FD_SET(completedclient, &set);
                        if (completedclient > fd_max) fd_max = completedclient;
                    } else {
                        int client = completedclient *(-1);
                        FD_CLR(client, &set);
                        close(client);
                        connected--;
                        if (client == fd_max) fd_max = updatemax(set, fd_max);
                        if (log_operation("DISCONNECT", client, 0, 0, 0, 0, "OK") == -1)
                            exit(EXIT_FAILURE);
                    }
                } else { //client
                    int ret;
                    int *client;
                    MALLOC(client, 1, int)
                    *client = fd;
                    CHECK_EQ_EXIT((ret = addToThreadPool(tpool, (void (*)(void *)) requesthandler, client)), -1, "add threadpool")

                    if (ret == 1) { //in questo caso il threadpool ha ritornato coda piena
                        FD_CLR(fd, &set);
                        close(fd);
                        connected--;
                        if (fd == fd_max) fd_max = updatemax(set, fd_max);
                        if (log_operation("DISCONNECT", fd, 0, 0, 0, 0, "OK") == -1)
                            exit(EXIT_FAILURE);
                        continue;
                    }
                    //togliamo il fd dal set perchè adesso viene gestito dal threadpool
                    FD_CLR(fd, &set);
                    if (fd == fd_max) fd_max = updatemax(set, fd_max);
                }
            }
        }
    }
    if (shutdown_now){
        //chiudo tutti i fd attivi
        for (fd = 0; fd <= fd_max; fd++) {
            if (FD_ISSET(fd, &set)) close(fd);
        }
    }
    return 0;
}

void cleanup(){
    if (confargs.sktname && strcmp(confargs.sktname, "") != 0){
        unlink(confargs.sktname);
        free(confargs.sktname);
    }
    if (confargs.logfile) free(confargs.logfile);

    if (tpool) destroyThreadPool(tpool, 0);
    if (storage) {
        fs_stats(storage);
        log_operation("MAXFILES", 0, 0, storage->max_files_number, 0, 0, "OK");
        log_operation("MAXCAPACITY", 0, 0, storage->max_occupied_memory, 0, 0, "OK");

        //chiudo i fd di tutti i client che attendono una lock
        for (elem_t *elem = list_gethead(storage->clients_awaiting);
             elem != NULL;
             elem = list_getnext(storage->clients_awaiting, elem)) {
            msg_t *lockrequest = elem->data;
            close(lockrequest->header->arg);
        }
        fs_destroy(storage);
    }
    if (listenfd != -1) close(listenfd);
    if (signal_thread_activated) pthread_join(signal_thread, NULL);
    if (logfile_opened) {
        fclose(logfile);
        pthread_mutex_destroy(&logfile_mutex);
    }
}

void signalhandler(void *arg){
    sigset_t* sigset = (sigset_t*) arg;
    int signal;
    CHECK_NEQ_EXIT(sigwait(sigset, &signal), 0, "sigwait")
    switch (signal) {
        case SIGINT:
        case SIGQUIT:
            shutdown_now = true;
            break;
        case SIGHUP:
            shutdown_ = true;
            break;
        default:
            break;
    }
    CHECK_EQ_EXIT(write(signalpipe[1], &signal, sizeof(int)), -1, "signalpipe write")
}

//ritorno l'inidce massimo dei descrittori attivi
int updatemax(fd_set set, int fdmax){
    for (int i = fdmax-1; i >= 0 ; --i) {
        if(FD_ISSET(i, &set))
            return i;
    }
    return -1;
}

int parse_config(const char* config_filename, configArgs* cargs){

    if (!config_filename || !cargs) {
        PRINT_ERROR("Invalid argument")
        return -1;
    }
    if (strlen(config_filename) >= MAX_PATH) {
        PRINT_ERROR("config filename too long")
        return -1;
    }

    FILE *ptr = NULL;
    if ((ptr = fopen(config_filename, "rb")) == NULL){
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
        TRUNC_NEWLINE(tok)
        if (strlen(tok) >= MAX_PATH) {
            PRINT_ERROR("Socket name too long")
            return -1;
        }
        cargs->sktname = strndup(tok, strlen(tok));
        if(cargs->sktname == NULL){
            PRINT_PERROR("strndup")
            return -1;
        }
        return 0;
    }

    //parsing nome file di log
    if (strcmp(tok, "LOG_FILE") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            PRINT_ERROR("Missing log file name argument")
            return -1;
        }
        if (strlen(tok) >= MAX_PATH) {
            PRINT_ERROR("Socket name too long")
            return -1;
        }
        TRUNC_NEWLINE(tok)
        cargs->logfile = strndup(tok, strlen(tok));
        if(cargs->logfile == NULL){
            PRINT_PERROR("strndup")
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
            PRINT_ERROR("Invalid storage capacity argument")
            return -1;
        }
        cargs->storagecapacity = (int)value;
        return 0;
    }

    //parsing limite file
    if (strcmp(tok, "FILE_LIMIT") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            PRINT_ERROR("Missing file limit argument")
            return -1;
        }
        TRUNC_NEWLINE(tok)
        if (isNumber(tok, &value) != 0 || value <= 0) {
            PRINT_ERROR("Invalid file limit argument")
            return -1;
        }
        cargs->filelimit = (int)value;
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
            PRINT_ERROR("Invalid thread workers argument")
            return -1;
        }
        cargs->nworkers = (int)value;
        return 0;
    }
    
    //parsing politica di rimpiazzamento
    if (strcmp(tok, "REPLACE_MODE") == 0) {
        tok = strtok_r(NULL, "=", &tmpstr);
        if (!tok || *tok == '\n') {
            PRINT_ERROR("Missing replacement mode argument")
            return -1;
        }
        TRUNC_NEWLINE(tok)
        if (strcmp(tok, "FIFO") == 0){
            cargs->replace_mode = 0;
            return 0;
        }
        if (strcmp(tok, "LRU") == 0) {
            PRINT_ERROR("Sorry, LRU not implemented yet")
            return -1;
        }
        PRINT_ERROR("Invalid replacement mode argument")
        return -1;
    }
    PRINT_ERROR("Unrecognized configuration option")
    return -1;
}

int log_operation(const char* OP, int IDCLIENT, size_t DELETED_BYTES, size_t ADDED_BYTES, size_t SENT_BYTES,
                  const char* OBJECT_FILE, const char* OUTCOME){

    if(pthread_mutex_lock(&logfile_mutex) != 0) return -1;
    if (fprintf(logfile,"/THREAD/=%lu /OP/=%s /IDCLIENT/=%d /DELETED_BYTES/=%zu /ADDED_BYTES/=%zu /SENT_BYTES/=%zu /OBJECT_FILE/=%s /OUTCOME/=%s\n",
                pthread_self(),OP, IDCLIENT, DELETED_BYTES, ADDED_BYTES, SENT_BYTES, OBJECT_FILE, OUTCOME) < 0) return -1;
    fflush(logfile);
    if(pthread_mutex_unlock(&logfile_mutex) != 0) return -1;

    return 0;
}