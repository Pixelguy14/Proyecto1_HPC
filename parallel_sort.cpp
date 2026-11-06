// ============================================================================
// parallel_sort.cpp - Implementacion de pthread_create/pthread_join
//
// Proyecto 1 - Topicos de HPC (Sprint 2 - Paco)
//
// Este archivo solo coordina hilos. El trabajo pesado (ordenar un rango) vive
// en sort_worker.cpp, y la memoria/contadores compartidos viven en
// shared_memory.cpp. Mantener esas responsabilidades separadas permite cambiar
// el algoritmo de ordenamiento sin tocar la capa de hilos.
// ============================================================================
#include "parallel_sort.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sort_worker.hpp"

namespace {

// ---------------------------------------------------------------------------
// Contexto de un hilo
// ---------------------------------------------------------------------------
// pthread_create() solo acepta un puntero void*. En lugar de empaquetar los
// argumentos en una cadena de bytes, se usa esta estructura y se le pasa su
// direccion. Cada hilo recibe una instancia distinta.
//
// `args` ya contiene el rango [begin, end) que le toca al hilo y el puntero al
// arreglo. `control` apunta al bloque compartido donde se contabiliza el avance.
struct ThreadTask {
    WorkerArgs args;
    SharedControl* control = nullptr;
};

// ---------------------------------------------------------------------------
// thread_entry - funcion que ejecuta cada pthread
// ---------------------------------------------------------------------------
// Ordena el bloque asignado y, al terminar, incrementa el contador compartido.
// No se hace nada especial con el valor de retorno: pthread_join() lo ignora.
//
// El orden de estas dos operaciones importa: primero se ordena y solo despues
// se marca "done". Asi, cuando el hilo principal ve completed_workers == N,
// sabe que los N bloques ya terminaron de ordenarse.
void* thread_entry(void* raw) {
    ThreadTask* task = static_cast<ThreadTask*>(raw);

    sort_worker(&task->args);
    shared_control_mark_done(task->control);

    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// parallel_sort_chunks - crea, ejecuta y espera los hilos
// ---------------------------------------------------------------------------
// Fases:
//   1. Validar parametros y reiniciar contadores.
//   2. Calcular el rango de cada hilo y crear un pthread por rango.
//   3. Esperar (join) a todos los hilos que se crearon.
//   4. Recolectar estadisticas y decidir si la corrida fue completa.
bool parallel_sort_chunks(double* data, std::size_t n,
                          std::size_t num_threads, SharedControl* control,
                          ParallelSortStats& stats) {
    // Se reinicia la salida desde el principio. Si la validacion falla, el
    // llamador recibe estadisticas en cero en lugar de basura de una corrida
    // anterior.
    stats = ParallelSortStats{};

    if (data == nullptr || num_threads == 0 || control == nullptr) {
        std::fprintf(stderr, "parallel_sort_chunks: parametros invalidos\n");
        return false;
    }

    // El bloque de control puede reutilizarse entre corridas. Se reinician los
    // contadores bajo el mutex para no pisar a otro hilo/proceso que aun los
    // este leyendo (en el Sprint 4 esto sera especialmente importante).
    pthread_mutex_lock(&control->mutex);
    control->completed_workers = 0;
    control->error = 0;
    pthread_mutex_unlock(&control->mutex);

    // Los vectores se dimensionan una sola vez y no vuelven a crecer. Por eso
    // las direcciones &threads[i] y &tasks[i] permanecen estables mientras los
    // hilos las usan; si el vector se reallocara, los punteros quedarian
    // colgando. Esta es la razon de reservar exactamente num_threads.
    std::vector<pthread_t> threads(num_threads);
    std::vector<ThreadTask> tasks(num_threads);

    // `created` cuenta los hilos que realmente se crearon. Puede ser menor que
    // num_threads si pthread_create() falla a mitad de la lista.
    std::size_t created = 0;

    // El cronometro incluye las tres fases: creacion, ejecucion concurrente y
    // join. Es el tiempo de pared que percibe el usuario.
    const auto begin = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < num_threads; ++i) {
        // worker_range() reparte el sobrante entre los primeros indices, de
        // modo que los bloques difieren como maximo en un elemento.
        tasks[i].args.data = data;
        tasks[i].args.worker_id = i;
        worker_range(n, num_threads, i, tasks[i].args.begin,
                     tasks[i].args.end);
        tasks[i].control = control;

        const int rc = pthread_create(&threads[i], nullptr, thread_entry,
                                      &tasks[i]);
        if (rc != 0) {
            // No se lanzan mas hilos. Los ya creados siguen ejecutandose y se
            // esperan abajo; los que faltaban simplemente no se crean.
            std::fprintf(stderr,
                         "parallel_sort_chunks: pthread_create fallo en el hilo %zu: %s\n",
                         i, std::strerror(rc));
            break;
        }
        ++created;
    }

    // join por cada hilo creado. Es indispensable: sin join, la funcion podria
    // retornar (y liberar la memoria) mientras un hilo sigue usando `data`.
    for (std::size_t i = 0; i < created; ++i) {
        pthread_join(threads[i], nullptr);
    }

    const auto end = std::chrono::steady_clock::now();

    stats.sort_ms =
        std::chrono::duration<double, std::milli>(end - begin).count();
    stats.created_workers = created;

    // Se lee despues de todos los join, asi que ya no hay escrituras en curso.
    // No hace falta tomar el mutex para esta lectura final.
    stats.completed_workers = control->completed_workers;

    // Solo se considera exitosa la corrida si se crearon todos los hilos. Un
    // valor menor indica que parte del arreglo no fue procesada.
    return created == num_threads;
}
