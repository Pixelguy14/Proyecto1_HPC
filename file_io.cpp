// ============================================================================
// file_io.cpp - Implementacion del guardado en CSV
//
// Proyecto 1 - Topicos de HPC
// ============================================================================
#include "file_io.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

bool save_csv(const char* path, const double* data, std::size_t rows,
              std::size_t cols) {
    if (path == nullptr || cols == 0) {
        std::fprintf(stderr, "save_csv: parametros invalidos\n");
        return false;
    }
    if (data == nullptr && rows != 0) {
        std::fprintf(stderr, "save_csv: arreglo nulo\n");
        return false;
    }

    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "save_csv: no se pudo crear '%s': %s\n", path,
                     std::strerror(errno));
        return false;
    }

    // Buffer de salida grande: evita una escritura al disco por cada numero.
    std::vector<char> buffer(1 << 20);
    std::setvbuf(file, buffer.data(), _IOFBF, buffer.size());

    const std::size_t total = rows * cols;
    for (std::size_t i = 0; i < total; ++i) {
        if (i % cols != 0) std::fputc(',', file);
        // %.17g conserva el valor de un double y evita notacion cientifica
        // para enteros como 1000000 (con %g se escribiria "1e+06").
        std::fprintf(file, "%.17g", data[i]);
        if ((i + 1) % cols == 0) std::fputc('\n', file);
    }

    bool ok = (std::ferror(file) == 0);
    if (std::fclose(file) != 0) ok = false;
    if (!ok) {
        std::fprintf(stderr, "save_csv: error al escribir '%s'\n", path);
    }
    return ok;
}

bool is_sorted_ascending(const double* data, std::size_t n) {
    if (n < 2) return true;
    if (data == nullptr) return false;
    for (std::size_t i = 1; i < n; ++i) {
        if (data[i - 1] > data[i]) return false;
    }
    return true;
}
