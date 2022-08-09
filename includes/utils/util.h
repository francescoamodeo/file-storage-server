#if !defined(_UTIL_H)
#define _UTIL_H

#include <stdarg.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <stdbool.h>

#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX 108
#endif
#if !defined(MAX_PATH)
#define MAX_PATH 2048 //compreso null terminator
#endif
#if !defined(MAX_USERNAME)
#define MAX_USERNAME 32
#endif
#if !defined(STRERROR_LEN)
#define STRERROR_LEN 4096
#endif

#define PRINT_PERROR(str)  \
    {   \
        fprintf(stderr,"< ERROR [%lu]: %s:%d: in function %s: ", pthread_self(), __FILE__, __LINE__, __func__); \
        perror(str);                   \
        fflush(stderr); \
    }

#define PRINT_ERROR(str)  \
    {   \
        fprintf(stderr,"< ERROR [%lu]: %s:%d: in function %s: %s\n", pthread_self(), __FILE__, __LINE__, __func__, str); \
        fflush(stderr); \
    }

#define CHECK_EQ_EXIT( X, val, str)	\
    if ((X)==val) {				    \
        if(errno == 0) PRINT_ERROR(str)  \
        else PRINT_PERROR(str)       \
        exit(EXIT_FAILURE);          \
    }

#define CHECK_NEQ_EXIT( X, val, str)	\
    if ((X)!=val) {				    \
        if(errno == 0) PRINT_ERROR(str)  \
        else PRINT_PERROR(str)       \
        exit(EXIT_FAILURE);          \
    }

#define SYSCALL_EXIT(r, sc, str)\
    if ((r=sc) == -1) {			\
	    PRINT_PERROR(str);		\
	    exit(EXIT_FAILURE);     \
    }

// prototipi necessari solo se compiliamo con c99 o c11
char *strndup(const char *s, size_t n);
char *realpath(const char *path, char *resolved_path);

/** 
 * \brief Controlla se la stringa passata come primo argomento e' un numero.
 * \return  0 ok  1 non e' un numbero   2 overflow/underflow
 */
static inline int isNumber(const char* s, long* n) {
  if (s==NULL) return 1;
  if (strlen(s)==0) return 1;
  char* e = NULL;
  errno=0;
  long val = strtol(s, &e, 10);
  if (errno == ERANGE) return 2;    // overflow/underflow
  if (e != NULL && *e == (char)0) {
    *n = val;
    return 0;   // successo 
  }
  return 1;   // non e' un numero
}

static inline char* strconcat(const char *s1, const char *s2) {
    if(s1==NULL && s2 != NULL) {
        errno = EINVAL;
        return NULL;
    }
    size_t len = strlen(s1) + strlen(s2) + 1;
    if (len > MAX_PATH){
        errno = ENAMETOOLONG;
        return NULL;
    }

    char *dest = calloc(len, sizeof(char));
    if(dest == NULL)
        return NULL;

    strncpy(dest, s1, strlen(s1));
    dest[strlen(s1)]='\0';

    return strncat(dest, s2, strlen(s2));
}

static inline char* strnconcat(const char* s1, ...){
    char* result;
    size_t len = strlen(s1);
    if (len >= MAX_PATH){
        errno = ENAMETOOLONG;
        return NULL;
    }
    result = strndup(s1, len);
    if (result == NULL) return NULL;
    va_list ap;
    va_start(ap,s1);
    char* s=NULL;

    //l'ultimo argomento passato sarà NULL per dire che la lista è terminata
    while ((s = va_arg(ap, char*))!=NULL) {
        char* temp = strconcat(result, s);
        if(temp == NULL){
            free(result);
            return NULL;
        }
        free(result);
        result=temp;
    }

    va_end(ap);
    return result;
}

static inline int mkdirs(char *file_path) {
    if (file_path == NULL){
        errno = EINVAL;
        return -1;
    }
    char *dir_path = (char *) malloc(strlen(file_path) + 1);
    if (dir_path == NULL) return -1;

    char *next_sep = strchr(file_path, '/');
    if(next_sep - file_path == 0) //path assoluto
        next_sep = strchr(next_sep + 1, '/');
    while (next_sep != NULL) {
        long dir_path_len = next_sep - file_path;
        memcpy(dir_path, file_path, dir_path_len);
        dir_path[dir_path_len] = '\0';

        if (mkdir(dir_path, S_IRWXU) == -1 && errno != EEXIST) return -1;

        next_sep = strchr(next_sep + 1, '/');
    }
    free(dir_path);
    if (errno == EEXIST) errno = 0;
    return 0;
}

static inline bool checkfile_ext(char *filename, char *extension) {

    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename)
        return false;
    if(strcmp(dot+1, extension) != 0)
        return false;
    return true;
}

/* msleep(): Sleep for the requested number of milliseconds. */
static inline int msleep(long msec) {
    struct timespec ts;
    int res;

    if (msec < 0) {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);

    return res;
}

static inline int max(int cnt,...) {
    va_list ap;
    int i, current, maximum;
    va_start(ap, cnt);
    maximum = 0;
    for (i = 0; i < cnt; i++) {
        current = va_arg(ap, int);
        if (current > maximum)
            maximum = current;
    }
    va_end(ap);
    return maximum;
}

#define TRUNC_NEWLINE(str) \
    {                      \
        size_t length = strlen(str);                   \
        if ((length) > 0 && (str)[(length)-1] == '\n') { \
            (str)[--(length)] = '\0'; \
        }                               \
    }

#define MALLOC(obj, size, type)   \
    {   \
        (obj) = malloc((size) * sizeof(type));  \
        if ((obj) == NULL) {        \
            PRINT_PERROR("malloc") \
            exit(EXIT_FAILURE);   \
        }                         \
    }


#define CALLOC(obj, size, type) \
    {   \
        (obj) = calloc(size, sizeof(type));   \
        if ((obj) == NULL) {    \
            PRINT_PERROR("calloc")                    \
            exit(EXIT_FAILURE);   \
        }                         \
    }

    //usate dal threadpool
#define LOCK(l)      if (pthread_mutex_lock(l)!=0)        { \
    fprintf(stderr, "ERRORE FATALE lock\n");		    \
    pthread_exit((void*)EXIT_FAILURE);			    \
  }
#define LOCK_RETURN(l, r)  if (pthread_mutex_lock(l)!=0)        {	\
    fprintf(stderr, "ERRORE FATALE lock\n");				\
    return r;								\
  }

#define UNLOCK(l)    if (pthread_mutex_unlock(l)!=0)      {	    \
    fprintf(stderr, "ERRORE FATALE unlock\n");			    \
    pthread_exit((void*)EXIT_FAILURE);				    \
  }
#define UNLOCK_RETURN(l,r)    if (pthread_mutex_unlock(l)!=0)      {	\
    fprintf(stderr, "ERRORE FATALE unlock\n");				\
    return r;								\
  }

#endif /* _UTIL_H */
