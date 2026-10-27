// ============================================================================
// Proyecto 1 - Topicos de HPC
// Sprint 1: lectura manual de un CSV numerico (sin librerias de CSV)
//
// Equipo: Francisco Javier Meléndez Ruiz y José Julian Sierra Alvarez
//
// Compilacion:
//   g++ -O2 -std=c++17 -Wall -Wextra main.cpp csv_parser.cpp -o leer_csv
//
// Uso:
//   ./leer_csv <archivo.csv> <filas> <columnas>
//   ./leer_csv matriz_1000x1000.csv 1000 1000
//
// Generar los datos de prueba:
//   python3 generador_csv.py
// ============================================================================
#include "csv_parser.hpp"

#include <cerrno> // validar desbordamiento de los argumentos
#include <chrono> // medir el tiempo de lectura
#include <cstdio> // printf y fprintf
#include <cstdlib> // strtoull, EXIT_SUCCESS/FAILURE

namespace {

void usage(const char* prog) {
    std::fprintf(stderr,
                 "Uso: %s <archivo.csv> <filas> <columnas>\n"
                 "Ejemplo: %s matriz_1000x1000.csv 1000 1000\n",
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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
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

    Matrix matrix;

    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = load_csv(path, rows, cols, matrix);
    const auto t1 = std::chrono::steady_clock::now();

    if (!ok) { // mensaje de error de la funcion de cargar el csv
        std::fprintf(stderr, "main: no se pudo leer '%s'\n", path);
        return EXIT_FAILURE;
    }

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double segundos = ms / 1000.0;

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
    std::printf("Tiempo lectura  : %.3f ms\n", ms);
    std::printf("Rendimiento     : %.2f M valores/s\n",
                segundos > 0.0 ? (static_cast<double>(matrix.size()) / segundos) / 1e6 : 0.0);
    std::printf("Esquinas        : [0][0]=%g  [0][%zu]=%g  [%zu][0]=%g  [%zu][%zu]=%g\n",
                matrix.at(0, 0),
                matrix.cols - 1, matrix.at(0, matrix.cols - 1),
                matrix.rows - 1, matrix.at(matrix.rows - 1, 0),
                matrix.rows - 1, matrix.cols - 1, matrix.at(matrix.rows - 1, matrix.cols - 1));
    std::printf("Min / Max       : %g / %g\n", minimo, maximo);
    std::printf("Suma (checksum) : %Lg\n", suma);

    return EXIT_SUCCESS;
}
