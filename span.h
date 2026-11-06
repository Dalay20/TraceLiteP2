#ifndef SPAN_H
#define SPAN_H

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define ID_LEN 17      /* 16 caracteres hex + '\0' */
#define NAME_LEN 32

typedef struct {
    char trace_id[ID_LEN];
    char span_id[ID_LEN];
    char parent_span_id[ID_LEN];   /* "" si es el span raíz */
    char servicio[NAME_LEN];
    char operacion[NAME_LEN];
    double inicio_ms;
    double fin_ms;
    double duracion_ms;
    char estado[8];                /* "OK" o "ERROR" */
} Span;

/* Tiempo monotónico en milisegundos */
static inline double ahora_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Span -> línea de texto (para enviar por la pipe) */
static inline int span_serializar(const Span *s, char *buf, size_t n) {
    return snprintf(buf, n, "%s|%s|%s|%s|%s|%.3f|%.3f|%.3f|%s\n",
        s->trace_id, s->span_id, s->parent_span_id,
        s->servicio, s->operacion,
        s->inicio_ms, s->fin_ms, s->duracion_ms, s->estado);
}

static inline int write_all(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len > 0) {
        ssize_t written = write(fd, p, len);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        p += written;
        len -= (size_t)written;
    }
    return 0;
}

static inline int id_valido(const char *id) {
    if (strlen(id) != ID_LEN - 1) return 0;
    for (size_t i = 0; i < ID_LEN - 1; i++) {
        if (!((id[i] >= '0' && id[i] <= '9') ||
              (id[i] >= 'a' && id[i] <= 'f') ||
              (id[i] >= 'A' && id[i] <= 'F'))) return 0;
    }
    return 1;
}

/* ID aleatorio de 16 caracteres hex */
static inline void gen_id(char *out) {
    snprintf(out, ID_LEN, "%08x%08x", (unsigned)rand(), (unsigned)rand());
}

/* Envía un span al collector. Una sola write() por línea (< PIPE_BUF),
   así los mensajes de A y B no se mezclan. */
static inline int emitir_span(int fd, const Span *s) {
    char buf[512];
    int len = span_serializar(s, buf, sizeof(buf));
    if (len <= 0 || (size_t)len >= sizeof(buf)) {
        errno = EOVERFLOW;
        return -1;
    }
    return write_all(fd, buf, (size_t)len);
}

#endif