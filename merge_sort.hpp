// ============================================================================
// merge_sort.hpp - Merge Sort propio (sin std::sort)
//
// Proyecto 1 - Topicos de HPC
// Sprint 1: mergeSort y merge escritos desde cero.
//
// El algoritmo es top-down: divide el rango a la mitad, ordena cada mitad y
// las mezcla con merge(). Para el Sprint 2, merge_sort_range() permite que
// cada hilo ordene solo su subseccion sin tocar el resto del arreglo.
// ============================================================================
#ifndef MERGE_SORT_HPP
#define MERGE_SORT_HPP

#include <cstddef>

// Ordena [begin, end) de data. temp debe tener espacio para end - begin.
// Se expone para poder reutilizarla y para que sea facil de evaluar.
void merge(double* data, std::size_t begin, std::size_t mid, std::size_t end,
           double* temp);

// Ordena todo el arreglo (equivalente a mergeSort de la planeacion).
void merge_sort(double* data, std::size_t n);

// Ordena solo [begin, end). Reserva un buffer temporal del tamano del rango.
void merge_sort_range(double* data, std::size_t begin, std::size_t end);

#endif  // MERGE_SORT_HPP
