// ============================================================================
// Proyecto 1 - Topicos de HPC
// Sprint 1: lectura manual de CSV + Merge Sort propio + guardado en archivo
// Sprint 2: modo --hilos con mmap compartido, pthreads y mutex
// Sprint 3: Sample Sort completo (scatter/gather) integrado en --hilos
// Fase 1 de entrega: lectura por chunks con mmap, salida .bin y verificacion
//
// Equipo: Francisco Javier Melendez Ruiz y Jose Julian Sierra Alvarez
//
// La matriz de entrada esta fija: "matriz_a.txt" (40,000 x 40,000).
// El nombre de archivo y las dimensiones se obtienen de las constantes globales
// kMatrizPath, kMatrizRows y kMatrizCols; no se pasan por linea de comandos.
//
// Este archivo es el punto de entrada que funciona con los modulos:
//   - csv_parser:     convierte el CSV/TXT en doubles.
//   - chunked_reader: lee por chunks con mmap, sin cargar todo en heap.
//   - merge_sort:     ordena el arreglo (secuencial o por rangos).
//   - sort_worker:    ordena el bloque de un hilo.
//   - shared_memory:  crea los segmentos mmap y el bloque de control.
//   - parallel_sort:  crea y espera los hilos.
//   - sample_sort:    muestreo, splitters y busqueda binaria.
//   - scatter_gather: reparte en cubetas y ordena cada cubeta en paralelo.
//   - file_io:        verifica el orden y guarda .csv/.txt/.bin.
//
// NOTA DE DISENO: la lectura del CSV es inherentemente secuencial (un solo
// hilo). ChunkedTextReader usa mmap + MADV_SEQUENTIAL y libera paginas con
// MADV_DONTNEED, pero el parseo caracter a caracter no se paraleliza: la
// velocidad esta limitada por el ancho de banda de I/O, no por el CPU.
// El paralelismo se aplica unicamente al ordenamiento (scatter/gather +
// orden local de cubetas con pthreads).
//
// Compilacion:
//   make
//
// Uso (la matriz de entrada es siempre "matriz_a.txt"):
//   ./ordenar_matriz <chunk_mb>
//   ./ordenar_matriz <chunk_mb> --hilos <N> [salida]
//   ./ordenar_matriz --verificar-bin <archivo.bin>
//
// Ejemplos:
//   ./ordenar_matriz 64
//   ./ordenar_matriz 64 --hilos 8 salida.bin
//   ./ordenar_matriz --verificar-bin salida.bin
// ============================================================================
#include "chunked_reader.hpp"
#include "csv_parser.hpp"
#include "external_sort.hpp"
#include "file_io.hpp"
#include "parallel_sort.hpp"
#include "scatter_gather.hpp"
#include "shared_memory.hpp"
#include "sort_worker.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include <sys/statvfs.h>
#include <unistd.h>

// ============================================================================
// Utilidades de la linea de comandos y de medicion
// ============================================================================
namespace {

// ---------------------------------------------------------------------------
// usage - contrato de la interfaz de linea de comandos
// ---------------------------------------------------------------------------
// Se imprime en stderr porque un uso incorrecto es un error, no una salida
// normal del programa.
//
// La matriz de entrada es siempre "matriz_a.txt" (40,000 x 40,000).
// Los unicos argumentos variables son:
//   1. chunk_mb      - tamano del bloque de lectura en MB.
//   2. --hilos N     - cantidad de hilos/cubetas para el Sort paralelo.
//   3. salida        - archivo de salida opcional (con --hilos).
//   4. --verificar-bin archivo.bin - verificar un .bin ya generado.
void usage(const char* prog) {
    std::fprintf(
        stderr,
        "Uso (la entrada es siempre 'matriz_a.txt', 40000x40000):\n"
        "  %s <chunk_mb>\n"
        "                              lectura por chunks sin ordenar (default)\n"
        "  %s <chunk_mb> --hilos <N> [salida]\n"
        "                              Sample Sort paralelo con N hilos/cubetas\n"
        "                              Salida opcional: .csv, .txt o .bin\n"
        "  %s --verificar-bin <archivo.bin>\n"
        "                              verificar que un .bin este ordenado\n"
        "\n"
        "Argumentos:\n"
        "  chunk_mb   Tamano del chunk de lectura en MB (entero positivo).\n"
        "  N          Cantidad de hilos/cubetas del Sort (>= 2).\n"
        "  salida     Ruta de salida (.csv, .txt o .bin). Solo con --hilos.\n"
        "\n"
        "Opciones:\n"
        "  --forzar   Omite el chequeo previo de memoria (usar con cuidado).\n"
        "\n"
        "NOTA: La lectura es secuencial (un hilo). El paralelismo se aplica\n"
        "      al ordenamiento (scatter/gather + orden local de cubetas).\n"
        "\n"
        "Ejemplos:\n"
        "  %s 64\n"
        "  %s 64 --hilos 8\n"
        "  %s 64 --hilos 8 salida.bin\n"
        "  %s --verificar-bin salida.bin\n",
        prog, prog, prog, prog, prog, prog, prog);
}

// ---------------------------------------------------------------------------
// parse_size - convierte un argumento a entero positivo
// ---------------------------------------------------------------------------
// Constantes de la matriz de entrada fija
// ---------------------------------------------------------------------------
// La matriz "matriz_a.txt" es el unico archivo de datos del proyecto.
// Sus dimensiones son conocidas y no se pasan por linea de comandos.
constexpr const char* kMatrizPath = "matriz_a.txt";
constexpr std::size_t kMatrizRows = 40000;
constexpr std::size_t kMatrizCols = 40000;

// ---------------------------------------------------------------------------
// parse_size - convierte un argumento a entero positivo
// ---------------------------------------------------------------------------
// Se usa strtoull para detectar desbordamiento con errno. Se exige que:
//   - el texto empiece con un digito (rechaza negativos y simbolos),
//   - se consuma todo el texto (rechaza "12abc"),
//   - el valor sea distinto de cero,
//   - no haya desbordamiento.
//
// Parametros:
//   text   Cadena recibida de argv.
//   value  Salida: valor convertido, solo se modifica si devuelve true.
//
// Devuelve true si el texto es un entero positivo valido.
bool parse_size(const char* text, std::size_t& value) {
    if (text == nullptr || text[0] < '0' || text[0] > '9') {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || v == 0) return false;
    value = static_cast<std::size_t>(v);
    return true;
}

// ---------------------------------------------------------------------------
// milliseconds - diferencia entre dos marcas de steady_clock
// ---------------------------------------------------------------------------
// steady_clock es monotonico: no salta si el reloj del sistema se ajusta.
// Se usa para medir duraciones, no para saber la hora.
double milliseconds(std::chrono::steady_clock::time_point begin,
                    std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

// ---------------------------------------------------------------------------
// checksum_bits - huella independiente del orden
// ---------------------------------------------------------------------------
// Suma los bits de cada double como uint64_t. La suma de bits no depende del
// orden de los elementos, asi que dos arreglos con el mismo multiset de valores
// producen el mismo resultado aunque esten ordenados de forma distinta.
//
// Se usa para verificar que el Sample Sort no perdio ni duplico elementos sin
// depender de la precision de una suma en punto flotante.
std::uint64_t checksum_bits(const double* data, std::size_t n) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &data[i], sizeof(bits));
        sum += bits;
    }
    return sum;
}

// ---------------------------------------------------------------------------
// mem_available_bytes - RAM realmente disponible segun /proc/meminfo
// ---------------------------------------------------------------------------
// MemAvailable incluye cache reclamable, a diferencia de MemFree. Si no se
// puede leer /proc/meminfo se devuelve 0 y el chequeo omite la comparacion.
std::size_t mem_available_bytes() {
    std::FILE* file = std::fopen("/proc/meminfo", "r");
    if (file == nullptr) return 0;

    char line[256];
    std::size_t available = 0;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        unsigned long long value = 0;
        if (std::sscanf(line, "MemAvailable: %llu kB", &value) == 1) {
            available = static_cast<std::size_t>(value) * 1024;
            break;
        }
    }
    std::fclose(file);
    return available;
}

// ---------------------------------------------------------------------------
// shm_free_bytes - espacio libre en /dev/shm
// ---------------------------------------------------------------------------
std::size_t shm_free_bytes() {
    struct statvfs vfs;
    if (::statvfs("/dev/shm", &vfs) != 0) return 0;
    return static_cast<std::size_t>(vfs.f_bavail) *
           static_cast<std::size_t>(vfs.f_frsize);
}

void print_gb(const char* label, std::size_t bytes) {
    std::fprintf(stderr, "  %-22s %8.2f GB\n", label,
                 static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

// ---------------------------------------------------------------------------
// check_memory - chequeo previo para evitar SIGBUS / bad_alloc
// ---------------------------------------------------------------------------
// shm_bytes: lo que se reservara en /dev/shm (0 si no aplica).
// ram_bytes: estimacion del pico de RAM (mmap + buffers temporales).
// Si `force` es true se omite todo (--forzar).
bool check_memory(const char* mode, std::size_t shm_bytes,
                  std::size_t ram_bytes, bool force) {
    if (force) {
        std::fprintf(stderr,
                     "main: chequeo de memoria omitido por --forzar\n");
        return true;
    }

    const std::size_t shm_free = shm_free_bytes();
    const std::size_t ram_free = mem_available_bytes();

    bool fails = false;
    if (shm_bytes > 0 && shm_free > 0 && shm_bytes > shm_free) fails = true;
    if (ram_bytes > 0 && ram_free > 0 && ram_bytes > ram_free) fails = true;
    if (!fails) return true;

    std::fprintf(stderr, "\nMemoria insuficiente para %s:\n", mode);
    print_gb("Requerido en /dev/shm:", shm_bytes);
    print_gb("/dev/shm libre:", shm_free);
    print_gb("RAM requerida (pico):", ram_bytes);
    print_gb("RAM disponible:", ram_free);
    std::fprintf(stderr,
                 "\nSugerencias:\n"
                 "  - reducir el chunk: ./ordenar_matriz <chunk_mb>\n"
                 "  - ampliar /dev/shm: sudo mount -o remount,size=28G /dev/shm\n"
                 "  - ejecutar en una maquina con mas RAM\n"
                 "  - usar --forzar para intentarlo de todos modos\n");
    return false;
}

// ---------------------------------------------------------------------------
// print_summary - resumen de los datos ya cargados
// ---------------------------------------------------------------------------
// Recorre el arreglo una sola vez para calcular minimo, maximo y suma. Las
// esquinas se imprimen para detectar rapidamente si la lectura quedo bien
// (por ejemplo, si el orden de filas/columnas se invirtio).
//
// Parametros:
//   path     Ruta del CSV, solo para mostrarla.
//   data     Arreglo de rows * cols doubles ya cargado.
//   rows     Numero de filas.
//   cols     Numero de columnas.
//   read_ms  Tiempo que tardo la lectura, para calcular el rendimiento.
//
// Precondicion: data != nullptr, rows > 0, cols > 0.
// Nota: la suma es un checksum; se calcula antes de ordenar para comparar
// contra el resultado ordenado si hace falta.
void print_summary(const char* path, const double* data, std::size_t rows,
                   std::size_t cols, double read_ms) {
    const std::size_t total = rows * cols;

    double minimo = data[0];
    double maximo = data[0];
    long double suma = 0.0L;

    for (std::size_t i = 0; i < total; ++i) {
        const double v = data[i];
        if (v < minimo) minimo = v;
        if (v > maximo) maximo = v;
        suma += v;
    }

    std::printf("Archivo         : %s\n", path);
    std::printf("Dimensiones     : %zu x %zu\n", rows, cols);
    std::printf("Valores leidos  : %zu\n", total);
    std::printf("Tiempo lectura  : %.3f ms\n", read_ms);
    std::printf("Rendimiento     : %.2f M valores/s\n",
                read_ms > 0.0
                    ? (static_cast<double>(total) / (read_ms / 1000.0)) / 1e6
                    : 0.0);

    // Esquinas: [0][0], [0][cols-1], [rows-1][0], [rows-1][cols-1].
    std::printf("Esquinas        : [0][0]=%g  [0][%zu]=%g  [%zu][0]=%g  [%zu][%zu]=%g\n",
                data[0], cols - 1, data[cols - 1],
                rows - 1, data[(rows - 1) * cols],
                rows - 1, cols - 1, data[total - 1]);
    std::printf("Min / Max       : %g / %g\n", minimo, maximo);
    std::printf("Suma (checksum) : %Lg\n", suma);
}

// ============================================================================
// Flujo con hilos: Sample Sort completo (orden global)
// ============================================================================

// ---------------------------------------------------------------------------
// run_parallel - mmap + pthreads + Sample Sort con orden global
// ---------------------------------------------------------------------------
// Este es el flujo principal del proyecto. Usa `num_threads` como la cantidad
// de cubetas Y de workers: un hilo por cubeta. El resultado es globalmente
// ordenado gracias al scatter/gather del Sample Sort.
//
// Recursos que se crean y deben liberarse en orden inverso:
//   1. /proyecto1_hpc_data  - arreglo original de entrada
//   2. /proyecto1_hpc_temp  - buffer de scatter y resultado ordenado
//   3. /proyecto1_hpc_ctrl  - mutex process-shared + contadores de workers
//
// Flujo:
//   shared_create(data) -> shared_create(temp) -> shared_create(ctrl)
//     -> shared_control_init()
//     -> load_csv_into(data)        [secuencial, un hilo]
//     -> sample_sort_parallel()     [scatter secuencial + orden local paralelo]
//          -> build_sample_plan()   muestreo y splitters
//          -> conteo por cubeta     [secuencial]
//          -> scatter a temp        [secuencial]
//          -> parallel_sort_buckets [paralelo: N hilos sobre N cubetas]
//     -> is_sorted_ascending(temp)  verificacion global
//     -> checksum de bits contra data
//     -> save_matrix(output_path)   si se pidio salida
//     -> limpieza de segmentos
//
// NOTA: La lectura del CSV (load_csv_into) es inherentemente secuencial.
// ChunkedTextReader parsea caracter a caracter con un solo hilo; el ancho
// de banda de I/O es el cuello de botella, no el CPU. El paralelismo
// comienza despues de que todos los datos estan en memoria compartida.
//
// Parametros:
//   path         Archivo de entrada (.csv o .txt).
//   rows, cols   Dimensiones de la matriz.
//   num_threads  Cantidad de hilos/cubetas del Sample Sort (>= 2).
//   output_path  Archivo de salida (.csv/.txt/.bin), o nullptr.
//   chunk_bytes  Tamano del chunk de lectura en bytes.
//   force        Si es true, omite el chequeo previo de memoria (--forzar).
//
// Devuelve true si:
//   - se leyo la cantidad exacta de valores esperada,
//   - el Sort termino sin errores,
//   - el arreglo resultado queda globalmente ordenado,
//   - el checksum de bits coincide con la entrada (sin perdidas),
//   - y, si se pidio salida, se guardo correctamente.
bool run_parallel(const char* path, std::size_t rows, std::size_t cols,
                  std::size_t num_threads, const char* output_path,
                  std::size_t chunk_bytes, bool force) {
    // Validar que filas * columnas no desborde size_t.
    if (rows != 0 && cols > static_cast<std::size_t>(-1) / rows) {
        std::fprintf(stderr, "main: desbordamiento en filas * columnas\n");
        return false;
    }
    const std::size_t total      = rows * cols;
    const std::size_t data_bytes = total * sizeof(double);

    // Comprobar si la matriz cabe en memoria fisica (RAM y /dev/shm).
    // Si no cabe y no se paso --forzar, se activa automaticamente el
    // ordenamiento externo (runs en disco + K-Way Merge con Min-Heap).
    const std::size_t shm_free = shm_free_bytes();
    const std::size_t ram_free = mem_available_bytes();
    const bool in_memory_fits = (force || (data_bytes * 2 <= shm_free && data_bytes * 3 <= ram_free));

    if (!in_memory_fits) {
        std::printf("\n\n");
        std::printf("TEST: La matriz (%zu valores = %.2f GB) no cabe en memoria\n",
                    total, static_cast<double>(data_bytes) / (1024.0 * 1024.0 * 1024.0));
        std::printf("disponible (requiere %.2f GB en shm / %.2f GB en RAM;\n",
                    static_cast<double>(data_bytes * 2) / (1024.0 * 1024.0 * 1024.0),
                    static_cast<double>(data_bytes * 3) / (1024.0 * 1024.0 * 1024.0));
        std::printf("disponible: %.2f GB en shm / %.2f GB en RAM).\n",
                    static_cast<double>(shm_free) / (1024.0 * 1024.0 * 1024.0),
                    static_cast<double>(ram_free) / (1024.0 * 1024.0 * 1024.0));
        std::printf("-> Activando el Ordenamiento Externo Out-of-Core\n");
        std::printf("   (runs en disco + K-Way Merge con Min-Heap y streaming)\n");
        std::printf("\n\n");

        ExternalSortStats ext_stats;
        return run_external_sort(path, rows, cols, num_threads, output_path,
                                 chunk_bytes, ext_stats);
    }

    // Crear los tres segmentos de memoria compartida en orden.
    // Si cualquiera falla, se liberan los ya creados en orden inverso.
    SharedMemory data_shm;
    if (!shared_create(data_shm, "/proyecto1_hpc_data", data_bytes)) {
        return false;
    }

    SharedMemory temp_shm;
    if (!shared_create(temp_shm, "/proyecto1_hpc_temp", data_bytes)) {
        shared_release(data_shm, true);
        return false;
    }

    SharedMemory ctrl_shm;
    if (!shared_create(ctrl_shm, "/proyecto1_hpc_ctrl",
                       sizeof(SharedControl))) {
        shared_release(temp_shm, true);
        shared_release(data_shm, true);
        return false;
    }

    // Inicializar el mutex process-shared y los contadores del bloque de control.
    SharedControl* control = static_cast<SharedControl*>(ctrl_shm.addr);
    if (!shared_control_init(control, num_threads)) {
        shared_release(ctrl_shm, true);
        shared_release(temp_shm, true);
        shared_release(data_shm, true);
        return false;
    }

    // Punteros a los arreglos en memoria compartida.
    double* data = static_cast<double*>(data_shm.addr);
    double* temp = static_cast<double*>(temp_shm.addr);

    // Lambda de limpieza unica para todas las rutas posteriores a init.
    // Se invoca al salir tanto en exito como en error.
    auto cleanup = [&]() {
        shared_control_destroy(control);
        shared_release(ctrl_shm, true);
        shared_release(temp_shm, true);
        shared_release(data_shm, true);
    };

    // --- Lectura (secuencial, un hilo) ---
    // load_csv_into() usa ChunkedTextReader: mapea el archivo con mmap,
    // parsea por bloques y escribe directo en `data` sin buffer intermedio.
    // El parseo es un bucle secuencial; no se paraleliza.
    const auto read_begin = std::chrono::steady_clock::now();
    std::size_t written = 0;
    const bool loaded = load_csv_into(path, data, total, chunk_bytes, written);
    const auto read_end = std::chrono::steady_clock::now();

    if (!loaded || written != total) {
        std::fprintf(stderr,
                     "main: error al leer '%s': se esperaban %zu valores, "
                     "se leyeron %zu\n",
                     path, total, written);
        cleanup();
        return false;
    }

    const double read_ms = milliseconds(read_begin, read_end);

    // Imprimir resumen de la lectura antes de lanzar el Sort.
    std::printf("Modo            : hilos (Sample Sort paralelo)\n");
    std::printf("Archivo         : %s\n", path);
    std::printf("Hilos/cubetas   : %zu\n", num_threads);
    std::printf("Lectura         : %.3f ms (%.2f M val/s) [secuencial]\n",
                read_ms,
                read_ms > 0.0
                    ? (static_cast<double>(total) / (read_ms / 1000.0)) / 1e6
                    : 0.0);
    std::printf("Memoria compart.: %.2f MB datos + %.2f MB temp\n",
                static_cast<double>(data_bytes) / (1024.0 * 1024.0),
                static_cast<double>(data_bytes) / (1024.0 * 1024.0));
    print_summary(path, data, rows, cols, read_ms);

    // --- Ordenamiento paralelo (scatter secuencial + cubetas en paralelo) ---
    // 10,000 muestras: barato (1.3% del tiempo en 100 M) y suficiente para
    // estimar quantiles con datos uniformes. Semilla fija para reproducibilidad.
    constexpr std::size_t kSampleSize = 10000;
    constexpr std::uint64_t kSeed = 42;

    ScatterGatherStats stats;
    const bool sample_ok =
        sample_sort_parallel(data, temp, total, num_threads, kSampleSize,
                             kSeed, control, stats);

    if (!sample_ok) {
        cleanup();
        return false;
    }

    // --- Verificacion global ---
    // is_sorted_ascending verifica O(n) que cada par consecutivo cumpla <=.
    // checksum_bits suma los bits de cada double como uint64; como la suma es
    // conmutativa, dos arreglos con el mismo multiset dan el mismo resultado
    // sin importar el orden. Detecta perdidas o duplicaciones de elementos.
    const bool sorted        = is_sorted_ascending(temp, total);
    const std::uint64_t in_sum  = checksum_bits(data, total);
    const std::uint64_t out_sum = checksum_bits(temp, total);
    const bool same_values   = (in_sum == out_sum);

    const std::size_t ideal = total / num_threads;

    std::printf("\n--- Sample Sort paralelo (scatter/gather) ---\n");
    std::printf("Splitters       : %zu\n", stats.num_splitters);
    std::printf("Tiempo plan     : %.3f ms\n",   stats.plan_ms);
    std::printf("Tiempo conteo   : %.3f ms\n",   stats.count_ms);
    std::printf("Tiempo scatter  : %.3f ms\n",   stats.scatter_ms);
    std::printf("Tiempo orden    : %.3f ms\n",   stats.sort_ms);
    std::printf("Gather          : implicito (cubetas contiguas)\n");
    std::printf("Tiempo pipeline : %.3f ms\n",   stats.total_ms);
    std::printf("Conteo min/max  : %zu / %zu (ideal %zu)\n",
                stats.min_bucket, stats.max_bucket, ideal);

    // Solo imprimir el desglose por cubeta si hay pocas (evita spam en N grande).
    if (num_threads <= 16) {
        std::printf("Conteo cubetas  :");
        for (std::size_t b = 0; b < stats.counts.size(); ++b) {
            std::printf(" %zu", stats.counts[b]);
        }
        std::printf("\n");
    }

    std::printf("Verificacion    : %s\n", sorted ? "ordenado" : "ERROR");
    std::printf("Checksum bits   : %s\n",
                same_values ? "OK (mismos elementos)" : "ERROR");

    // --- Guardado opcional ---
    bool saved = true;
    if (output_path != nullptr) {
        const auto write_begin = std::chrono::steady_clock::now();
        saved = save_matrix(output_path, temp, rows, cols);
        const auto write_end = std::chrono::steady_clock::now();

        if (!saved) {
            std::fprintf(stderr, "main: no se pudo guardar '%s'\n",
                         output_path);
        } else {
            std::printf("Tiempo escritura: %.3f ms\n",
                        milliseconds(write_begin, write_end));
            std::printf("Salida          : %s\n", output_path);
        }
    }

    const auto finish = std::chrono::steady_clock::now();
    std::printf("Tiempo total    : %.3f ms\n",
                milliseconds(read_begin, finish));

    cleanup();
    return sorted && same_values && saved;
}


// ============================================================================
// Fase 1: lectura por chunks y verificacion binaria
// ============================================================================

// ---------------------------------------------------------------------------
// run_chunked_read - lee el archivo completo sin guardar todo en RAM
// ---------------------------------------------------------------------------
// Recorre el archivo con ChunkedTextReader en bloques de `chunk_mb` MB y
// acumula conteo, minimo, maximo y checksum de bits. Sirve para validar que
// el formato y el mmap funcionan sobre matrices grandes (ej: 16.8 GB).
//
// NOTA: La lectura es inherentemente secuencial. ChunkedTextReader parsea un
// caracter a la vez con un solo hilo. El cuello de botella es el ancho de
// banda de I/O + el parseo; paralelizar la lectura no mejora el rendimiento
// porque el disco es el recurso limitante en este patron de acceso.
//
// Parametros:
//   path      Archivo de entrada (.csv o .txt).
//   rows      Numero de filas (para saber cuantos valores esperar).
//   cols      Numero de columnas.
//   chunk_mb  Tamano del buffer de lectura en MB.
//   force     Si es true, omite el chequeo previo de RAM.
//
// Devuelve true si se leyeron exactamente rows * cols valores sin errores.
bool run_chunked_read(const char* path, std::size_t rows, std::size_t cols,
                      std::size_t chunk_mb, bool force) {
    if (chunk_mb == 0) {
        std::fprintf(stderr, "main: el chunk debe ser mayor que 0 MB\n");
        return false;
    }
    if (rows != 0 && cols > static_cast<std::size_t>(-1) / rows) {
        std::fprintf(stderr, "main: desbordamiento en filas * columnas\n");
        return false;
    }
    const std::size_t total = rows * cols;

    if (chunk_mb > static_cast<std::size_t>(-1) / (1024 * 1024)) {
        std::fprintf(stderr, "main: chunk demasiado grande\n");
        return false;
    }
    std::size_t chunk_values =
        (chunk_mb * 1024 * 1024) / sizeof(double);
    if (chunk_values == 0) chunk_values = 1;
    if (chunk_values > total) chunk_values = total;

    const std::size_t chunk_bytes = chunk_values * sizeof(double);
    // Buffer + overhead del parser. Se pide ~2x para ser conservador.
    if (!check_memory("modo lectura por chunks", 0, chunk_bytes * 2, force)) {
        return false;
    }

    ChunkedTextReader reader;
    const auto begin = std::chrono::steady_clock::now();
    if (!reader.open(path)) {
        return false;
    }

    std::vector<double> buffer;
    try {
        buffer.resize(chunk_values);
    } catch (const std::bad_alloc&) {
        std::fprintf(stderr,
                     "main: no hay memoria para un chunk de %.2f MB\n",
                     static_cast<double>(chunk_bytes) / (1024.0 * 1024.0));
        reader.close();
        return false;
    }

    const std::size_t total_chunks =
        (total + chunk_values - 1) / chunk_values;
    const std::size_t progress_step =
        total_chunks <= 32 ? 1 : (total_chunks / 10 == 0 ? 1
                                                         : total_chunks / 10);

    std::size_t read_total = 0;
    std::size_t chunk_index = 0;
    bool have_value = false;
    double min_value = 0.0;
    double max_value = 0.0;
    std::uint64_t checksum = 0;

    while (true) {
        std::size_t count = 0;
        if (!reader.next(buffer.data(), buffer.size(), count)) {
            reader.close();
            return false;
        }
        if (count == 0) break;

        for (std::size_t i = 0; i < count; ++i) {
            const double v = buffer[i];
            if (!have_value) {
                min_value = v;
                max_value = v;
                have_value = true;
            } else {
                if (v < min_value) min_value = v;
                if (v > max_value) max_value = v;
            }
        }
        checksum += checksum_bits(buffer.data(), count);
        read_total += count;
        ++chunk_index;

        if (chunk_index % progress_step == 0 || read_total == total) {
            std::printf("Chunk %zu/%zu: %zu valores acumulados (%.1f%%)\n",
                        chunk_index, total_chunks, read_total,
                        total > 0
                            ? 100.0 * static_cast<double>(read_total) /
                                  static_cast<double>(total)
                            : 0.0);
            std::fflush(stdout);
        }
    }

    const std::size_t bytes_read = reader.bytes_read();
    reader.close();
    const auto end = std::chrono::steady_clock::now();
    const double elapsed_ms = milliseconds(begin, end);

    std::printf("\n--- Lectura por chunks (mmap) ---\n");
    std::printf("Archivo         : %s\n", path);
    std::printf("Dimensiones     : %zu x %zu\n", rows, cols);
    std::printf("Valores leidos  : %zu (esperados %zu)\n", read_total, total);
    std::printf("Bytes leidos    : %zu\n", bytes_read);
    std::printf("Chunks          : %zu (%.2f MB por chunk)\n", total_chunks,
                static_cast<double>(chunk_bytes) / (1024.0 * 1024.0));
    std::printf("Tiempo lectura  : %.3f ms\n", elapsed_ms);
    std::printf("Rendimiento     : %.2f M valores/s\n",
                elapsed_ms > 0.0
                    ? (static_cast<double>(read_total) / (elapsed_ms / 1000.0)) /
                          1e6
                    : 0.0);
    std::printf("Min / Max       : %g / %g\n", min_value, max_value);
    std::printf("Checksum bits   : %llu\n",
                static_cast<unsigned long long>(checksum));
    std::printf("Verificacion    : %s\n",
                read_total == total ? "OK (todos los valores)" : "ERROR");

    if (read_total != total) {
        std::fprintf(stderr,
                     "main: se esperaban %zu valores y se leyeron %zu\n", total,
                     read_total);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// run_verify_bin - verifica que un .bin generado este ordenado (por chunks)
// ---------------------------------------------------------------------------
// Lee el archivo por chunks y verifica que cada par consecutivo sea <= al
// siguiente. No carga todo en RAM. Las dimensiones se leen de la cabecera
// MATBIN1 del propio archivo cuando rows == 0 y cols == 0.
//
// Parametros:
//   path   Ruta del archivo .bin a verificar.
//   rows   Filas esperadas (0 = leer de la cabecera del .bin).
//   cols   Columnas esperadas (0 = leer de la cabecera del .bin).
//
// Devuelve true si el archivo tiene formato correcto y esta ordenado.
bool run_verify_bin(const char* path, std::size_t rows, std::size_t cols) {
    BinVerifyStats stats;
    const auto begin = std::chrono::steady_clock::now();
    const bool ok = verify_bin(path, rows, cols, stats);
    const auto end = std::chrono::steady_clock::now();
    const double elapsed_ms = milliseconds(begin, end);

    std::printf("\n--- Verificacion binaria ---\n");
    std::printf("Archivo         : %s\n", path);
    std::printf("Dimensiones     : %zu x %zu\n", stats.rows, stats.cols);
    std::printf("Valores leidos  : %zu\n", stats.count);
    std::printf("Min / Max       : %g / %g\n", stats.min_value,
                stats.max_value);
    std::printf("Checksum bits   : %llu\n",
                static_cast<unsigned long long>(stats.checksum));
    std::printf("Tiempo lectura  : %.3f ms\n", elapsed_ms);
    std::printf("Verificacion    : %s\n", ok ? "ordenado" : "ERROR");

    return ok;
}

}  // namespace

// ============================================================================
// main - seleccion del modo y validacion de argumentos
// ============================================================================
// La matriz de entrada es FIJA: "matriz_a.txt" (40,000 x 40,000).
// Los argumentos son minimos:
//
//   ./ordenar_matriz <chunk_mb>
//       Modo lectura: valida formato y conteo sin ordenar ni guardar.
//       chunk_mb es el tamano del bloque de lectura en MB.
//
//   ./ordenar_matriz <chunk_mb> --hilos <N> [salida]
//       Sample Sort paralelo con N hilos/cubetas. Produce orden global.
//       salida es opcional: .csv, .txt o .bin (recomendado .bin, mas rapido).
//
//   ./ordenar_matriz --verificar-bin <archivo.bin>
//       Verifica que un .bin generado este globalmente ordenado.
//       Toma las dimensiones de la cabecera MATBIN1 del archivo.
//
// Opciones globales:
//   --forzar   Omite el chequeo previo de RAM y /dev/shm.
//
// NOTA DE LECTURA: la lectura del archivo de entrada es siempre secuencial
// (un hilo). El paralelismo comienza despues de cargar los datos en memoria.
int main(int argc, char** argv) {
    // --- Forma corta: verificar un .bin sin especificar chunk_mb ---
    // La forma "ordenar_matriz --verificar-bin archivo.bin" es un atajo;
    // las dimensiones se leen de la cabecera MATBIN1 del propio archivo.
    if (argc == 3 && std::strcmp(argv[1], "--verificar-bin") == 0) {
        return run_verify_bin(argv[2], 0, 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // Se necesita al menos: nombre del programa + chunk_mb.
    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    // --- Parsear chunk_mb (primer argumento obligatorio) ---
    std::size_t chunk_mb = 0;
    if (!parse_size(argv[1], chunk_mb)) {
        std::fprintf(stderr,
                     "main: chunk_mb debe ser un entero positivo (ej: 64)\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (chunk_mb > static_cast<std::size_t>(-1) / (1024 * 1024)) {
        std::fprintf(stderr, "main: chunk_mb demasiado grande\n");
        return EXIT_FAILURE;
    }
    // Convertir MB a bytes para pasarlo a las funciones internas.
    const std::size_t chunk_bytes = chunk_mb * 1024 * 1024;

    // --- Parsear argumentos opcionales a partir de argv[2] ---
    const char* output_path = nullptr; // ruta de salida, solo con --hilos
    std::size_t num_threads = 0;       // cantidad de hilos/cubetas
    bool force = false;                // bandera --forzar

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hilos") == 0) {
            // --hilos consume el siguiente argumento como cantidad de hilos.
            if (i + 1 >= argc || !parse_size(argv[++i], num_threads)) {
                std::fprintf(stderr,
                             "main: --hilos requiere un entero positivo (N >= 2)\n");
                return EXIT_FAILURE;
            }
        } else if (std::strcmp(argv[i], "--verificar-bin") == 0) {
            // Si se pasa --verificar-bin dentro del flujo normal, el
            // siguiente argumento es el archivo .bin a verificar.
            if (i + 1 >= argc) {
                std::fprintf(stderr,
                             "main: --verificar-bin requiere la ruta del archivo\n");
                return EXIT_FAILURE;
            }
            // Usar la forma corta directamente: leer dims de la cabecera.
            return run_verify_bin(argv[++i], 0, 0) ? EXIT_SUCCESS
                                                   : EXIT_FAILURE;
        } else if (std::strcmp(argv[i], "--forzar") == 0) {
            force = true;
        } else if (output_path == nullptr) {
            // El primer argumento libre se interpreta como ruta de salida.
            output_path = argv[i];
        } else {
            std::fprintf(stderr,
                         "main: argumento no reconocido '%s'\n", argv[i]);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // --- Validar combinaciones invalidas ---
    if (num_threads == 1) {
        // Con 1 hilo Sample Sort necesita al menos 2 cubetas.
        std::fprintf(stderr,
                     "main: --hilos requiere N >= 2 (con 1 hilo no hay "
                     "paralelismo util en el Sort)\n");
        return EXIT_FAILURE;
    }
    if (output_path != nullptr && num_threads == 0) {
        // La ruta de salida no tiene sentido sin el modo --hilos.
        std::fprintf(stderr,
                     "main: la ruta de salida solo se acepta con --hilos\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    // --- Usar la matriz fija como entrada ---
    // Las constantes kMatrizPath, kMatrizRows y kMatrizCols estan definidas
    // al inicio de este archivo y corresponden a "matriz_a.txt" (40000x40000).
    const char*       path = kMatrizPath;
    const std::size_t rows = kMatrizRows;
    const std::size_t cols = kMatrizCols;

    // --- Despachar al modo correspondiente ---
    if (num_threads > 0) {
        // Modo --hilos: Sample Sort paralelo con orden global.
        // num_threads define tanto el numero de cubetas como el de workers.
        // Produce el arreglo globalmente ordenado y opcionalmente lo guarda.
        return run_parallel(path, rows, cols, num_threads, output_path,
                            chunk_bytes, force)
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }

    // Modo por defecto: solo lectura y validacion, sin ordenar ni guardar.
    // Util para verificar que "matriz_a.txt" se parsea correctamente.
    return run_chunked_read(path, rows, cols, chunk_mb, force) ? EXIT_SUCCESS
                                                               : EXIT_FAILURE;
}
