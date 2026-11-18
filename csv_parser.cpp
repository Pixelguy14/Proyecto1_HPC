// ============================================================================
// csv_parser.cpp - Implementacion del parser manual de CSV
//
// Proyecto 1 - Topicos de HPC
// Sprint 1: lectura completa con fread().
// Sprint 2: parse_csv_into() escribe directo en un bloque de memoria.
// Fase 1: load_csv()/load_csv_into() leen por chunks con ChunkedTextReader
//         (mmap), sin cargar el archivo completo de texto en heap.
//
// Compilacion (junto con main.cpp):
//   g++ -O3 -std=c++17 -Wall -Wextra main.cpp csv_parser.cpp -o ordenar_matriz
// ============================================================================
#include "csv_parser.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "chunked_reader.hpp"

namespace {

// Separadores aceptados: coma, salto de linea y espacios.
inline bool is_separator(char c) {
    return c == ',' || c == '\n' || c == '\r' || c == ' ' || c == '\t';
}

// Valores por chunk por defecto si el llamador no especifica un tamano: 64 MB.
constexpr std::size_t kDefaultChunkBytes = 64u * 1024u * 1024u;

// Convierte bytes a cantidad de doubles, garantizando al menos un valor.
std::size_t chunk_values_from_bytes(std::size_t chunk_bytes) {
    if (chunk_bytes == 0) chunk_bytes = kDefaultChunkBytes;
    const std::size_t values = chunk_bytes / sizeof(double);
    return values == 0 ? 1 : values;
}

}  // namespace

bool parse_csv_into(const char* buf, std::size_t size, double* dst,
                    std::size_t capacity, std::size_t& written) {
    written = 0;
    if (dst == nullptr && capacity != 0) {
        std::fprintf(stderr, "parse_csv_into: destino nulo\n");
        return false;
    }
    if (buf == nullptr && size != 0) {
        std::fprintf(stderr, "parse_csv_into: buffer nulo\n");
        return false;
    }
    if (size == 0) return true;

    const char* p = buf;
    const char* const end = buf + size;
    char token[64];  // cualquier double cabe de sobra
    std::size_t count = 0;

    while (p < end) {
        // Ignorar comas, saltos de linea y espacios (equivale a strtok).
        while (p < end && is_separator(*p)) ++p;
        if (p >= end) break;

        // Delimitar el numero: desde p hasta el siguiente separador.
        const char* const start = p;
        while (p < end && !is_separator(*p)) ++p;
        const std::size_t len = static_cast<std::size_t>(p - start);

        if (len >= sizeof(token)) {
            std::fprintf(stderr,
                         "parse_csv_into: numero demasiado largo en el byte %zu\n",
                         static_cast<std::size_t>(start - buf));
            written = count;
            return false;
        }

        // Copia local para terminar el token en '\0' y usar strtod.
        // (strtod es atof() pero indica donde termino de convertir).
        std::memcpy(token, start, len);
        token[len] = '\0';

        char* next = nullptr;
        const double value = std::strtod(token, &next);
        if (next == token || *next != '\0') {
            std::fprintf(stderr,
                         "parse_csv_into: valor invalido \"%s\" en el byte %zu\n",
                         token, static_cast<std::size_t>(start - buf));
            written = count;
            return false;
        }

        // Si el CSV tuviera mas valores que la capacidad, se cuentan pero
        // no se escriben: nunca se desborda el mmap.
        if (count < capacity) {
            dst[count] = value;
        }
        ++count;
    }

    written = count;
    return true;
}

bool parse_csv_buffer(const char* buf, std::size_t size, std::size_t rows,
                      std::size_t cols, Matrix& out) {
    if (rows == 0 || cols == 0) {
        std::fprintf(stderr, "parse_csv_buffer: dimensiones invalidas (%zu x %zu)\n",
                     rows, cols);
        return false;
    }
    if (buf == nullptr && size != 0) {
        std::fprintf(stderr, "parse_csv_buffer: buffer nulo\n");
        return false;
    }

    const std::size_t expected = rows * cols;

    out.rows = rows;
    out.cols = cols;
    out.data.clear();
    out.reserve(expected);   // evita reallocaciones
    out.data.resize(expected);  // memoria contigua para parse_csv_into

    std::size_t written = 0;
    if (!parse_csv_into(buf, size, out.data.data(), expected, written)) {
        out.data.clear();
        return false;
    }
    if (written != expected) {
        std::fprintf(stderr,
                     "parse_csv_buffer: se esperaban %zu valores (%zu x %zu) y se leyeron %zu\n",
                     expected, rows, cols, written);
        out.data.clear();
        return false;
    }
    return true;
}

bool load_csv(const char* path, std::size_t rows, std::size_t cols,
              std::size_t chunk_bytes, Matrix& out) {
    if (rows == 0 || cols == 0) {
        std::fprintf(stderr, "load_csv: dimensiones invalidas (%zu x %zu)\n",
                     rows, cols);
        return false;
    }
    if (cols > static_cast<std::size_t>(-1) / rows) {
        std::fprintf(stderr, "load_csv: desbordamiento en filas * columnas\n");
        return false;
    }

    const std::size_t expected = rows * cols;

    out.rows = rows;
    out.cols = cols;
    out.data.clear();
    try {
        out.data.resize(expected);  // memoria contigua para load_csv_into
    } catch (const std::bad_alloc&) {
        std::fprintf(stderr,
                     "load_csv: no hay memoria para %zu valores (%.2f GB)\n",
                     expected,
                     static_cast<double>(expected * sizeof(double)) /
                         (1024.0 * 1024.0 * 1024.0));
        out.data.clear();
        return false;
    }

    std::size_t written = 0;
    if (!load_csv_into(path, out.data.data(), expected, chunk_bytes, written)) {
        out.data.clear();
        out.rows = 0;
        out.cols = 0;
        return false;
    }
    return true;
}

bool load_csv_into(const char* path, double* dst, std::size_t capacity,
                   std::size_t chunk_bytes, std::size_t& written) {
    written = 0;
    if (dst == nullptr && capacity != 0) {
        std::fprintf(stderr, "load_csv_into: destino nulo\n");
        return false;
    }

    ChunkedTextReader reader;
    if (!reader.open(path)) return false;

    // Lectura directa al destino en bloques de `chunk_values`: no hay buffer
    // intermedio con todo el texto. ChunkedTextReader consume tokens completos,
    // asi que un numero puede quedar partido entre dos llamadas sin problema.
    const std::size_t chunk_values = chunk_values_from_bytes(chunk_bytes);

    std::size_t offset = 0;
    while (offset < capacity) {
        std::size_t want = capacity - offset;
        if (want > chunk_values) want = chunk_values;

        std::size_t count = 0;
        if (!reader.next(dst + offset, want, count)) {
            reader.close();
            written = offset;
            return false;
        }
        if (count == 0) break;  // fin del archivo
        offset += count;
    }
    written = offset;

    // Debe haber exactamente `capacity` valores: un valor extra es un error.
    if (offset == capacity) {
        double extra = 0.0;
        std::size_t count = 0;
        if (!reader.next(&extra, 1, count)) {
            reader.close();
            return false;
        }
        if (count != 0) {
            std::fprintf(stderr,
                         "load_csv_into: hay mas de %zu valores en '%s'\n",
                         capacity, path);
            reader.close();
            return false;
        }
    }

    reader.close();

    if (written != capacity) {
        std::fprintf(stderr,
                     "load_csv_into: se esperaban %zu valores y se encontraron %zu\n",
                     capacity, written);
        return false;
    }
    return true;
}
