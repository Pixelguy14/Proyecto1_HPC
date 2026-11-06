// ============================================================================
// Proyecto 1 - Topicos de HPC
// Sprint 1: lectura manual de CSV + Merge Sort propio + guardado en archivo
// Sprint 2: modo --hilos con mmap compartido, pthreads y mutex
//
// Equipo: Francisco Javier Melendez Ruiz y Jose Julian Sierra Alvarez
//
// Este archivo es el punto de entrada y el "pegamento" entre modulos:
//   - csv_parser:    convierte el CSV en doubles.
//   - merge_sort:    ordena el arreglo (secuencial o por rangos).
//   - sort_worker:   ordena el bloque de un hilo.
//   - shared_memory: crea los segmentos mmap y el bloque de control.
//   - parallel_sort: crea y espera los hilos.
//   - file_io:       verifica el orden y guarda el resultado.
//
// Compilacion:
//   make
//   (o: g++ -O3 -std=c++17 -Wall -Wextra main.cpp csv_parser.cpp
//        merge_sort.cpp file_io.cpp sort_worker.cpp parallel_sort.cpp
//        shared_memory.cpp -o leer_csv -lrt -lpthread)
//
// Uso:
//   ./leer_csv <archivo.csv> <filas> <columnas>
//   ./leer_csv <archivo.csv> <filas> <columnas> <salida.csv>
//   ./leer_csv <archivo.csv> <filas> <columnas> --hilos <N>
//
// Ejemplos:
//   ./leer_csv matriz_1000x1000.csv 1000 1000
//   ./leer_csv matriz_1000x1000.csv 1000 1000 ordenada.csv
//   ./leer_csv matriz_1000x1000.csv 1000 1000 --hilos 4
//
// Generar los datos de prueba:
//   python3 tools/generador_csv.py -f 1000 -c 1000
// ============================================================================
#include "csv_parser.hpp"
#include "file_io.hpp"
#include "merge_sort.hpp"
#include "parallel_sort.hpp"
#include "shared_memory.hpp"
#include "sort_worker.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Utilidades de la linea de comandos y de medicion
// ============================================================================
namespace {

// ---------------------------------------------------------------------------
// usage - contrato de la interfaz de linea de comandos
// ---------------------------------------------------------------------------
// Se imprime en stderr porque un uso incorrecto es un error, no una salida
// normal del programa.
void usage(const char* prog) {
    std::fprintf(stderr,
                 "Uso:\n"
                 "  %s <archivo.csv> <filas> <columnas>                solo lectura\n"
                 "  %s <archivo.csv> <filas> <columnas> <salida.csv>   secuencial + orden + guardado\n"
                 "  %s <archivo.csv> <filas> <columnas> --hilos <N>    mmap + hilos (bloques)\n"
                 "\n"
                 "Ejemplos:\n"
                 "  %s matriz_1000x1000.csv 1000 1000\n"
                 "  %s matriz_1000x1000.csv 1000 1000 ordenada.csv\n"
                 "  %s matriz_1000x1000.csv 1000 1000 --hilos 4\n",
                 prog, prog, prog, prog, prog, prog);
}

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
// Flujo secuencial (Sprint 1)
// ============================================================================

// ---------------------------------------------------------------------------
// run_sequential - leer, (opcionalmente) ordenar y guardar
// ---------------------------------------------------------------------------
// Si output_path == nullptr solo se lee y se muestra el resumen. Si se da una
// ruta de salida, el flujo completo es:
//
//   load_csv -> print_summary -> merge_sort -> verificar -> save_csv
//
// Parametros:
//   path         CSV de entrada.
//   rows, cols   Dimensiones conocidas de antemano.
//   output_path  CSV de salida, o nullptr para modo solo lectura.
//
// Devuelve true si todas las etapas solicitadas terminaron correctamente.
bool run_sequential(const char* path, std::size_t rows, std::size_t cols,
                    const char* output_path) {
    // En modo secuencial el arreglo vive en un vector local (no en mmap).
    Matrix matrix;

    const auto read_begin = std::chrono::steady_clock::now();
    const bool loaded = load_csv(path, rows, cols, matrix);
    const auto read_end = std::chrono::steady_clock::now();

    if (!loaded) {
        std::fprintf(stderr, "main: no se pudo leer '%s'\n", path);
        return false;
    }

    const double read_ms = milliseconds(read_begin, read_end);

    // El resumen se imprime antes de ordenar: asi el checksum corresponde a la
    // entrada original y las esquinas muestran el orden real del CSV.
    print_summary(path, matrix.data.data(), rows, cols, read_ms);

    // Modo solo lectura: no hay nada mas que hacer.
    if (output_path == nullptr) {
        return true;
    }

    const auto sort_begin = std::chrono::steady_clock::now();
    merge_sort(matrix.data.data(), matrix.size());
    const auto sort_end = std::chrono::steady_clock::now();

    const double sort_ms = milliseconds(sort_begin, sort_end);

    // La verificacion recorre todo el arreglo y confirma data[i-1] <= data[i].
    // Es O(n) y evita guardar un resultado incorrecto.
    const bool sorted = is_sorted_ascending(matrix.data.data(), matrix.size());

    std::printf("\n--- Ordenamiento secuencial ---\n");
    std::printf("Tiempo orden    : %.3f ms\n", sort_ms);
    std::printf("Rendimiento     : %.2f M valores/s\n",
                sort_ms > 0.0
                    ? (static_cast<double>(matrix.size()) / (sort_ms / 1000.0)) / 1e6
                    : 0.0);
    std::printf("Verificacion    : %s\n", sorted ? "ORDENADO" : "ERROR");
    std::printf("Primer / Ultimo : %g / %g\n", matrix.data.front(),
                matrix.data.back());

    if (!sorted) {
        std::fprintf(stderr, "main: el arreglo no quedo ordenado\n");
        return false;
    }

    const auto write_begin = std::chrono::steady_clock::now();
    const bool saved = save_csv(output_path, matrix.data.data(), matrix.rows,
                                matrix.cols);
    const auto write_end = std::chrono::steady_clock::now();

    if (!saved) {
        std::fprintf(stderr, "main: no se pudo guardar '%s'\n", output_path);
        return false;
    }

    std::printf("Tiempo escritura: %.3f ms\n",
                milliseconds(write_begin, write_end));

    // Tiempo total de pared desde el inicio de la lectura hasta el final de la
    // escritura; incluye lectura, ordenamiento, verificacion y guardado.
    std::printf("Tiempo total    : %.3f ms\n",
                milliseconds(read_begin, write_end));
    std::printf("Salida          : %s\n", output_path);
    return true;
}

// ============================================================================
// Flujo con hilos y memoria compartida (Sprint 2)
// ============================================================================

// ---------------------------------------------------------------------------
// run_parallel - mmap + pthreads, ordenamiento por bloques
// ---------------------------------------------------------------------------
// Recursos que se crean y deben liberarse en orden inverso:
//   1. Segmento de datos  (/proyecto1_hpc_data)
//   2. Segmento de control (/proyecto1_hpc_ctrl)
//   3. Mutex dentro del segmento de control
//
// El flujo es:
//   shared_create(data) -> shared_create(ctrl) -> shared_control_init()
//     -> load_csv_into(data) -> parallel_sort_chunks()
//     -> verificar cada bloque -> limpieza
//
// Limitacion deliberada: cada hilo ordena su bloque, pero el arreglo completo
// no queda globalmente ordenado. Eso requiere el scatter/gather del Sprint 3.
//
// Parametros:
//   path         CSV de entrada.
//   rows, cols   Dimensiones de la matriz.
//   num_threads  Cantidad de hilos/bloques (> 0).
//
// Devuelve true solo si se leyo todo, se crearon todos los hilos, todos
// marcaron su avance y todos los bloques quedaron ordenados.
bool run_parallel(const char* path, std::size_t rows, std::size_t cols,
                  std::size_t num_threads) {
    const std::size_t total = rows * cols;
    const std::size_t data_bytes = total * sizeof(double);

    // Los nombres son fijos: solo puede correr una instancia de este modo a la
    // vez. shared_create() elimina cualquier segmento viejo con el mismo nombre.
    SharedMemory data_shm;
    if (!shared_create(data_shm, "/proyecto1_hpc_data", data_bytes)) {
        return false;
    }

    SharedMemory ctrl_shm;
    if (!shared_create(ctrl_shm, "/proyecto1_hpc_ctrl",
                       sizeof(SharedControl))) {
        // Si falla el segundo segmento, el primero ya existe y debe liberarse.
        shared_release(data_shm, true);
        return false;
    }

    SharedControl* control = static_cast<SharedControl*>(ctrl_shm.addr);
    if (!shared_control_init(control, num_threads)) {
        // El mutex no quedo inicializado, pero los segmentos si: liberar ambos.
        shared_release(ctrl_shm, true);
        shared_release(data_shm, true);
        return false;
    }

    // El arreglo vive directamente en el segmento mapeado. load_csv_into()
    // escribe ahi sin copiar a un vector intermedio.
    double* data = static_cast<double*>(data_shm.addr);

    const auto read_begin = std::chrono::steady_clock::now();
    std::size_t written = 0;
    const bool loaded = load_csv_into(path, data, total, written);
    const auto read_end = std::chrono::steady_clock::now();

    if (!loaded || written != total) {
        std::fprintf(stderr, "main: no se pudo leer '%s' en la memoria compartida\n",
                     path);
        // Limpieza completa ante un error de lectura.
        shared_control_destroy(control);
        shared_release(ctrl_shm, true);
        shared_release(data_shm, true);
        return false;
    }

    const double read_ms = milliseconds(read_begin, read_end);

    std::printf("Modo            : hilos (mmap + pthreads)\n");
    std::printf("Memoria compart.: %.2f MB\n",
                static_cast<double>(data_bytes) / (1024.0 * 1024.0));
    std::printf("Hilos           : %zu\n", num_threads);
    print_summary(path, data, rows, cols, read_ms);

    // parallel_sort_chunks() crea los hilos, espera el join y llena `stats`.
    ParallelSortStats stats;
    const bool threads_ok =
        parallel_sort_chunks(data, total, num_threads, control, stats);

    // Verificacion por bloque: se repite el mismo reparto que uso
    // parallel_sort_chunks() para revisar cada [begin, end) por separado.
    // No se verifica el orden global porque este modo todavia no lo produce.
    std::size_t sorted_chunks = 0;
    for (std::size_t i = 0; i < num_threads; ++i) {
        std::size_t begin = 0;
        std::size_t end = 0;
        worker_range(total, num_threads, i, begin, end);
        if (is_sorted_ascending(data + begin, end - begin)) {
            ++sorted_chunks;
        }
    }

    std::printf("\n--- Ordenamiento por bloques (hilos) ---\n");
    std::printf("Tiempo orden    : %.3f ms\n", stats.sort_ms);
    std::printf("Hilos creados   : %zu/%zu\n", stats.created_workers,
                num_threads);
    std::printf("Workers done    : %zu/%zu\n", stats.completed_workers,
                num_threads);
    std::printf("Bloques ord.    : %zu/%zu\n", sorted_chunks, num_threads);
    std::printf("Nota            : cada bloque queda ordenado; el orden global\n");
    std::printf("                  se implementa en el Sprint 3 (scatter/gather).\n");

    const bool ok = threads_ok && stats.completed_workers == num_threads &&
                    sorted_chunks == num_threads;

    // Orden inverso al de creacion: primero el mutex, luego su segmento y al
    // final el segmento de datos.
    shared_control_destroy(control);
    shared_release(ctrl_shm, true);
    shared_release(data_shm, true);
    return ok;
}

}  // namespace

// ============================================================================
// main - seleccion del modo y validacion de argumentos
// ============================================================================
// Argumentos obligatorios: archivo.csv, filas, columnas.
// Opcionales:
//   - una ruta de salida (solo modo secuencial),
//   - "--hilos N" (modo paralelo, sin salida global por ahora).
//
// No se permite combinar salida con --hilos: el modo paralelo actual ordena
// bloques, no el arreglo completo, asi que guardar el resultado seria enganoso.
int main(int argc, char** argv) {
    if (argc < 4) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* path = argv[1];
    std::size_t rows = 0;
    std::size_t cols = 0;
    if (!parse_size(argv[2], rows) || !parse_size(argv[3], cols)) {
        std::fprintf(stderr, "main: filas y columnas deben ser enteros positivos\n");
        return EXIT_FAILURE;
    }

    const char* output_path = nullptr;
    std::size_t num_threads = 0;

    // Los argumentos opcionales se pueden dar en cualquier orden. El primero
    // que no sea --hilos se interpreta como ruta de salida; un segundo valor
    // suelto es un error.
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hilos") == 0) {
            // --hilos consume el siguiente argumento como cantidad de hilos.
            if (i + 1 >= argc || !parse_size(argv[++i], num_threads)) {
                std::fprintf(stderr, "main: --hilos requiere un entero positivo\n");
                return EXIT_FAILURE;
            }
        } else if (output_path == nullptr) {
            output_path = argv[i];
        } else {
            std::fprintf(stderr, "main: argumento no reconocido '%s'\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    // num_threads > 0 implica que el usuario paso --hilos (parse_size rechaza 0).
    if (num_threads > 0) {
        if (output_path != nullptr) {
            std::fprintf(stderr,
                         "main: el modo --hilos no genera salida global; "
                         "use el modo secuencial o espere al Sprint 3\n");
            return EXIT_FAILURE;
        }
        return run_parallel(path, rows, cols, num_threads) ? EXIT_SUCCESS
                                                           : EXIT_FAILURE;
    }

    return run_sequential(path, rows, cols, output_path) ? EXIT_SUCCESS
                                                         : EXIT_FAILURE;
}
