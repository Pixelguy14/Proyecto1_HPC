// ============================================================================
// csv_parser.cpp - Implementacion del parser manual de CSV
//
// Proyecto 1 - Topicos de HPC
// Sprint 1: lectura completa con fread().
// Sprint 2: parse_csv_into() escribe directo en un bloque de memoria.
//
// Compilacion (junto con main.cpp):
//   g++ -O3 -std=c++17 -Wall -Wextra main.cpp csv_parser.cpp -o leer_csv
// ============================================================================
#include "csv_parser.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Separadores aceptados: coma, salto de linea y espacios.
inline bool is_separator(char c) {
    return c == ',' || c == '\n' || c == '\r' || c == ' ' || c == '\t';
}

// Lee el archivo completo en un buffer contiguo.
// Se aísla aqui para que load_csv() y load_csv_into() compartan el mismo
// manejo de errores. En la version con mmap este paso no hace falta.
bool read_file(const char* path, std::vector<char>& buffer) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        std::fprintf(stderr, "load_csv: no se pudo abrir '%s': %s\n", path,
                     std::strerror(errno));
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fprintf(stderr, "load_csv: fseek fallo en '%s'\n", path);
        std::fclose(file);
        return false;
    }
    const long length = std::ftell(file);
    if (length < 0) {
        std::fprintf(stderr, "load_csv: no se pudo obtener el tamano de '%s'\n",
                     path);
        std::fclose(file);
        return false;
    }
    std::rewind(file);

    buffer.resize(static_cast<std::size_t>(length));
    std::size_t leidos = 0;
    if (!buffer.empty()) {
        leidos = std::fread(buffer.data(), 1, buffer.size(), file);
    }
    std::fclose(file);

    if (leidos != buffer.size()) {
        std::fprintf(stderr, "load_csv: lectura incompleta de '%s' (%zu de %zu bytes)\n",
                     path, leidos, buffer.size());
        return false;
    }
    return true;
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

bool load_csv(const char* path, std::size_t rows, std::size_t cols, Matrix& out) {
    std::vector<char> buffer;
    if (!read_file(path, buffer)) return false;
    return parse_csv_buffer(buffer.data(), buffer.size(), rows, cols, out);
}

bool load_csv_into(const char* path, double* dst, std::size_t capacity,
                   std::size_t& written) {
    written = 0;
    std::vector<char> buffer;
    if (!read_file(path, buffer)) return false;

    if (!parse_csv_into(buffer.data(), buffer.size(), dst, capacity, written)) {
        return false;
    }
    if (written != capacity) {
        std::fprintf(stderr,
                     "load_csv_into: se esperaban %zu valores y se encontraron %zu\n",
                     capacity, written);
        return false;
    }
    return true;
}
