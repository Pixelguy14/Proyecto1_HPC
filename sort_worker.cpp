// ============================================================================
// sort_worker.cpp - Implementacion del Worker y del reparto de rangos
//
// Proyecto 1 - Topicos de HPC
// ============================================================================
#include "sort_worker.hpp"

#include "merge_sort.hpp"

void* sort_worker(void* arg) {
    WorkerArgs* args = static_cast<WorkerArgs*>(arg);
    if (args != nullptr && args->data != nullptr && args->begin < args->end) {
        merge_sort_range(args->data, args->begin, args->end);
    }
    return nullptr;
}

void worker_range(std::size_t n, std::size_t workers, std::size_t index,
                  std::size_t& begin, std::size_t& end) {
    begin = 0;
    end = 0;
    if (workers == 0 || index >= workers) return;

    const std::size_t base = n / workers;
    const std::size_t remainder = n % workers;

    // Los primeros `remainder` hilos reciben un elemento extra.
    const std::size_t extra_before =
        (index < remainder) ? index : remainder;
    begin = index * base + extra_before;
    end = begin + base + ((index < remainder) ? 1 : 0);
}
