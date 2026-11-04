// ============================================================================
// file_io.hpp - Guardado del arreglo ordenado en CSV y verificacion
//
// Proyecto 1 - Topicos de HPC
// Sprint 1: funcion de guardado del arreglo en un archivo de salida.
// ============================================================================
#ifndef FILE_IO_HPP
#define FILE_IO_HPP

#include <cstddef>

// Guarda data (rows * cols elementos, row-major) como CSV, sin encabezado.
// Devuelve false si no se pudo abrir o escribir el archivo.
bool save_csv(const char* path, const double* data, std::size_t rows,
              std::size_t cols);

// Recorre el arreglo y confirma data[i-1] <= data[i] para todo i.
bool is_sorted_ascending(const double* data, std::size_t n);

#endif  // FILE_IO_HPP
