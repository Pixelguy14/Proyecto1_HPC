// ============================================================================
// csv_parser.hpp - Modulo de lectura manual de un CSV numerico
//
// Proyecto 1 - Topicos de HPC (Sprint 1)
//
// No se usan librerias de CSV: el archivo se lee con fread(), los numeros se
// delimitan con un ciclo propio (equivalente a strtok) y se convierten con
// strtod() (la version segura de atof(), detecta errores).
// ============================================================================
#ifndef CSV_PARSER_HPP
#define CSV_PARSER_HPP

#include <cstddef> // std::size_t (filas, columnas, tamaños)
#include <vector> // std::vector<double> de Matrix para poder leer organizar la matriz

// Matriz en un solo bloque contiguo, almacenada por filas (row-major).
// std::vector<double> se usa solo como contenedor de memoria contigua:
// mas adelante sus datos se pueden compartir con hilos/procesos sin copiar.
struct Matrix {
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<double> data;  // data[r * cols + c]

    // Reserva el espacio exacto cuando ya se conocen las dimensiones
    // (1000x1000 en las pruebas; 10,000x10,000 en la version final).
    void reserve(std::size_t total) { data.reserve(total); }

    std::size_t size() const { return data.size(); }

    double& at(std::size_t r, std::size_t c) { return data[r * cols + c]; }
    const double& at(std::size_t r, std::size_t c) const { return data[r * cols + c]; }
};

// Lee un CSV de numeros separados por comas, sin encabezado.
// rows y cols se conocen de antemano.
// Devuelve true solo si se leyeron exactamente rows * cols valores.
bool load_csv(const char* path, std::size_t rows, std::size_t cols, Matrix& out);

// Nucleo del parser: trabaja sobre un buffer contiguo ya en memoria.
// Esta separado de load_csv() para reutilizarlo despues con mmap().
bool parse_csv_buffer(const char* buf, std::size_t size, std::size_t rows,
                      std::size_t cols, Matrix& out);

#endif  // CSV_PARSER_HPP
