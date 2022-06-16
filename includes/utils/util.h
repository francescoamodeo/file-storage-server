#if !defined(_UTIL_H)
#define _UTIL_H


#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>

#if !defined(BUFSIZE)
#define BUFSIZE 256
#endif

#if !defined(EXTRA_LEN_PRINT_ERROR)
#define EXTRA_LEN_PRINT_ERROR   512
#endif

#define MAX_FILE_NAME 128
#define MAX_CONN 32
#define MAX_ARGV 1024
#define MAX_PATH (MAX_ARGV-MAX_FILE_NAME)

#define SYSCALL_EXIT(name, r, sc, str, ...)	\
    if ((r=sc) == -1) {				\
	perror(#name);				\
	int errno_copy = errno;			\
	print_error(str, __VA_ARGS__);		\
	exit(errno_copy);			\
    }

#define SYSCALL_PRINT(name, r, sc, str, ...)	\
    if ((r=sc) == -1) {				\
	perror(#name);				\
	int errno_copy = errno;			\
	print_error(str, __VA_ARGS__);		\
	errno = errno_copy;			\
    }

#define SYSCALL_RETURN(name, r, sc, str, ...)	\
    if ((r=sc) == -1) {				\
	perror(#name);				\
	int errno_copy = errno;			\
	print_error(str, __VA_ARGS__);		\
	errno = errno_copy;			\
	return r;                               \
    }

#define PRINT_PERROR(str)  \
    {   \
        fprintf(stderr,"ERROR: %s:%d: in function %s: ", __FILE__, __LINE__, __func__); \
        perror(str);                   \
        fflush(stderr); \
    }

#define PRINT_ERROR(str)  \
    {   \
        fprintf(stderr,"ERROR: %s:%d: in function %s: %s\n", __FILE__, __LINE__, __func__, str); \
        fflush(stderr); \
    }

#define CHECK_ERROR_EXIT(X, val)    \
   if ((X)==val) {				    \
	    int errno_copy = errno;     \
        if(errno_copy == 0)         \
            exit(EXIT_FAILURE);     \
        else                        \
            exit(errno_copy);       \
    }

#define CHECK_EQ_EXIT( X, val, str)	\
    if ((X)==val) {				    \
	    int errno_copy = errno;     \
        if(errno_copy == 0) {       \
            PRINT_ERROR(str)        \
            exit(EXIT_FAILURE);     \
        }                           \
        else {                      \
            PRINT_PERROR(str)       \
            exit(errno_copy);       \
        }                           \
    }

// prototipo necessario solo se compiliamo con c99 o c11
char *strndup(const char *s, size_t n);

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
        if ((obj) == NULL) {          \
            fprintf(stderr, "LINE: %d FILE: %s --> ", __LINE__, __FILE__); \
            perror("malloc");   \
            fflush(stderr); \
            exit(EXIT_FAILURE);   \
        }                         \
    }


#define CALLOC(obj, size, type) \
    {   \
        (obj) = calloc(size, sizeof(type));   \
        if ((obj) == NULL) {    \
            fprintf(stderr, "LINE: %d FILE: %s --> ", __LINE__, __FILE__); \
            perror("calloc");   \
            fflush(stderr); \
            exit(EXIT_FAILURE);   \
        }                         \
    }

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
#define WAIT(c,l)    if (pthread_cond_wait(c,l)!=0)       {	    \
    fprintf(stderr, "ERRORE FATALE wait\n");			    \
    pthread_exit((void*)EXIT_FAILURE);				    \
  }
/* ATTENZIONE: t e' un tempo assoluto! */
#define TWAIT(c,l,t) {							\
    int r=0;								\
    if ((r=pthread_cond_timedwait(c,l,t))!=0 && r!=ETIMEDOUT) {		\
      fprintf(stderr, "ERRORE FATALE timed wait\n");			\
      pthread_exit((void*)EXIT_FAILURE);				\
    }									\
  }
#define SIGNAL(c)    if (pthread_cond_signal(c)!=0)       {		\
    fprintf(stderr, "ERRORE FATALE signal\n");				\
    pthread_exit((void*)EXIT_FAILURE);					\
  }
#define BCAST(c)     if (pthread_cond_broadcast(c)!=0)    {		\
    fprintf(stderr, "ERRORE FATALE broadcast\n");			\
    pthread_exit((void*)EXIT_FAILURE);					\
  }
static inline int TRYLOCK(pthread_mutex_t* l) {
  int r=0;		
  if ((r=pthread_mutex_trylock(l))!=0 && r!=EBUSY) {		    
    fprintf(stderr, "ERRORE FATALE unlock\n");		    
    pthread_exit((void*)EXIT_FAILURE);			    
  }								    
  return r;	
}

#endif /* _UTIL_H */