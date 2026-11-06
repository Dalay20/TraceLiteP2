#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "span.h"
#include "services.h"

static void trabajar_ms(int ms) { usleep(ms * 1000); }

/* ---------- Servicio A: api (span raíz) ---------- */
void servicio_a(int w_ab, int r_ba, int w_spans, int n_solicitudes) {
    srand(getpid() ^ time(NULL));          /* semilla distinta por proceso */
    FILE *in = fdopen(r_ba, "r");

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

        /* Propagación de contexto: trace_id + MI span_id (será el padre de B) */
        char msg[128];
        int len = snprintf(msg, sizeof(msg), "%s|%s|%d\n", s.trace_id, s.span_id, i);
        write(w_ab, msg, len);

        /* Espero la respuesta de B */
        char resp[16] = "ERROR";
        if (fgets(resp, sizeof(resp), in) == NULL) strcpy(resp, "ERROR");

        trabajar_ms(10 + rand() % 20);
        s.fin_ms = ahora_ms();
        s.duracion_ms = s.fin_ms - s.inicio_ms;
        snprintf(s.estado, sizeof(s.estado), "%s",
                 strncmp(resp, "ERROR", 5) == 0 ? "ERROR" : "OK");

        emitir_span(w_spans, &s);
    }

    close(w_ab);       /* B recibe EOF y termina */
    fclose(in);
    close(w_spans);
}

/* ---------- Servicio B: pagos (span hijo) ---------- */
void servicio_b(int r_ab, int w_ba, int w_spans) {
    srand(getpid() ^ time(NULL));
    FILE *in = fdopen(r_ab, "r");
    char linea[128];

    while (fgets(linea, sizeof(linea), in) != NULL) {
        char trace[ID_LEN], padre[ID_LEN];
        int num;
        if (sscanf(linea, "%16[^|]|%16[^|]|%d", trace, padre, &num) != 3) continue;

        Span s;
        memset(&s, 0, sizeof(s));
        snprintf(s.trace_id, ID_LEN, "%s", trace);        /* mismo Trace ID */
        snprintf(s.parent_span_id, ID_LEN, "%s", padre);  /* padre = span de A */
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

        emitir_span(w_spans, &s);
        write(w_ba, error ? "ERROR\n" : "OK\n", error ? 6 : 3);
    }

    fclose(in);
    close(w_ba);
    close(w_spans);
}