// ============================================================================
// scatter_gather.hpp - Sample Sort paralelo: scatter, orden local y gather
//
// Proyecto 1 - Topicos de HPC
// Sprint 3: usar los splitters de sample_sort.hpp para repartir los
// datos en cubetas, ordenar cada cubeta en paralelo y dejar el resultado
// globalmente ordenado.
//
// El "gather" es implicito: el scatter escribe las cubetas una detras de otra
// en el orden de los splitters, asi que al terminar de ordenar cada cubeta el
// arreglo completo ya queda ordenado. No hace falta mezclar cubetas.
//
// Flujo:
//   build_sample_plan()          muestreo + splitters
//     -> count_buckets()         contar cuantos elementos van a cada cubeta
//     -> offsets                prefijo de los conteos
//     -> scatter                 copiar cada elemento a su cubeta
//     -> parallel_sort_buckets() ordenar cada cubeta en paralelo
//     -> verificar               is_sorted_ascending() en main
// ============================================================================
#ifndef SCATTER_GATHER_HPP
#define SCATTER_GATHER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "shared_memory.hpp"

// Resultado de una corrida de sample_sort_parallel().
// Los tiempos se reportan por etapa para poder analizarlos en el reporte.
struct ScatterGatherStats {
    float plan_ms = 0.0;     // muestreo + ordenar muestra + splitters
    float count_ms = 0.0;    // conteo de elementos por cubeta
    float scatter_ms = 0.0;  // copia de cada elemento a su cubeta
    float sort_ms = 0.0;     // orden local de las cubetas (paralelo)
    float total_ms = 0.0;    // plan + conteo + scatter + orden local

    std::size_t num_buckets = 0;
    std::size_t num_splitters = 0;
    std::size_t min_bucket = 0;  // cubeta con menos elementos
    std::size_t max_bucket = 0;  // cubeta con mas elementos

    // Conteo real de cada cubeta. Se guarda para imprimir el balance y para
    // comparar contra el ideal n / num_buckets.
    std::vector<std::size_t> counts;
};

// ---------------------------------------------------------------------------
// sample_sort_parallel
// ---------------------------------------------------------------------------
// Ejecuta el Sample Sort completo sobre `input` y deja el resultado en
// `output`. No modifica `input`.
//
// Parametros:
//   input        Arreglo de entrada de `n` floats.
//   output       Buffer de salida de `n` floats. Debe ser distinto de input:
//                el scatter lee input mientras escribe output.
//   n            Numero de elementos.
//   num_buckets  Cantidad de cubetas y de workers. Debe ser >= 2.
//   sample_size  Tamano de la muestra para elegir splitters (> 0).
//   seed         Semilla del PRNG; la misma semilla repite la corrida.
//   control      Bloque inicializado con shared_control_init(control,
//                num_buckets). Se usa para el contador de workers.
//   stats        Salida: tiempos, conteos y numero de splitters.
//
// Devuelve:
//   true   si el plan, el conteo, el scatter y el orden local terminaron bien.
//   false  si algun parametro es invalido, si el muestreo falla, si los offsets
//          no cubren exactamente n elementos o si falla la creacion de hilos.
//
// Precondiciones:
//   - input y output no se solapan.
//   - output tiene espacio para n floats.
//   - num_buckets es razonable para el equipo (idealmente <= nucleos y <= n).
//
// Postcondiciones:
//   - output contiene los mismos elementos que input, ordenados de forma
//     ascendente en todo el arreglo.
//   - stats.counts[b] es la cantidad de elementos de la cubeta b.
//   - La suma de stats.counts es n.
//
// Notas:
//   - El conteo y el scatter son secuenciales; el orden local de cubetas es
//     la etapa paralela y la que domina el tiempo.
//   - Cada cubeta se ordena con merge_sort_range(), sin std::sort.
// ---------------------------------------------------------------------------
bool sample_sort_parallel(const float* input, float* output, std::size_t n,
                          std::size_t num_buckets, std::size_t sample_size,
                          std::uint64_t seed, SharedControl* control,
                          ScatterGatherStats& stats);

#endif  // SCATTER_GATHER_HPP
