// ============================================================================
// Proyecto 1 - Topicos de HPC
// Sprint 1: lectura manual de CSV + Merge Sort propio + guardado en archivo
//
// Equipo: Francisco Javier Meléndez Ruiz y José Julian Sierra Alvarez
//
// Compilacion:
//   make
//   (o: g++ -O3 -std=c++17 -Wall -Wextra main.cpp csv_parser.cpp
//        merge_sort.cpp file_io.cpp -o leer_csv)
//
// Uso:
//   ./leer_csv <archivo.csv> <filas> <columnas>
//   ./leer_csv <archivo.csv> <filas> <columnas> <salida.csv>
//
// Ejemplos:
//   ./leer_csv matriz_1000x1000.csv 1000 1000
//   ./leer_csv matriz_1000x1000.csv 1000 1000 ordenada.csv
//
// Generar los datos de prueba:
//   python3 tools/generador_csv.py -f 1000 -c 1000
// ============================================================================
#include "csv_parser.hpp"
#include "file_io.hpp"
#include "merge_sort.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

void usage(const char* prog) {
    std::fprintf(stderr,
                 "Uso: %s <archivo.csv> <filas> <columnas> [salida.csv]\n"
                 "  Sin salida.csv solo lee y muestra un resumen.\n"
                 "  Con salida.csv ordena (Merge Sort) y guarda el resultado.\n"
                 "Ejemplo: %s matriz_1000x1000.csv 1000 1000 ordenada.csv\n",
                 prog, prog);
}

bool parse_size(const char* text, std::size_t& value) {
    char* end = nullptr;
    errno = 0;
    const unsigned long long v = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || v == 0) return false;
    value = static_cast<std::size_t>(v);
    return true;
}

double milliseconds(std::chrono::steady_clock::time_point begin,
                    std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* path = argv[1];
    const bool sort_and_save = (argc == 5);
    const char* output_path = sort_and_save ? argv[4] : nullptr;

    std::size_t rows = 0;
    std::size_t cols = 0;
    if (!parse_size(argv[2], rows) || !parse_size(argv[3], cols)) {
        std::fprintf(stderr, "main: filas y columnas deben ser enteros positivos\n");
        return EXIT_FAILURE;
    }

    Matrix matrix;

    const auto read_begin = std::chrono::steady_clock::now();
    const bool loaded = load_csv(path, rows, cols, matrix);
    const auto read_end = std::chrono::steady_clock::now();

    if (!loaded) {
        std::fprintf(stderr, "main: no se pudo leer '%s'\n", path);
        return EXIT_FAILURE;
    }

    const double read_ms = milliseconds(read_begin, read_end);

    // Resumen y verificacion rapida de los datos.
    double minimo = matrix.data[0];
    double maximo = matrix.data[0];
    long double suma = 0.0L;
    for (const double v : matrix.data) {
        if (v < minimo) minimo = v;
        if (v > maximo) maximo = v;
        suma += v;
    }

    std::printf("Archivo         : %s\n", path);
    std::printf("Dimensiones     : %zu x %zu\n", matrix.rows, matrix.cols);
    std::printf("Valores leidos  : %zu\n", matrix.size());
    std::printf("Tiempo lectura  : %.3f ms\n", read_ms);
    std::printf("Rendimiento     : %.2f M valores/s\n",
                read_ms > 0.0
                    ? (static_cast<double>(matrix.size()) / (read_ms / 1000.0)) / 1e6
                    : 0.0);
    std::printf("Esquinas        : [0][0]=%g  [0][%zu]=%g  [%zu][0]=%g  [%zu][%zu]=%g\n",
                matrix.at(0, 0),
                matrix.cols - 1, matrix.at(0, matrix.cols - 1),
                matrix.rows - 1, matrix.at(matrix.rows - 1, 0),
                matrix.rows - 1, matrix.cols - 1,
                matrix.at(matrix.rows - 1, matrix.cols - 1));
    std::printf("Min / Max       : %g / %g\n", minimo, maximo);
    std::printf("Suma (checksum) : %Lg\n", suma);

    if (!sort_and_save) {
        return EXIT_SUCCESS;
    }

    // ------------------------------------------------------------------------
    // Sprint 1 - Integracion: ordenar, verificar y guardar el resultado.
    // ------------------------------------------------------------------------
    const auto sort_begin = std::chrono::steady_clock::now();
    merge_sort(matrix.data.data(), matrix.size());
    const auto sort_end = std::chrono::steady_clock::now();

    const double sort_ms = milliseconds(sort_begin, sort_end);
    const bool sorted =
        is_sorted_ascending(matrix.data.data(), matrix.size());

    std::printf("\n--- Ordenamiento ---\n");
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
        return EXIT_FAILURE;
    }

    const auto write_begin = std::chrono::steady_clock::now();
    const bool saved = save_csv(output_path, matrix.data.data(), matrix.rows,
                                matrix.cols);
    const auto write_end = std::chrono::steady_clock::now();

    if (!saved) {
        std::fprintf(stderr, "main: no se pudo guardar '%s'\n", output_path);
        return EXIT_FAILURE;
    }

    std::printf("Tiempo escritura: %.3f ms\n",
                milliseconds(write_begin, write_end));
    std::printf("Tiempo total    : %.3f ms\n",
                milliseconds(read_begin, write_end));
    std::printf("Salida          : %s\n", output_path);

    return EXIT_SUCCESS;
}
