// ============================================================================
// csv_parser.hpp - Modulo de lectura manual de un CSV/TXT numerico
//
// Proyecto 1 - Topicos de HPC
// Sprint 1: lectura con fread() y parseo manual (sin librerias de CSV).
// Sprint 2: parse_csv_into() escribe directo en un puntero de memoria, para
// que el destino pueda ser el mmap compartido en lugar de un vector local.
// Fase 1: load_csv() y load_csv_into() leen por chunks con mmap a traves de
// ChunkedTextReader; ya no se carga el archivo completo de texto en heap.
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

// Lee un archivo de numeros separados por comas, espacios o tabs, sin
// encabezado. rows y cols se conocen de antemano. La lectura es por chunks con
// mmap: el archivo de texto nunca se carga completo en heap.
// chunk_bytes controla el tamano del bloque de lectura (0 usa 64 MB).
// Devuelve true solo si se leyeron exactamente rows * cols valores.
bool load_csv(const char* path, std::size_t rows, std::size_t cols,
              std::size_t chunk_bytes, Matrix& out);

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
// Lee por chunks directamente en `dst`, sin buffer intermedio del texto.
// chunk_bytes controla el tamano del bloque de lectura (0 usa 64 MB).
// Devuelve true solo si se leyeron exactamente `capacity` valores.
bool load_csv_into(const char* path, double* dst, std::size_t capacity,
                   std::size_t chunk_bytes, std::size_t& written);

#endif  // CSV_PARSER_HPP
