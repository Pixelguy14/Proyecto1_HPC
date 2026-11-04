// ============================================================================
// csv_parser.hpp - Modulo de lectura manual de un CSV numerico
//
// Proyecto 1 - Topicos de HPC
// Sprint 1: lectura con fread() y parseo manual (sin librerias de CSV).
// Sprint 2: parse_csv_into() escribe directo en un puntero de memoria, para
// que el destino pueda ser el mmap compartido en lugar de un vector local.
// ============================================================================
#ifndef CSV_PARSER_HPP
#define CSV_PARSER_HPP

#include <cstddef>
#include <vector>

// Matriz en un solo bloque contiguo, almacenada por filas (row-major).
// std::vector<double> se usa solo como contenedor de memoria contigua.
struct Matrix {
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<double> data;  // data[r * cols + c]

    // Reserva el espacio exacto cuando ya se conocen las dimensiones.
    void reserve(std::size_t total) { data.reserve(total); }

    std::size_t size() const { return data.size(); }

    double& at(std::size_t r, std::size_t c) { return data[r * cols + c]; }
    const double& at(std::size_t r, std::size_t c) const {
        return data[r * cols + c];
    }
};

// Lee un CSV de numeros separados por comas, sin encabezado.
// rows y cols se conocen de antemano.
// Devuelve true solo si se leyeron exactamente rows * cols valores.
bool load_csv(const char* path, std::size_t rows, std::size_t cols, Matrix& out);

// Nucleo del parser sobre un buffer contiguo ya en memoria.
// Esta separado de load_csv() para reutilizarlo con mmap().
bool parse_csv_buffer(const char* buf, std::size_t size, std::size_t rows,
                      std::size_t cols, Matrix& out);

// Escribe los numeros del buffer directamente en dst (p. ej. memoria mmap).
// Solo escribe hasta `capacity` elementos; si el CSV trae mas, no desborda.
// `written` recibe cuantos valores se encontraron en total.
// Devuelve false solo si hay un valor con formato invalido.
bool parse_csv_into(const char* buf, std::size_t size, double* dst,
                    std::size_t capacity, std::size_t& written);

// Igual que load_csv(), pero el destino es un puntero ya reservado.
// Devuelve true solo si se leyeron exactamente `capacity` valores.
// (Para la version final, el buffer de entrada puede venir de mmap y se
//  llama directo a parse_csv_into(), sin este helper).
bool load_csv_into(const char* path, double* dst, std::size_t capacity,
                   std::size_t& written);

#endif  // CSV_PARSER_HPP
