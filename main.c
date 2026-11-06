#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "services.h"

#define N_SOLICITUDES 4

int main(void) {
    int pipe_spans[2], pipe_ab[2], pipe_ba[2];
    if (pipe(pipe_spans) == -1 || pipe(pipe_ab) == -1 || pipe(pipe_ba) == -1) {
        perror("pipe"); exit(1);
    }

    /* --- Collector (fork + exec) --- */
    pid_t pc = fork();
    if (pc == 0) {
        close(pipe_spans[1]);
        close(pipe_ab[0]); close(pipe_ab[1]);   /* ¡importante! */
        close(pipe_ba[0]); close(pipe_ba[1]);
        dup2(pipe_spans[0], STDIN_FILENO);
        close(pipe_spans[0]);
        execl("./collector", "collector", NULL);
        perror("exec collector"); exit(1);
    }

    /* --- Servicio B --- */
    pid_t pb = fork();
    if (pb == 0) {
        close(pipe_spans[0]);
        close(pipe_ab[1]);
        close(pipe_ba[0]);
        servicio_b(pipe_ab[0], pipe_ba[1], pipe_spans[1]);
        exit(0);
    }

    /* --- Servicio A --- */
    pid_t pa = fork();
    if (pa == 0) {
        close(pipe_spans[0]);
        close(pipe_ab[0]);
        close(pipe_ba[1]);
        servicio_a(pipe_ab[1], pipe_ba[0], pipe_spans[1], N_SOLICITUDES);
        exit(0);
    }

    /* main cierra todo */
    close(pipe_spans[0]); close(pipe_spans[1]);
    close(pipe_ab[0]);    close(pipe_ab[1]);
    close(pipe_ba[0]);    close(pipe_ba[1]);

    waitpid(pa, NULL, 0);
    waitpid(pb, NULL, 0);
    waitpid(pc, NULL, 0);
    return 0;
}