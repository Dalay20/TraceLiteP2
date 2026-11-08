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
| `services.c` / `services.h` | Lógica de los servicios A (`api`), inventario y B (`pagos`). |
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
 ├─ fork ─────────► servicio I (inventario)
 └─ fork ─────────► servicio A (api)

Flujo de una solicitud:   A ──► inventario ──► B ──► respuesta a A
Flujo de los spans:       A ──► collector ◄── inventario y B
```

| Proceso | Responsabilidad |
|---|---|
| `main` | Crea las pipes, lanza los procesos hijos, cierra sus descriptores y espera su finalización (`waitpid`). |
| Servicio A (`api`) | Genera el Trace ID y el span raíz, propaga el contexto a inventario, espera el resultado del flujo y envía su span al collector. |
| Servicio inventario | Verifica y reserva stock antes del pago. Si hay stock, propaga el contexto a pagos; si no, responde `ERROR`. |
| Servicio B (`pagos`) | Recibe el contexto después de la validación, genera su span hijo, simula el cobro y envía su resultado al collector. |
| Collector | Lee los spans desde su entrada estándar, reconstruye la jerarquía padre-hijo y muestra las trazas. |

El collector se lanza con `fork()` + `exec()`; los servicios se crean con `fork()`.

## Mecanismo de comunicación (IPC)

Se usan **pipes anónimas**, por tres razones: la comunicación es local, los procesos están emparentados por `fork()` y su uso es sencillo y eficiente en C.

| Pipe | Dirección | Contenido |
|---|---|---|
| `pipe_ai` | A → inventario | Contexto de la solicitud (Trace ID, Span ID de A y número). |
| `pipe_ib` | inventario → B | Contexto validado (Trace ID, Span ID de inventario y número). |
| `pipe_bi` | B → inventario | Resultado del pago (`OK` o `ERROR`). |
| `pipe_ia` | inventario → A | Resultado final del flujo (`OK` o `ERROR`). |
| `pipe_spans` | A, inventario, B → collector | Spans completos, una línea por span. |

El collector recibe `pipe_spans` como su entrada estándar mediante `dup2()` antes del `exec`. Cada span se envía con una sola llamada a `write()` menor a `PIPE_BUF`, por lo que los mensajes de A y B no se mezclan.

Las escrituras se realizan mediante `write_all()`, que repite la operación
hasta enviar todos los bytes y gestiona interrupciones `EINTR`; cada mensaje
individual sigue siendo menor que `PIPE_BUF`. Las respuestas,
los contextos y los spans se validan antes de ser procesados. Los procesos
reportan su PID y PPID por `stderr`, sin mezclar ese diagnóstico con la salida
de spans del collector.

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
| `servicio` | Nombre del servicio (`api`, `inventario`, `pagos`). |
| `operacion` | Operación registrada (`recibir_solicitud`, `verificar_stock`, `cobrar`). |
| `inicio_ms`, `fin_ms` | Instantes de inicio y fin en milisegundos. |
| `duracion_ms` | `fin_ms - inicio_ms`. |
| `estado` | `OK` o `ERROR`. |

Ejemplo:

```
78953f5f7e6a1357|2df404b1234c0d5b||api|recibir_solicitud|1520.1|1682.4|162.3|OK
78953f5f7e6a1357|6a6aeb692c2edabb|2df404b1234c0d5b|inventario|verificar_stock|1550.0|1582.1|32.1|OK
78953f5f7e6a1357|7b6aeb692c2edabb|6a6aeb692c2edabb|pagos|cobrar|1585.0|1661.1|76.1|OK
```

Los tiempos se miden con `clock_gettime(CLOCK_MONOTONIC)`.

## Propagación de contexto

1. A genera un Trace ID y su Span ID, y los envía a inventario por `pipe_ai`.
2. Inventario conserva el Trace ID y usa el Span ID de A como su `parent_span_id`.
3. Si hay stock, inventario genera su span y envía su Span ID a pagos por `pipe_ib`.
4. Pagos usa el Span ID de inventario como su `parent_span_id` y genera su propio Span ID.
5. Las respuestas regresan por `pipe_bi` y `pipe_ia`, conservando el estado de la solicitud.

## Anomalías simuladas

El servicio B introduce anomalías que quedan registradas en su span (y se reflejan
en los spans de inventario y A):

- **Retraso aleatorio:** con 25 % de probabilidad se agregan entre 300 y 500 ms.
- **Error:** con 10 % de probabilidad el estado es `ERROR`.
- **Error garantizado:** la solicitud número 3 siempre termina en `ERROR`, para que la demostración incluya siempre una anomalía.

El inventario inicia con tres unidades disponibles. Cada solicitud aprobada
reserva una unidad antes de llamar a pagos; por ello, cuando se procesan cuatro
solicitudes, la cuarta puede terminar con `ERROR` por falta de stock y no genera
un span de pago.

## Ejemplo de salida

```
===== TraceLite: 11 spans recibidos =====

Trace 78953f5f7e6a1357
└─ [api] recibir_solicitud       162.3 ms  OK    (span 2df404b1234c0d5b, padre: -)
    └─ [inventario] verificar_stock 32.1 ms  OK (span 6a6aeb692c2edabb, padre: 2df404b1234c0d5b)
        └─ [pagos] cobrar            76.1 ms  OK (span 7b6aeb692c2edabb, padre: 6a6aeb692c2edabb)

Trace 00edba1d64fdc032
└─ [api] recibir_solicitud       182.3 ms  ERROR (span 6d18ea0069227993, padre: -)
    └─ [inventario] verificar_stock 32.1 ms  OK (span 74b0dc5166334873, padre: 6d18ea0069227993)
        └─ [pagos] cobrar             142.1 ms  ERROR (span 756b9dfd16554a12, padre: 74b0dc5166334873)
```

Cuando inventario detecta falta de stock, la traza termina en ese servicio y
no se genera un span de pagos. Cuando sí hay stock, pagos siempre aparece como
hijo de inventario, incluso si el cobro termina con ERROR.

## Estado del avance

**Implementado**

- [x] Proceso principal con `fork()` y `exec()`
- [x] Tres servicios y un collector como procesos independientes
- [x] Spans con Trace ID, Span ID, Parent Span ID, servicio, operación, tiempos, duración y estado
- [x] Propagación de contexto entre servicios
- [x] Verificación de stock antes del pago
- [x] Comunicación por pipes hacia el collector
- [x] Visualización en terminal agrupada por Trace ID con relación padre-hijo
- [x] Cuatro solicitudes completas con anomalías de tiempo y de error

**Pendiente para la entrega final**

- [ ] Completar según los requisitos de la entrega final (por definir)
