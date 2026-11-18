// ============================================================================
// sort_worker.hpp - Trabajo de un hilo y division del arreglo
//
// Proyecto 1 - Topicos de HPC
// Sprint 2: cada hilo recibe un rango [begin, end) y lo ordena con
// el Merge Sort del Sprint 1. La firma void* (void*) es compatible con
// pthread_create(), pero este modulo no incluye pthread.h: la creacion y
// sincronizacion de hilos es responsabilidad de la capa de sistemas.
// ============================================================================
#ifndef SORT_WORKER_HPP
#define SORT_WORKER_HPP

#include <cstddef>

// Parametros que recibe cada hilo. worker_id se usa ahora para depuracion y
// en el Sprint 4 se ampliara con el ID de proceso.
struct WorkerArgs {
    double* data = nullptr;
    std::size_t begin = 0;  // primer elemento del rango (inclusive)
    std::size_t end = 0;    // ultimo elemento del rango (exclusivo)
    std::size_t worker_id = 0;
};

// Funcion que ejecuta cada hilo: ordena [begin, end) con merge_sort_range().
void* sort_worker(void* arg);

// Divide [0, n) en `workers` rangos contiguos lo mas equilibrados posible.
// El reparto sobrante se da a los primeros indices:
//   n=10, workers=3 -> [0,4) [4,7) [7,10)
// Si workers == 0 o index >= workers, devuelve un rango vacio.
void worker_range(std::size_t n, std::size_t workers, std::size_t index,
                  std::size_t& begin, std::size_t& end);

#endif  // SORT_WORKER_HPP
