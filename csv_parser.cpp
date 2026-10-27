// ============================================================================
// csv_parser.cpp - Implementacion del parser manual de CSV
//
// Proyecto 1 - Topicos de HPC
//
// Compilacion (junto con main.cpp):
//   g++ -O2 -std=c++17 -Wall -Wextra main.cpp csv_parser.cpp -o leer_csv
// ============================================================================
#include "csv_parser.hpp"

#include <cerrno>  // para reportar por qué falló fopen
#include <cstdio> // FILE, fopen, fread, fseek, ftell, fclose, fprintf
#include <cstdlib> // strtod conversión texto a número
#include <cstring> // memcpy (armar el token) y strerror (mensajes de error)

namespace {

// Separadores aceptados: coma, salto de linea y espacios.
// Cualquier otro caracter forma parte de un numero.
inline bool is_separator(char c) {
    return c == ',' || c == '\n' || c == '\r' || c == ' ' || c == '\t';
}

}  // namespace

bool parse_csv_buffer(const char* buf, std::size_t size, std::size_t rows,
                      std::size_t cols, Matrix& out) {
    if (rows == 0 || cols == 0) {
        std::fprintf(stderr, "parse_csv_buffer: dimensiones invalidas (%zu x %zu)\n", rows, cols);
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
    out.reserve(expected);  // evita reallocaciones durante el parseo

    const char* p = buf;
    const char* const end = buf + size;
    char token[64];  // cualquier double cabe de sobra

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
                         "parse_csv_buffer: numero demasiado largo en el byte %zu\n",
                         static_cast<std::size_t>(start - buf));
            out.data.clear();
            return false;
        }

        // Copia local para poder terminar el token en '\0' y usar strtod.
        // (strtod es atof() pero indica donde termino de convertir).
        std::memcpy(token, start, len);
        token[len] = '\0';

        char* next = nullptr;
        const double value = std::strtod(token, &next);
        if (next == token || *next != '\0') {
            std::fprintf(stderr,
                         "parse_csv_buffer: valor invalido \"%s\" en el byte %zu\n",
                         token, static_cast<std::size_t>(start - buf));
            out.data.clear();
            return false;
        }

        out.data.push_back(value);
    }

    if (out.data.size() != expected) {
        std::fprintf(stderr,
                     "parse_csv_buffer: se esperaban %zu valores (%zu x %zu) y se leyeron %zu\n",
                     expected, rows, cols, out.data.size());
        out.data.clear();
        return false;
    }

    return true;
}

bool load_csv(const char* path, std::size_t rows, std::size_t cols, Matrix& out) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        std::fprintf(stderr, "load_csv: no se pudo abrir '%s': %s\n", path, std::strerror(errno));
        return false;
    }

    // Tamano del archivo
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fprintf(stderr, "load_csv: fseek fallo en '%s'\n", path);
        std::fclose(file);
        return false;
    }
    const long length = std::ftell(file);
    if (length < 0) {
        std::fprintf(stderr, "load_csv: no se pudo obtener el tamano de '%s'\n", path);
        std::fclose(file);
        return false;
    }
    std::rewind(file);

    // Se lee todo el archivo de una vez con fread().
    // En la version final este bloque utiliza mmap() y llama directamente a 
    // parse_csv_buffer() sobre la memoria mapeada, evitando copiar
    std::vector<char> buffer(static_cast<std::size_t>(length));
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

    return parse_csv_buffer(buffer.data(), buffer.size(), rows, cols, out);
}
