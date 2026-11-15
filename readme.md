# Proyecto 1 HPC

Ordenamiento de la matriz `matriz_a.txt` (40,000 × 40,000, 1.6 G valores)
mediante **Sample Sort** (scatter/gather) con `mmap`, `pthreads` y memoria
compartida POSIX. El modo por defecto valida la lectura por chunks sin ordenar;
`--hilos N` lanza el Sort paralelo y produce un resultado globalmente ordenado.

**Equipo:** Francisco Javier Meléndez Ruiz y José Julian Sierra Alvarez.

> **Nota de diseño:** la lectura del archivo de entrada es inherentemente
> secuencial (un hilo). `ChunkedTextReader` usa `mmap` + `MADV_SEQUENTIAL` y
> libera páginas con `MADV_DONTNEED`, pero el parseo carácter a carácter no se
> paraleliza: el ancho de banda de I/O es el cuello de botella, no el CPU. El
> paralelismo se aplica únicamente al ordenamiento (scatter/gather + orden local
> de cubetas con pthreads).

## Compilación

Requisitos: `g++` con C++17 y Linux (usa `mmap`, `shm_open` y `pthreads`).

```bash
make          # genera ./ordenar_matriz
make clean    # borra obj/ y el binario
```

Equivale a:

```bash
g++ -O3 -std=c++17 -Wall -Wextra *.cpp -o ordenar_matriz -lrt -lpthread
```

## Uso

La matriz de entrada es siempre `matriz_a.txt` (40,000 × 40,000). No hay que
pasar la ruta ni las dimensiones. Los únicos argumentos son:

```bash
./ordenar_matriz <chunk_mb> [--hilos <N>] [salida] [--forzar]
./ordenar_matriz --verificar-bin <archivo.bin>
```

| Argumento | Tipo | Descripción |
|---|---|---|
| `chunk_mb` | Obligatorio | Tamaño del bloque de lectura en MB (ej: `64`). |
| `--hilos N` | Opcional | Lanza el Sample Sort paralelo con `N` hilos/cubetas (`N >= 2`). |
| `salida` | Opcional | Ruta de salida con `--hilos`. Extensión decide el formato. |
| `--verificar-bin archivo.bin` | Independiente | Verifica que un `.bin` esté ordenado. |
| `--forzar` | Opcional | Omite el chequeo previo de RAM y `/dev/shm`. |

### Modos

| Modo | Comando de ejemplo | Descripción |
|---|---|---|
| Lectura (default) | `./ordenar_matriz 64` | Valida formato y conteo. No ordena ni guarda. |
| Hilos (Sort paralelo) | `./ordenar_matriz 64 --hilos 8` | Sample Sort completo. Orden global. |
| Hilos + salida | `./ordenar_matriz 64 --hilos 8 salida.bin` | Idem, guarda resultado. |
| Verificación binaria | `./ordenar_matriz --verificar-bin salida.bin` | Verifica orden de un `.bin`. |

### Formatos de salida

La extensión de salida decide el formato:

| Extensión | Separador | Precisión |
|---|---|---|
| `.csv` | coma | `%.17g` |
| `.txt` | espacio | `%.10g` |
| `.bin` | — | cabecera `MATBIN1` + `double` crudo (recomendado para archivos grandes) |

La entrada acepta comas, espacios, tabs y saltos de línea (`.csv` o `.txt`).
Todos los modos leen el archivo por chunks del tamaño indicado con `mmap`; el
texto completo nunca se carga en heap.

### Ejemplos

```bash
# Lectura por chunks: validar matriz_a.txt completa sin ordenar
./ordenar_matriz 64

# Sample Sort con 8 hilos: ordena y verifica (sin guardar)
./ordenar_matriz 64 --hilos 8

# Sample Sort con 4 hilos: ordena y guarda en binario
./ordenar_matriz 64 --hilos 4 salida.bin

# Verificar que el .bin generado esté ordenado
./ordenar_matriz --verificar-bin salida.bin
```

## Archivo `matriz_a.txt`

| Dato | Valor |
|---|---:|
| Filas × columnas | 40,000 × 40,000 |
| Valores | 1,600,000,000 |
| Tamaño en disco | ~16.8 GB |
| Tipo | floats con 6 decimales, separados por espacios |
| Tamaño como `double` | ~12.8 GB por arreglo |

### Requisitos de memoria

El Sample Sort en memoria necesita dos arreglos de ~12.8 GB (`data` y `temp`)
en `/dev/shm`, más los buffers de merge. En una máquina con 14 GiB de RAM y
7.5 GiB de `/dev/shm` **no cabe el archivo completo**. El programa hace un
chequeo previo de memoria y aborta con un mensaje claro antes de intentarlo.
`--forzar` omite el chequeo (puede provocar `SIGBUS` o `bad_alloc`).

Para matrices más pequeñas (ej: 1,000 × 1,000 o 10,000 × 10,000) el Sort en
memoria sí funciona con la RAM disponible.

## Entrega

```bash
make entrega     # o ./empaquetar_entrega.sh
```

Crea `entrega/` con el código, el `Makefile`, el `README`, la licencia y
`matriz_a.txt`. Usa un hardlink para el archivo de datos cuando el sistema de
archivos lo permite, así no se duplican los 16.8 GB. El `.bin` de salida no se
entrega: se genera al ejecutar el programa.

## Características técnicas

- Parseo manual de números, sin librerías de CSV.
- Sin `std::sort`: Merge Sort propio y orden local de cubetas.
- Sample Sort con muestreo reservoir, splitters y scatter/gather.
- Memoria compartida POSIX (`shm_open` + `mmap`) + `pthreads`.
- Lectura secuencial por chunks con `mmap` y `madvise(MADV_DONTNEED)`.
- Salida `.csv`, `.txt` y `.bin` con verificación binaria por chunks.
- Chequeo previo de memoria (`/proc/meminfo` y `/dev/shm`).
- La lectura es inherentemente secuencial; el paralelismo es solo en el Sort.

## Licencia

[LICENCIA MIT](LICENSE)
