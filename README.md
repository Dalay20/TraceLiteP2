# TraceLite

Sistema ligero de trazabilidad distribuida en C para GNU/Linux. Procesa
solicitudes entre procesos, propaga el contexto de cada traza, muestra los spans
agrupados por Trace ID y los guarda en `traces.log`. Está inspirado en los
conceptos de trazas de OpenTelemetry y almacenamiento de Grafana Tempo; no
requiere sus SDK.

## Requisitos

- GNU/Linux
- `gcc`
- `make`

## Compilar y ejecutar

```bash
make
./main normal
./main anomalous
```

`make` genera `main` y `collector`. El programa procesa cinco solicitudes. El
modo `normal` permite comprobar la ejecución sin alertas; `anomalous` activa la
simulación de anomalías. Para eliminar los ejecutables:

```bash
make clean
```

El collector se ejecuta desde `./main` mediante `fork()` y `exec()`, por lo que
ambos ejecutables deben estar en el directorio de trabajo.

## Procesos y comunicación

El proceso principal crea el collector y los servicios api, inventario y pagos.
Las solicitudes siguen el flujo `api → inventario → pagos`; las respuestas
regresan a api. Se usan pipes anónimas para propagar el contexto, devolver
resultados y enviar spans al collector.

Cada solicitud genera tres spans relacionados: api es la raíz, inventario su
hijo y pagos hijo de inventario. Los spans comparten Trace ID y registran Span
ID, Parent Span ID, servicio, operación, tiempos, duración en milisegundos y
estado (`OK` o `ERROR`).

Formato de cada línea persistida:

```text
trace_id|span_id|parent_span_id|servicio|operacion|inicio_ms|fin_ms|duracion_ms|estado
```

## Persistencia y alertas

El collector agrupa y muestra las trazas en la terminal, y agrega los spans
válidos a `traces.log`. El archivo de texto delimitado por `|` conserva los
registros entre ejecuciones; si está vacío, se crea con un encabezado.

En modo `anomalous`, pagos puede introducir un retraso aleatorio o un error; la
solicitud 3 siempre termina con error. El collector muestra una alerta cuando
un span tiene estado `ERROR` o dura más de 300 ms. Al finalizar, informa el
total de solicitudes, spans recibidos y alertas.

Para comprobar una ejecución normal desde un registro nuevo:

```bash
rm -f traces.log
./main normal
wc -l traces.log
```

El caso normal debe procesar cinco solicitudes y almacenar 15 spans sin
alertas (`wc -l` muestra 16 líneas: el encabezado y los spans). Después,
ejecuta `./main anomalous` para comprobar las alertas y que los nuevos registros
se agregan al archivo.
