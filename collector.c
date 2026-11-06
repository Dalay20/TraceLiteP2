#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "span.h"

#define MAX_SPANS 256

static Span spans[MAX_SPANS];
static int n = 0;

/* Convierte una línea "a|b|c|..." en un Span.
   Usamos strsep porque el parent_span_id puede venir vacío ("||"),
   y sscanf/strtok se saltarían ese campo. */
static int span_parsear(char *linea, Span *s) {
    char *campos[9];
    char *p = linea;

    linea[strcspn(linea, "\n")] = '\0';
    for (int i = 0; i < 9; i++) {
        campos[i] = strsep(&p, "|");
        if (campos[i] == NULL) return -1;   /* línea incompleta */
    }

    snprintf(s->trace_id, ID_LEN, "%s", campos[0]);
    snprintf(s->span_id, ID_LEN, "%s", campos[1]);
    snprintf(s->parent_span_id, ID_LEN, "%s", campos[2]);
    snprintf(s->servicio, NAME_LEN, "%s", campos[3]);
    snprintf(s->operacion, NAME_LEN, "%s", campos[4]);
    s->inicio_ms = atof(campos[5]);
    s->fin_ms = atof(campos[6]);
    s->duracion_ms = atof(campos[7]);
    snprintf(s->estado, sizeof(s->estado), "%s", campos[8]);
    return 0;
}

/* Imprime los hijos de 'padre' dentro de la traza, con indentación */
static void imprimir_arbol(const char *trace, const char *padre, int nivel) {
    for (int i = 0; i < n; i++) {
        if (strcmp(spans[i].trace_id, trace) == 0 &&
            strcmp(spans[i].parent_span_id, padre) == 0) {
            char etiqueta[96];
            snprintf(etiqueta, sizeof(etiqueta), "%*s└─ [%s] %s",
                    nivel * 4, "", spans[i].servicio, spans[i].operacion);
            printf("%-50s %8.1f ms  %-5s (span %s, padre: %s)\n",
                etiqueta, spans[i].duracion_ms, spans[i].estado,
                spans[i].span_id,
                spans[i].parent_span_id[0] ? spans[i].parent_span_id : "-");
            imprimir_arbol(trace, spans[i].span_id, nivel + 1);
        }
    }
}

int main(void) {
    char linea[512];

    /* 1. Recolectar hasta EOF */
    while (fgets(linea, sizeof(linea), stdin) != NULL) {
        if (n >= MAX_SPANS) break;
        if (span_parsear(linea, &spans[n]) == 0) n++;
    }

    /* 2. Mostrar agrupado por Trace ID */
    printf("\n===== TraceLite: %d spans recibidos =====\n", n);
    for (int i = 0; i < n; i++) {
        int repetido = 0;                      /* ¿ya imprimí esta traza? */
        for (int j = 0; j < i; j++)
            if (strcmp(spans[j].trace_id, spans[i].trace_id) == 0) { repetido = 1; break; }
        if (repetido) continue;

        printf("\nTrace %s\n", spans[i].trace_id);
        imprimir_arbol(spans[i].trace_id, "", 0);   /* "" = span raíz */
    }
    return 0;
}