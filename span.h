#ifndef SPAN_H
#define SPAN_H

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

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

/* ID aleatorio de 16 caracteres hex */
static inline void gen_id(char *out) {
    snprintf(out, ID_LEN, "%08x%08x", (unsigned)rand(), (unsigned)rand());
}

/* Envía un span al collector. Una sola write() por línea (< PIPE_BUF),
   así los mensajes de A y B no se mezclan. */
static inline void emitir_span(int fd, const Span *s) {
    char buf[512];
    int len = span_serializar(s, buf, sizeof(buf));
    if (len > 0) write(fd, buf, len);
}

#endif