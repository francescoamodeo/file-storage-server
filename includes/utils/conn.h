#if !defined(CONN_H)
#define CONN_H

#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#if !defined(BUFSIZE)
#define BUFSIZE 256
#endif
#if !defined(MAXBACKLOG)
#define MAXBACKLOG 32
#endif
#if !defined(CONN_TIMEOUT_SEC)
#define CONN_TIMEOUT_SEC 10
#endif
#if !defined(RETRY_CONN_MSEC)
#define RETRY_CONN_MSEC 3000
#endif

/** Evita letture parziali
 *
 *   \retval -1   errore (errno settato)
 *   \retval  0   se durante la lettura da fd leggo EOF
 *   \retval size se termina con successo
 */
static inline int readn(long fd, void *buf, size_t size) {
    size_t left = size;
    int r;
    unsigned char *bufptr = buf;
    while (left > 0) {
        if ((r = (int) read((int) fd, bufptr, left)) == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return 0;   // EOF
        left -= r;
        bufptr += r;
    }
    return (int) size;
}

/** Evita scritture parziali
 *
 *   \retval -1   errore (errno settato)
 *   \retval  0   se durante la scrittura la write ritorna 0
 *   \retval  1   se la scrittura termina con successo
 */
static inline int writen(long fd, void *buf, size_t size) {
    size_t left = size;
    int r;
    unsigned char *bufptr = buf;
    while (left > 0) {
        if ((r = (int) write((int) fd, bufptr, left)) == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return 0;
        left -= r;
        bufptr += r;
    }
    return 1;
}

#endif /* CONN_H */
