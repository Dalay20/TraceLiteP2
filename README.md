# TraceLiteP2 – Avance parcial

Primera versión funcional de un sistema ligero de trazabilidad distribuida en C para GNU/Linux, inspirado en OpenTelemetry y Grafana Tempo. Genera trazas entre procesos, las transmite a un recolector mediante pipes y las muestra en la terminal. No utiliza ningún SDK de OpenTelemetry ni de Tempo.

## Requisitos

- GNU/Linux
- `gcc`
- `make`

## Estructura del proyecto

| Archivo | Descripción |
|---|---|
| `main.c` | Proceso principal: crea las pipes y los procesos hijos (`fork`/`exec`). |
| `services.c` / `services.h` | Lógica de los servicios A (`api`) y B (`pagos`). |
| `collector.c` | Collector Lite: recibe los spans, los agrupa por Trace ID y los imprime. |
| `span.h` | Estructura `Span`, medición de tiempo, generación de IDs y serialización. |
| `Makefile` | Compilación del proyecto. |

## Compilación

```bash
make
```

Genera dos ejecutables: `main` y `collector`. Para limpiar:

```bash
make clean
```

## Ejecución

```bash
./main
```

El programa genera 4 solicitudes completas. Al finalizar, el collector imprime en la consola todas las trazas recibidas. Cada ejecución produce resultados distintos, porque los tiempos de respuesta y algunas anomalías son aleatorios.

> `collector` debe estar compilado en el mismo directorio desde el que se ejecuta `./main`, porque se lanza con `exec`.

## Arquitectura de procesos

```
main (proceso principal)
 ├─ fork + exec ──► collector
 ├─ fork ─────────► servicio B (pagos)
 └─ fork ─────────► servicio A (api)

Flujo de una solicitud:   A ──► B ──► respuesta a A
Flujo de los spans:       A ──► collector ◄── B
```

| Proceso | Responsabilidad |
|---|---|
| `main` | Crea las pipes, lanza los procesos hijos, cierra sus descriptores y espera su finalización (`waitpid`). |
| Servicio A (`api`) | Genera el Trace ID y el span raíz, propaga el contexto a B, espera su respuesta y envía su span al collector. |
| Servicio B (`pagos`) | Recibe el contexto, genera su propio span hijo, simula la operación (con posibles anomalías) y envía su span al collector. |
| Collector | Lee los spans desde su entrada estándar, reconstruye la jerarquía padre-hijo y muestra las trazas. |

El collector se lanza con `fork()` + `exec()`; los servicios se crean con `fork()`.

## Mecanismo de comunicación (IPC)

Se usan **pipes anónimas**, por tres razones: la comunicación es local, los procesos están emparentados por `fork()` y su uso es sencillo y eficiente en C.

| Pipe | Dirección | Contenido |
|---|---|---|
| `pipe_ab` | A → B | Contexto de la solicitud (Trace ID y Span ID del padre). |
| `pipe_ba` | B → A | Respuesta de B (`OK` o `ERROR`). |
| `pipe_spans` | A, B → collector | Spans completos, una línea por span. |

El collector recibe `pipe_spans` como su entrada estándar mediante `dup2()` antes del `exec`. Cada span se envía con una sola llamada a `write()` menor a `PIPE_BUF`, por lo que los mensajes de A y B no se mezclan.

## Formato del span

Cada span es una línea de texto con campos separados por `|`:

```
trace_id|span_id|parent_span_id|servicio|operacion|inicio_ms|fin_ms|duracion_ms|estado
```

| Campo | Descripción |
|---|---|
| `trace_id` | Identificador de la traza, compartido por todos los spans de una solicitud. |
| `span_id` | Identificador único del span. |
| `parent_span_id` | Span ID del padre; vacío en el span raíz. |
| `servicio` | Nombre del servicio (`api`, `pagos`). |
| `operacion` | Operación registrada (`recibir_solicitud`, `cobrar`). |
| `inicio_ms`, `fin_ms` | Instantes de inicio y fin en milisegundos. |
| `duracion_ms` | `fin_ms - inicio_ms`. |
| `estado` | `OK` o `ERROR`. |

Ejemplo:

```
78953f5f7e6a1357|2df404b1234c0d5b||api|recibir_solicitud|1520.1|1682.4|162.3|OK
78953f5f7e6a1357|2d6aeb692c2edabb|2df404b1234c0d5b|pagos|cobrar|1575.0|1661.1|86.1|OK
```

Los tiempos se miden con `clock_gettime(CLOCK_MONOTONIC)`.

## Propagación de contexto

1. A genera un Trace ID y su Span ID, y los envía a B por `pipe_ab`.
2. B conserva el Trace ID y usa el Span ID de A como su `parent_span_id`.
3. B genera su propio Span ID, de modo que no se pierde la correlación con la solicitud original.

## Anomalías simuladas

El servicio B introduce anomalías que quedan registradas en su span (y se reflejan en el span de A):

- **Retraso aleatorio:** con 25 % de probabilidad se agregan entre 300 y 500 ms.
- **Error:** con 10 % de probabilidad el estado es `ERROR`.
- **Error garantizado:** la solicitud número 3 siempre termina en `ERROR`, para que la demostración incluya siempre una anomalía.

## Ejemplo de salida

```
===== TraceLite: 8 spans recibidos =====

Trace 78953f5f7e6a1357
└─ [api] recibir_solicitud       162.3 ms  OK    (span 2df404b1234c0d5b, padre: -)
    └─ [pagos] cobrar              86.1 ms  OK    (span 2d6aeb692c2edabb, padre: 2df404b1234c0d5b)

Trace 00edba1d64fdc032
└─ [api] recibir_solicitud       182.3 ms  ERROR (span 6d18ea0069227993, padre: -)
    └─ [pagos] cobrar             142.1 ms  ERROR (span 756b9dfd16554a12, padre: 6d18ea0069227993)
```

## Estado del avance

**Implementado**

- [x] Proceso principal con `fork()` y `exec()`
- [x] Dos servicios y un collector como procesos independientes
- [x] Spans con Trace ID, Span ID, Parent Span ID, servicio, operación, tiempos, duración y estado
- [x] Propagación de contexto entre servicios
- [x] Comunicación por pipes hacia el collector
- [x] Visualización en terminal agrupada por Trace ID con relación padre-hijo
- [x] Cuatro solicitudes completas con anomalías de tiempo y de error

**Pendiente para la entrega final**

- [ ] Completar según los requisitos de la entrega final (por definir)
