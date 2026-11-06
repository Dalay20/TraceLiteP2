#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "services.h"

#define N_SOLICITUDES 4

static int esperar(pid_t pid, const char *nombre) {
    int status;
    pid_t result;
    do {
        result = waitpid(pid, &status, 0);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        perror("waitpid");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "%s (pid %ld) terminó anormalmente\n",
                nombre, (long)pid);
        return -1;
    }
    return 0;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    int pipe_spans[2], pipe_ai[2], pipe_ib[2], pipe_bi[2], pipe_ia[2];
    if (pipe(pipe_spans) == -1 || pipe(pipe_ai) == -1 || pipe(pipe_ib) == -1 ||
        pipe(pipe_bi) == -1 || pipe(pipe_ia) == -1) {
        perror("pipe");
        return 1;
    }

    /* --- Collector (fork + exec) --- */
    pid_t pc = fork();
    if (pc == -1) { perror("fork collector"); return 1; }
    if (pc == 0) {
        fprintf(stderr, "[collector] PID=%ld PPID=%ld\n", (long)getpid(),
                (long)getppid());
        close(pipe_spans[1]);
        close(pipe_ai[0]); close(pipe_ai[1]);
        close(pipe_ib[0]); close(pipe_ib[1]);
        close(pipe_bi[0]); close(pipe_bi[1]);
        close(pipe_ia[0]); close(pipe_ia[1]);
        if (dup2(pipe_spans[0], STDIN_FILENO) == -1) {
            perror("dup2 collector");
            _exit(1);
        }
        close(pipe_spans[0]);
        execl("./collector", "collector", NULL);
        perror("exec collector"); exit(1);
    }

    /* --- Servicio B: pagos --- */
    pid_t pb = fork();
    if (pb == -1) { perror("fork pagos"); return 1; }
    if (pb == 0) {
        fprintf(stderr, "[pagos] PID=%ld PPID=%ld\n", (long)getpid(),
                (long)getppid());
        close(pipe_spans[0]);
        close(pipe_ai[0]); close(pipe_ai[1]);
        close(pipe_ib[1]);
        close(pipe_bi[0]);
        close(pipe_ia[0]);
        _exit(servicio_b(pipe_ib[0], pipe_bi[1], pipe_spans[1]) == 0 ? 0 : 1);
    }

    /* --- Servicio I: inventario --- */
    pid_t pi = fork();
    if (pi == -1) { perror("fork inventario"); return 1; }
    if (pi == 0) {
        fprintf(stderr, "[inventario] PID=%ld PPID=%ld\n", (long)getpid(),
                (long)getppid());
        close(pipe_spans[0]);
        close(pipe_ai[1]);
        close(pipe_ib[0]);
        close(pipe_bi[1]);
        close(pipe_ia[0]);
        _exit(servicio_inventario(pipe_ai[0], pipe_ib[1], pipe_bi[0], pipe_ia[1],
                                  pipe_spans[1]) == 0 ? 0 : 1);
    }

    /* --- Servicio A --- */
    pid_t pa = fork();
    if (pa == -1) { perror("fork api"); return 1; }
    if (pa == 0) {
        fprintf(stderr, "[api] PID=%ld PPID=%ld\n", (long)getpid(),
                (long)getppid());
        close(pipe_spans[0]);
        close(pipe_ai[0]);
        close(pipe_ib[0]); close(pipe_ib[1]);
        close(pipe_bi[0]); close(pipe_bi[1]);
        close(pipe_ia[1]);
        _exit(servicio_a(pipe_ai[1], pipe_ia[0], pipe_spans[1],
                         N_SOLICITUDES) == 0 ? 0 : 1);
    }

    /* main cierra todo */
    close(pipe_spans[0]); close(pipe_spans[1]);
    close(pipe_ai[0]); close(pipe_ai[1]);
    close(pipe_ib[0]); close(pipe_ib[1]);
    close(pipe_bi[0]); close(pipe_bi[1]);
    close(pipe_ia[0]); close(pipe_ia[1]);

    int failed = 0;
    failed |= esperar(pa, "api") != 0;
    failed |= esperar(pb, "pagos") != 0;
    failed |= esperar(pi, "inventario") != 0;
    failed |= esperar(pc, "collector") != 0;
    return failed ? 1 : 0;
}