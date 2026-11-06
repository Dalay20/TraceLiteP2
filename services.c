#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "span.h"
#include "services.h"

static void trabajar_ms(int ms) { usleep(ms * 1000); }

static int contexto_parsear(const char *linea, char *trace, char *padre,
                            int *numero) {
    char copia[128];
    char extra;
    if (snprintf(copia, sizeof(copia), "%s", linea) >= (int)sizeof(copia))
        return -1;
    copia[strcspn(copia, "\n")] = '\0';
    if (sscanf(copia, "%16[0-9a-fA-F]|%16[0-9a-fA-F]|%d%c",
               trace, padre, numero, &extra) != 3 ||
        *numero < 1 || *numero > 4 ||
        !id_valido(trace) || !id_valido(padre)) {
        return -1;
    }
    return 0;
}

static int respuesta_valida(const char *respuesta) {
    return strcmp(respuesta, "OK\n") == 0 || strcmp(respuesta, "ERROR\n") == 0;
}

/* ---------- Servicio A: api (span raíz) ---------- */
int servicio_a(int w_ai, int r_ia, int w_spans, int n_solicitudes) {
    srand(getpid() ^ time(NULL));          /* semilla distinta por proceso */
    FILE *in = fdopen(r_ia, "r");
    if (in == NULL) {
        perror("fdopen api");
        close(w_ai);
        close(r_ia);
        close(w_spans);
        return -1;
    }

    for (int i = 1; i <= n_solicitudes; i++) {
        Span s;
        memset(&s, 0, sizeof(s));
        gen_id(s.trace_id);                /* nace la traza */
        gen_id(s.span_id);
        s.parent_span_id[0] = '\0';        /* raíz: sin padre */
        snprintf(s.servicio, NAME_LEN, "api");
        snprintf(s.operacion, NAME_LEN, "recibir_solicitud");

        s.inicio_ms = ahora_ms();
        trabajar_ms(20 + rand() % 30);

        /* Propagación de contexto: trace_id + mi span_id (padre de inventario) */
        char msg[128];
        int len = snprintf(msg, sizeof(msg), "%s|%s|%d\n", s.trace_id, s.span_id, i);
        if (len < 0 || (size_t)len >= sizeof(msg) ||
            write_all(w_ai, msg, (size_t)len) == -1) {
            perror("write contexto api");
            fclose(in);
            close(w_ai);
            close(w_spans);
            return -1;
        }

        /* Espero la respuesta del flujo inventario-pagos */
        char resp[16] = "ERROR";
        if (fgets(resp, sizeof(resp), in) == NULL) {
            if (ferror(in)) perror("fgets api");
            strcpy(resp, "ERROR");
        } else if (!respuesta_valida(resp)) {
            fprintf(stderr, "[api] respuesta inválida\n");
            strcpy(resp, "ERROR\n");
        }

        trabajar_ms(10 + rand() % 20);
        s.fin_ms = ahora_ms();
        s.duracion_ms = s.fin_ms - s.inicio_ms;
        snprintf(s.estado, sizeof(s.estado), "%s",
                 strncmp(resp, "ERROR", 5) == 0 ? "ERROR" : "OK");

        if (emitir_span(w_spans, &s) == -1) {
            perror("write span api");
            fclose(in);
            close(w_ai);
            close(w_spans);
            return -1;
        }
    }

    close(w_ai);       /* inventario recibe EOF y termina */
    fclose(in);
    if (close(w_spans) == -1) {
        perror("close spans api");
        return -1;
    }
    return 0;
}

/* ---------- Servicio inventario ---------- */
int servicio_inventario(int r_ai, int w_ib, int r_bi, int w_ia,
                        int w_spans) {
    FILE *in_a = fdopen(r_ai, "r");
    FILE *in_b = fdopen(r_bi, "r");
    int correcto = 1;
    if (in_a == NULL || in_b == NULL) {
        perror("fdopen inventario");
        if (in_a != NULL) fclose(in_a); else close(r_ai);
        if (in_b != NULL) fclose(in_b); else close(r_bi);
        close(w_ib);
        close(w_ia);
        close(w_spans);
        return -1;
    }
    char linea[128];
    int stock = 3;

    while (fgets(linea, sizeof(linea), in_a) != NULL) {
        char trace[ID_LEN], padre[ID_LEN];
        int num;
        if (contexto_parsear(linea, trace, padre, &num) == -1) {
            fprintf(stderr, "[inventario] contexto inválido\n");
            continue;
        }

        Span s;
        memset(&s, 0, sizeof(s));
        snprintf(s.trace_id, ID_LEN, "%s", trace);
        snprintf(s.parent_span_id, ID_LEN, "%s", padre);
        gen_id(s.span_id);
        snprintf(s.servicio, NAME_LEN, "inventario");
        snprintf(s.operacion, NAME_LEN, "verificar_stock");

        s.inicio_ms = ahora_ms();
        trabajar_ms(15 + rand() % 20);
        int disponible = stock > 0;
        if (disponible) stock--;
        s.fin_ms = ahora_ms();
        s.duracion_ms = s.fin_ms - s.inicio_ms;
        snprintf(s.estado, sizeof(s.estado), "%s", disponible ? "OK" : "ERROR");
        if (emitir_span(w_spans, &s) == -1) {
            perror("write span inventario");
            fclose(in_a);
            fclose(in_b);
            close(w_ib);
            close(w_ia);
            close(w_spans);
            return -1;
        }

        if (!disponible) {
            if (write_all(w_ia, "ERROR\n", 6) == -1) {
                perror("write respuesta inventario");
                correcto = 0;
                break;
            }
            continue;
        }

        int len = snprintf(linea, sizeof(linea), "%s|%s|%d\n",
                           s.trace_id, s.span_id, num);
        if (len < 0 || (size_t)len >= sizeof(linea) ||
            write_all(w_ib, linea, (size_t)len) == -1) {
            perror("write contexto inventario");
            correcto = 0;
            break;
        }

        char respuesta[16] = "ERROR";
        if (fgets(respuesta, sizeof(respuesta), in_b) == NULL) {
            if (ferror(in_b)) perror("fgets inventario");
            strcpy(respuesta, "ERROR");
        } else if (!respuesta_valida(respuesta)) {
            fprintf(stderr, "[inventario] respuesta de pagos inválida\n");
            strcpy(respuesta, "ERROR\n");
        }
        const char *resultado = strcmp(respuesta, "ERROR\n") == 0
                                ? "ERROR\n" : "OK\n";
        if (write_all(w_ia, resultado, strlen(resultado)) == -1) {
            perror("write respuesta inventario");
            correcto = 0;
            break;
        }
    }

    close(w_ib);
    fclose(in_a);
    fclose(in_b);
    close(w_ia);
    if (close(w_spans) == -1) {
        perror("close spans inventario");
        return -1;
    }
    return correcto ? 0 : -1;
}

/* ---------- Servicio B: pagos (span hijo de inventario) ---------- */
int servicio_b(int r_ib, int w_bi, int w_spans) {
    srand(getpid() ^ time(NULL));
    FILE *in = fdopen(r_ib, "r");
    if (in == NULL) {
        perror("fdopen pagos");
        close(r_ib);
        close(w_bi);
        close(w_spans);
        return -1;
    }
    char linea[128];

    while (fgets(linea, sizeof(linea), in) != NULL) {
        char trace[ID_LEN], padre[ID_LEN];
        int num;
        if (contexto_parsear(linea, trace, padre, &num) == -1) {
            fprintf(stderr, "[pagos] contexto inválido\n");
            continue;
        }

        Span s;
        memset(&s, 0, sizeof(s));
        snprintf(s.trace_id, ID_LEN, "%s", trace);        /* mismo Trace ID */
        snprintf(s.parent_span_id, ID_LEN, "%s", padre);  /* padre = inventario */
        gen_id(s.span_id);                                /* mi propio Span ID */
        snprintf(s.servicio, NAME_LEN, "pagos");
        snprintf(s.operacion, NAME_LEN, "cobrar");

        s.inicio_ms = ahora_ms();
        trabajar_ms(50 + rand() % 100);

        /* Anomalías: retraso aleatorio y/o error */
        if (rand() % 100 < 25) trabajar_ms(300 + rand() % 200);
        int error = (num == 3) || (rand() % 100 < 10);    /* la 3ª siempre falla */

        s.fin_ms = ahora_ms();
        s.duracion_ms = s.fin_ms - s.inicio_ms;
        snprintf(s.estado, sizeof(s.estado), "%s", error ? "ERROR" : "OK");

        if (emitir_span(w_spans, &s) == -1) {
            perror("write span pagos");
            fclose(in);
            close(w_bi);
            close(w_spans);
            return -1;
        }
        const char *resultado = error ? "ERROR\n" : "OK\n";
        if (write_all(w_bi, resultado, strlen(resultado)) == -1) {
            perror("write respuesta pagos");
            fclose(in);
            close(w_bi);
            close(w_spans);
            return -1;
        }
    }

    fclose(in);
    close(w_bi);
    if (close(w_spans) == -1) {
        perror("close spans pagos");
        return -1;
    }
    return 0;
}