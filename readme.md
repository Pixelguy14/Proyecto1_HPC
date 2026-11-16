# Proyecto 1 HPC

Ordenamiento de la matriz `matriz_a.txt` (40,000 × 40,000, 1.6 G valores)
mediante **Sample Sort** (scatter/gather) con `mmap`, `pthreads` y memoria
compartida POSIX. El modo por defecto valida la lectura por chunks sin ordenar;
`--hilos N` lanza el Sort paralelo y produce un resultado globalmente ordenado.

**Equipo:** Francisco Javier Meléndez Ruiz y José Julian Sierra Alvarez.

## Compilación

Requisitos: `g++` con C++17 y Linux (usa `mmap`, `shm_open` y `pthreads`).

```bash
make          # genera ./ordenar_matriz
make clean    # borra obj/ y el binario
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
# Lectura por chunks: validar matriz_a.txt completa sin ordenar (unicamente lectura)
./ordenar_matriz 64

# Sample Sort con X hilos: ordena y verifica (sin guardar)
./ordenar_matriz 64 --hilos 8 # parametors: tamaño del chunk, y cantidad de hilos

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

### Requisitos de memoria y Activación Automática Out-of-Core

El Sample Sort puramente en memoria necesita dos arreglos de ~12.8 GB (`data` y `temp`)
en `/dev/shm`, más los buffers de merge (~25.6 GB a 35 GB de RAM). En una máquina típica
con 14 GiB de RAM y 7.5 GiB de `/dev/shm` el archivo completo no cabe en memoria a la vez.

**Activación Automática Out-of-Core (External Merge Sort):**
El programa consulta en runtime la memoria disponible. Si los datos no caben en RAM/tmpfs,
**activa automáticamente el ordenamiento externo sin requerir banderas adicionales**:
1. **Fase 1 (Runs paralelos):** Lee bloques de `chunk_mb` MB con `ChunkedTextReader`
   y los ordena en memoria mediante Sample Sort paralelo multihilo (`--hilos N`).
   Vuelca cada bloque a un run binario temporal en `./runs/`.
2. **Fase 2 (K-Way Merge en streaming):** Abre todos los runs con buffers prefetched,
   los unifica en tiempo real con un Min-Heap de bajo consumo de memoria (< 50 MB de RAM)
   y emite el resultado ordenado a `salida` verificando el orden monotono y el checksum de bits.
3. Limpia automáticamente los archivos temporales de `runs/`.

Para matrices que sí caben en RAM (ej: 1,000 × 1,000 o 10,000 × 10,000), el ordenamiento
se ejecuta directamente en memoria compartida sin tocar disco. `--forzar` fuerza la
ejecución puramente en memoria.

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
