// ============================================================================
// scatter_gather.cpp - Implementacion del Sample Sort paralelo
//
// Proyecto 1 - Topicos de HPC
//
// Este archivo une las piezas del Sprint 3:
//   - sample_sort.hpp: splitters y find_bucket().
//   - parallel_sort.hpp: orden local de cubetas con pthreads.
//   - shared_memory.hpp: bloque de control para los workers.
//
// El resultado se escribe en un buffer distinto de la entrada. Ese buffer
// queda globalmente ordenado porque las cubetas se escriben en orden creciente
// de splitters; el "gather" es simplemente usar ese buffer como resultado.
// ============================================================================
#include "scatter_gather.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "parallel_sort.hpp"
#include "sample_sort.hpp"

namespace {

float elapsed_ms(std::chrono::steady_clock::time_point begin,
                  std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - begin).count();
}

}  // namespace

bool sample_sort_parallel(const float* input, float* output, std::size_t n,
                          std::size_t num_buckets, std::size_t sample_size,
                          std::uint64_t seed, SharedControl* control,
                          ScatterGatherStats& stats) {
    stats = ScatterGatherStats{};

    if (input == nullptr || output == nullptr || n == 0 ||
        num_buckets < 2 || sample_size == 0 || control == nullptr) {
        std::fprintf(stderr, "sample_sort_parallel: parametros invalidos\n");
        return false;
    }

    if (input == output) {
        std::fprintf(stderr,
                     "sample_sort_parallel: input y output deben ser buffers distintos\n");
        return false;
    }

    stats.num_buckets = num_buckets;
    const auto total_begin = std::chrono::steady_clock::now();

    // -----------------------------------------------------------------------
    // 1. Plan: muestra, ordenamiento de la muestra y splitters
    // -----------------------------------------------------------------------
    std::vector<float> splitters;
    const auto plan_begin = std::chrono::steady_clock::now();

    if (!build_sample_plan(input, n, sample_size, num_buckets, seed,
                           splitters)) {
        std::fprintf(stderr, "sample_sort_parallel: no se pudo construir el plan\n");
        return false;
    }

    const auto plan_end = std::chrono::steady_clock::now();
    stats.plan_ms = elapsed_ms(plan_begin, plan_end);
    stats.num_splitters = splitters.size();

    // -----------------------------------------------------------------------
    // 2. Conteo: cuantos elementos caen en cada cubeta
    // -----------------------------------------------------------------------
    // Se recorre input una vez. Este paso es necesario para conocer el tamano
    // de cada cubeta antes de hacer el scatter y calcular los offsets.
    const auto count_begin = std::chrono::steady_clock::now();

    std::vector<std::size_t> counts(num_buckets, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t b =
            find_bucket(splitters.data(), splitters.size(), input[i]);
        ++counts[b];
    }

    const auto count_end = std::chrono::steady_clock::now();
    stats.count_ms = elapsed_ms(count_begin, count_end);

    // -----------------------------------------------------------------------
    // 3. Offsets: prefijo de los conteos
    // -----------------------------------------------------------------------
    // offsets[b] es la posicion donde empieza la cubeta b en output.
    // offsets[b + 1] es donde termina (exclusivo).
    std::vector<std::size_t> offsets(num_buckets + 1, 0);
    for (std::size_t b = 0; b < num_buckets; ++b) {
        offsets[b + 1] = offsets[b] + counts[b];
    }

    // Invariante: si el conteo esta bien, el ultimo offset debe ser n.
    if (offsets[num_buckets] != n) {
        std::fprintf(stderr,
                     "sample_sort_parallel: los offsets cubren %zu elementos, se esperaban %zu\n",
                     offsets[num_buckets], n);
        return false;
    }

    // -----------------------------------------------------------------------
    // 4. Scatter: copiar cada elemento a su cubeta
    // -----------------------------------------------------------------------
    // `next` es el cursor de escritura de cada cubeta. Empieza en offsets y
    // avanza una posicion por cada elemento colocado. Al final, cada cursor
    // debe haber llegado exactamente a offsets[b + 1].
    const auto scatter_begin = std::chrono::steady_clock::now();

    std::vector<std::size_t> next = offsets;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t b =
            find_bucket(splitters.data(), splitters.size(), input[i]);
        output[next[b]++] = input[i];
    }

    const auto scatter_end = std::chrono::steady_clock::now();
    stats.scatter_ms = elapsed_ms(scatter_begin, scatter_end);

    // Verificacion del scatter: detecta cubetas incompletas o desbordadas.
    for (std::size_t b = 0; b < num_buckets; ++b) {
        if (next[b] != offsets[b + 1]) {
            std::fprintf(stderr,
                         "sample_sort_parallel: la cubeta %zu quedo incompleta\n",
                         b);
            return false;
        }
    }

    // -----------------------------------------------------------------------
    // 5. Orden local en paralelo
    // -----------------------------------------------------------------------
    // Cada worker recibe una o mas cubetas. La funcion ya reinicia el bloque
    // de control y espera a que todos los hilos terminen.
    ParallelSortStats sort_stats;
    const auto sort_begin = std::chrono::steady_clock::now();

    const bool sorted_ok =
        parallel_sort_buckets(output, offsets.data(), num_buckets, num_buckets,
                              control, sort_stats);

    const auto sort_end = std::chrono::steady_clock::now();

    if (!sorted_ok) {
        std::fprintf(stderr, "sample_sort_parallel: fallo el orden local\n");
        return false;
    }

    // parallel_sort_buckets() ya mide creacion + ejecucion + join. Se usa su
    // medicion para que el reporte sea consistente con el Sprint 2.
    stats.sort_ms = sort_stats.sort_ms;
    if (stats.sort_ms <= 0.0) {
        stats.sort_ms = elapsed_ms(sort_begin, sort_end);
    }

    // -----------------------------------------------------------------------
    // 6. Estadisticas de balance y cierre
    // -----------------------------------------------------------------------
    stats.min_bucket = n;
    stats.max_bucket = 0;
    for (std::size_t b = 0; b < num_buckets; ++b) {
        if (counts[b] < stats.min_bucket) stats.min_bucket = counts[b];
        if (counts[b] > stats.max_bucket) stats.max_bucket = counts[b];
    }

    const auto total_end = std::chrono::steady_clock::now();
    stats.total_ms = elapsed_ms(total_begin, total_end);
    stats.counts = counts;

    return true;
}
