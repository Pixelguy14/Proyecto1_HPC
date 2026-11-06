// ============================================================================
// parallel_sort.hpp - Ordenamiento por bloques con pthreads
//
// Proyecto 1 - Topicos de HPC
// Sprint 2 (Paco): lanzar y sincronizar hilos con pthread_create/pthread_join.
//
// Este modulo es la capa de "sistemas": no sabe como se ordena un rango.
// Julian aporta sort_worker() y worker_range(); aqui solo se crean los hilos,
// se les asigna un bloque contiguo y se espera a que terminen.
//
// Flujo completo:
//   parallel_sort_chunks()
//     -> worker_range() para calcular [begin, end) de cada hilo
//     -> pthread_create(thread_entry)
//          -> sort_worker()             ordena el bloque
//          -> shared_control_mark_done() marca el avance bajo mutex
//     -> pthread_join()                espera a todos los hilos creados
//
// Importante: cada hilo ordena su bloque, pero el arreglo completo NO queda
// globalmente ordenado. El orden global requiere el scatter/gather del Sprint 3.
// ============================================================================
#ifndef PARALLEL_SORT_HPP
#define PARALLEL_SORT_HPP

#include <cstddef>

#include "shared_memory.hpp"

// Resultado de una corrida de parallel_sort_chunks().
// Se usa para imprimir el resumen y para decidir si la corrida fue correcta.
struct ParallelSortStats {
    // Tiempo de pared del bloque completo: crear hilos + ordenar + join.
    double sort_ms = 0.0;

    // Hilos que pthread_create() logro crear.
    std::size_t created_workers = 0;

    // Hilos que llegaron al final de thread_entry() e incrementaron el contador
    // compartido. En una corrida sana debe ser igual a created_workers.
    std::size_t completed_workers = 0;
};

// ---------------------------------------------------------------------------
// parallel_sort_chunks
// ---------------------------------------------------------------------------
// Divide [0, n) en `num_threads` bloques contiguos, lanza un hilo por bloque y
// espera a que todos terminen. Cada hilo ordena su bloque con merge_sort_range()
// (a traves de sort_worker()) y marca su avance en `control`.
//
// Parametros:
//   data         Arreglo de `n` doubles. Debe ser memoria compartida o al menos
//                permanecer valida hasta que la funcion retorne.
//   n            Numero total de elementos.
//   num_threads  Cantidad de hilos/bloques. Debe ser > 0.
//   control      Bloque de control ya inicializado con shared_control_init().
//                Se usa para reiniciar y consultar completed_workers.
//   stats        Salida: tiempos y contadores de la corrida. Se reinicia al
//                entrar, incluso si los parametros son invalidos.
//
// Devuelve:
//   true   si se crearon exactamente `num_threads` hilos y todos hicieron join.
//   false  si algun parametro es invalido o si pthread_create() fallo. En ese
//          caso se esperan los hilos que si se crearon y se reporta cuantos.
//
// Precondiciones:
//   - control != nullptr y fue inicializado con shared_control_init().
//   - control->total_workers describe la misma cantidad de hilos.
//
// Postcondiciones:
//   - Cada bloque [begin, end) queda ordenado de forma ascendente.
//   - No se modifica ningun elemento fuera de [0, n).
//   - El arreglo completo puede seguir desordenado entre bloques.
//
// Notas de concurrencia:
//   - Los hilos solo escriben en su propio bloque: no hay condicion de carrera
//     sobre `data`.
//   - El unico recurso compartido que se modifica es completed_workers, y se
//     protege con el mutex process-shared de `control`.
// ---------------------------------------------------------------------------
bool parallel_sort_chunks(double* data, std::size_t n,
                          std::size_t num_threads, SharedControl* control,
                          ParallelSortStats& stats);

#endif  // PARALLEL_SORT_HPP
