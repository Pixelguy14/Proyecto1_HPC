// ============================================================================
// merge_sort.cpp - Implementacion del Merge Sort propio
//
// Proyecto 1 - Topicos de HPC
// ============================================================================
#include "merge_sort.hpp"

#include <vector>

// Mezcla las dos mitades ordenadas [begin, mid) y [mid, end) en data.
// temp es el buffer auxiliar; al final se copia el resultado de vuelta.
void merge(double* data, std::size_t begin, std::size_t mid, std::size_t end,
           double* temp) {
    std::size_t i = begin;
    std::size_t j = mid;
    std::size_t k = 0;

    while (i < mid && j < end) {
        // <= conserva el orden relativo de los valores iguales (estable).
        if (data[i] <= data[j]) {
            temp[k++] = data[i++];
        } else {
            temp[k++] = data[j++];
        }
    }
    while (i < mid) temp[k++] = data[i++];
    while (j < end) temp[k++] = data[j++];

    for (std::size_t t = 0; t < k; ++t) {
        data[begin + t] = temp[t];
    }
}

namespace {

void merge_sort_recursive(double* data, std::size_t begin, std::size_t end,
                          double* temp) {
    if (end - begin < 2) return;  // 0 o 1 elemento ya esta ordenado

    const std::size_t mid = begin + (end - begin) / 2;
    merge_sort_recursive(data, begin, mid, temp);
    merge_sort_recursive(data, mid, end, temp);
    merge(data, begin, mid, end, temp);
}

}  // namespace

void merge_sort_range(double* data, std::size_t begin, std::size_t end) {
    if (data == nullptr || end <= begin + 1) return;

    // Buffer temporal solo del rango del hilo, no de todo el arreglo.
    std::vector<double> temp(end - begin);
    merge_sort_recursive(data, begin, end, temp.data());
}

void merge_sort(double* data, std::size_t n) {
    merge_sort_range(data, 0, n);
}
