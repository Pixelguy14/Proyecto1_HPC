// ============================================================================
// file_io.cpp - Guardado en texto/binario y verificacion
//
// Proyecto 1 - Topicos de HPC
// ============================================================================
#include "file_io.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const char kBinMagic[8] = {'M', 'A', 'T', 'B', 'I', 'N', '1', '\0'};
constexpr std::uint32_t kBinVersion = 1;
constexpr std::uint32_t kBinDtypefloat = 8;

// Compara el final de `path` con `suffix` sin distinguir mayusculas.
bool ends_with_ci(const char* path, const char* suffix) {
    if (path == nullptr || suffix == nullptr) return false;
    const std::size_t path_len = std::strlen(path);
    const std::size_t suffix_len = std::strlen(suffix);
    if (suffix_len > path_len) return false;

    const char* tail = path + (path_len - suffix_len);
    for (std::size_t i = 0; i < suffix_len; ++i) {
        char a = tail[i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// Escritura de texto generica. `separator` es ',' para CSV y ' ' para TXT.
bool save_text(const char* path, const float* data, std::size_t rows,
               std::size_t cols, char separator, const char* format) {
    if (path == nullptr || cols == 0 || format == nullptr) {
        std::fprintf(stderr, "save_text: parametros invalidos\n");
        return false;
    }
    if (data == nullptr && rows != 0) {
        std::fprintf(stderr, "save_text: arreglo nulo\n");
        return false;
    }
    if (rows != 0 && cols > static_cast<std::size_t>(-1) / rows) {
        std::fprintf(stderr, "save_text: desbordamiento en rows * cols\n");
        return false;
    }

    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "save_text: no se pudo crear '%s': %s\n", path,
                     std::strerror(errno));
        return false;
    }

    // Buffer grande: evita una escritura al disco por cada numero.
    std::vector<char> buffer(1 << 20);
    std::setvbuf(file, buffer.data(), _IOFBF, buffer.size());

    const std::size_t total = rows * cols;
    for (std::size_t i = 0; i < total; ++i) {
        if (i % cols != 0) std::fputc(separator, file);
        std::fprintf(file, format, data[i]);
        if ((i + 1) % cols == 0) std::fputc('\n', file);
    }

    bool ok = (std::ferror(file) == 0);
    if (std::fclose(file) != 0) ok = false;
    if (!ok) {
        std::fprintf(stderr, "save_text: error al escribir '%s'\n", path);
    }
    return ok;
}

}  // namespace

bool save_csv(const char* path, const float* data, std::size_t rows,
              std::size_t cols) {
    // Se conserva el comportamiento historico: comas y %.17g.
    return save_text(path, data, rows, cols, ',', "%.17g");
}

bool save_matrix(const char* path, const float* data, std::size_t rows,
                 std::size_t cols) {
    if (path == nullptr) {
        std::fprintf(stderr, "save_matrix: ruta nula\n");
        return false;
    }

    if (ends_with_ci(path, ".bin")) {
        return save_bin(path, data, rows, cols);
    }
    if (ends_with_ci(path, ".txt")) {
        // %.10g es suficiente para los 6 decimales de matriz_a.txt y evita
        // imprimir artefactos de representacion binaria (%.17g).
        return save_text(path, data, rows, cols, ' ', "%.10g");
    }

    // .csv y cualquier otra extension: comportamiento historico.
    return save_text(path, data, rows, cols, ',', "%.17g");
}

bool save_bin(const char* path, const float* data, std::size_t rows,
              std::size_t cols) {
    if (path == nullptr || cols == 0) {
        std::fprintf(stderr, "save_bin: parametros invalidos\n");
        return false;
    }
    if (data == nullptr && rows != 0) {
        std::fprintf(stderr, "save_bin: arreglo nulo\n");
        return false;
    }
    if (rows != 0 && cols > static_cast<std::size_t>(-1) / rows) {
        std::fprintf(stderr, "save_bin: desbordamiento en rows * cols\n");
        return false;
    }

    const std::size_t total = rows * cols;

    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "save_bin: no se pudo crear '%s': %s\n", path,
                     std::strerror(errno));
        return false;
    }

    BinHeader header{};
    std::memcpy(header.magic, kBinMagic, sizeof(kBinMagic));
    header.version = kBinVersion;
    header.dtype = kBinDtypefloat;
    header.rows = static_cast<std::uint64_t>(rows);
    header.cols = static_cast<std::uint64_t>(cols);
    header.count = static_cast<std::uint64_t>(total);

    bool ok = (std::fwrite(&header, sizeof(header), 1, file) == 1);

    // Escritura por bloques para no pedir un fwrite gigantesco de una vez.
    constexpr std::size_t kChunk = 1 << 20;  // 1 M floats = 8 MB
    for (std::size_t offset = 0; ok && offset < total; offset += kChunk) {
        const std::size_t n =
            (total - offset < kChunk) ? (total - offset) : kChunk;
        if (std::fwrite(data + offset, sizeof(float), n, file) != n) {
            ok = false;
        }
    }

    if (std::fclose(file) != 0) ok = false;
    if (!ok) {
        std::fprintf(stderr, "save_bin: error al escribir '%s'\n", path);
    }
    return ok;
}

bool verify_bin(const char* path, std::size_t rows, std::size_t cols,
                BinVerifyStats& stats) {
    stats = BinVerifyStats{};

    if (path == nullptr) {
        std::fprintf(stderr, "verify_bin: ruta nula\n");
        return false;
    }

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        std::fprintf(stderr, "verify_bin: no se pudo abrir '%s': %s\n", path,
                     std::strerror(errno));
        return false;
    }

    BinHeader header{};
    if (std::fread(&header, sizeof(header), 1, file) != 1) {
        std::fprintf(stderr, "verify_bin: cabecera incompleta en '%s'\n", path);
        std::fclose(file);
        return false;
    }

    if (std::memcmp(header.magic, kBinMagic, sizeof(kBinMagic)) != 0) {
        std::fprintf(stderr, "verify_bin: magic invalido en '%s'\n", path);
        std::fclose(file);
        return false;
    }
    if (header.version != kBinVersion) {
        std::fprintf(stderr, "verify_bin: version %u no soportada en '%s'\n",
                     header.version, path);
        std::fclose(file);
        return false;
    }
    if (header.dtype != kBinDtypefloat) {
        std::fprintf(stderr, "verify_bin: dtype %u no soportado en '%s'\n",
                     header.dtype, path);
        std::fclose(file);
        return false;
    }
    if (header.rows == 0 || header.cols == 0) {
        std::fprintf(stderr, "verify_bin: dimensiones invalidas en '%s'\n",
                     path);
        std::fclose(file);
        return false;
    }
    if (header.rows > static_cast<std::uint64_t>(-1) / header.cols) {
        std::fprintf(stderr, "verify_bin: desbordamiento en la cabecera\n");
        std::fclose(file);
        return false;
    }
    if (header.count != header.rows * header.cols) {
        std::fprintf(stderr,
                     "verify_bin: count %llu no coincide con %llu x %llu\n",
                     static_cast<unsigned long long>(header.count),
                     static_cast<unsigned long long>(header.rows),
                     static_cast<unsigned long long>(header.cols));
        std::fclose(file);
        return false;
    }

    // Si el llamador no dio dimensiones, se usan las de la cabecera.
    if (rows == 0 || cols == 0) {
        rows = static_cast<std::size_t>(header.rows);
        cols = static_cast<std::size_t>(header.cols);
    } else if (rows != header.rows || cols != header.cols) {
        std::fprintf(stderr,
                     "verify_bin: dimensiones %zu x %zu no coinciden con la "
                     "cabecera %llu x %llu\n",
                     rows, cols, static_cast<unsigned long long>(header.rows),
                     static_cast<unsigned long long>(header.cols));
        std::fclose(file);
        return false;
    }

    const std::size_t total = static_cast<std::size_t>(header.count);
    constexpr std::size_t kChunk = 1 << 20;  // 1 M floats = 8 MB
    std::vector<float> buffer(kChunk);

    bool sorted = true;
    bool have_prev = false;
    float prev = 0.0;
    float min_value = 0.0;
    float max_value = 0.0;
    std::uint64_t checksum = 0;
    std::size_t read_total = 0;

    while (read_total < total) {
        const std::size_t want =
            (total - read_total < kChunk) ? (total - read_total) : kChunk;
        const std::size_t got =
            std::fread(buffer.data(), sizeof(float), want, file);
        if (got != want) {
            std::fprintf(stderr,
                         "verify_bin: lectura incompleta en '%s' (%zu de %zu "
                         "en el bloque)\n",
                         path, got, want);
            std::fclose(file);
            return false;
        }

        for (std::size_t i = 0; i < got; ++i) {
            const float v = buffer[i];
            if (!have_prev) {
                min_value = v;
                max_value = v;
                have_prev = true;
            } else {
                if (prev > v) sorted = false;
                if (v < min_value) min_value = v;
                if (v > max_value) max_value = v;
            }
            prev = v;

            std::uint64_t bits = 0;
            std::memcpy(&bits, &v, sizeof(bits));
            checksum += bits;
        }
        read_total += got;
    }

    // No debe haber datos extra despues de count floats.
    const int extra = std::fgetc(file);
    if (extra != EOF) {
        std::fprintf(stderr, "verify_bin: datos extra al final de '%s'\n",
                     path);
        std::fclose(file);
        return false;
    }

    if (std::fclose(file) != 0) {
        std::fprintf(stderr, "verify_bin: error al cerrar '%s'\n", path);
        return false;
    }

    stats.rows = rows;
    stats.cols = cols;
    stats.count = read_total;
    stats.min_value = min_value;
    stats.max_value = max_value;
    stats.checksum = checksum;
    stats.sorted = sorted;

    return sorted && read_total == total;
}

bool is_sorted_ascending(const float* data, std::size_t n) {
    if (n < 2) return true;
    if (data == nullptr) return false;
    for (std::size_t i = 1; i < n; ++i) {
        if (data[i - 1] > data[i]) return false;
    }
    return true;
}
