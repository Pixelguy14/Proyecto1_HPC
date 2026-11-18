// ============================================================================
// file_io.hpp - Guardado en texto/binario y verificacion
//
// Proyecto 1 - Topicos de HPC
// Sprint 1: guardado del arreglo en CSV.
// Fase 1 de entrega: salida .txt (espacios), .csv (comas) y .bin (float crudo
// con cabecera), ademas de verificacion binaria por chunks.
// ============================================================================
#ifndef FILE_IO_HPP
#define FILE_IO_HPP

#include <cstddef>
#include <cstdint>

// Cabecera del formato binario. Se escribe tal cual (little-endian en x86-64).
// sizeof(BinHeader) == 40 bytes: 8 + 4 + 4 + 8 + 8 + 8.
struct BinHeader {
    char magic[8];       // "MATBIN1\0"
    std::uint32_t version;  // 1
    std::uint32_t dtype;    // 8 = float
    std::uint64_t rows;
    std::uint64_t cols;
    std::uint64_t count;    // rows * cols
};

// Resultado de verify_bin().
struct BinVerifyStats {
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::size_t count = 0;
    float min_value = 0.0;
    float max_value = 0.0;
    std::uint64_t checksum = 0;
    bool sorted = false;
};

// Guarda data (rows * cols elementos, row-major) como CSV, sin encabezado.
// Se conserva por compatibilidad; equivale a save_matrix() con extension .csv.
bool save_csv(const char* path, const float* data, std::size_t rows,
              std::size_t cols);

// Guarda segun la extension:
//   .csv -> comas, precision %.17g
//   .txt -> espacios, precision %.10g
//   .bin -> cabecera BinHeader + floats crudos
// Cualquier otra extension se trata como .csv.
bool save_matrix(const char* path, const float* data, std::size_t rows,
                 std::size_t cols);

// Guarda el formato binario con cabecera.
bool save_bin(const char* path, const float* data, std::size_t rows,
              std::size_t cols);

// Verifica un archivo .bin por chunks:
//   - valida cabecera, magic, version y dimensiones;
//   - confirma data[i-1] <= data[i] en todo el archivo;
//   - calcula min, max y checksum de bits.
// Si rows == 0 o cols == 0 se usan las dimensiones de la cabecera; si no,
// deben coincidir con las de la cabecera.
bool verify_bin(const char* path, std::size_t rows, std::size_t cols,
                BinVerifyStats& stats);

// Recorre el arreglo y confirma data[i-1] <= data[i] para todo i.
bool is_sorted_ascending(const float* data, std::size_t n);

#endif  // FILE_IO_HPP
