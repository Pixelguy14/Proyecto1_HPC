// ============================================================================
// parallel_sort.cpp - Implementacion de pthread_create/pthread_join
//
// Proyecto 1 - Topicos de HPC (Sprint 2)
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

#include "merge_sort.hpp"
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

// ---------------------------------------------------------------------------
// Contexto de un hilo que ordena cubetas
// ---------------------------------------------------------------------------
// A diferencia de ThreadTask, aqui no hay un solo rango: el hilo recorre las
// cubetas que le tocaron (worker_id, worker_id + num_workers, ...). Los offsets
// son los mismos para todos los hilos y solo se leen, por lo que no necesitan
// proteccion.
struct BucketTask {
    double* data = nullptr;
    const std::size_t* offsets = nullptr;
    std::size_t num_buckets = 0;
    std::size_t worker_id = 0;
    std::size_t num_workers = 0;
    SharedControl* control = nullptr;
};

// ---------------------------------------------------------------------------
// bucket_thread_entry - ordena las cubetas asignadas a un worker
// ---------------------------------------------------------------------------
// Cada cubeta [offsets[b], offsets[b + 1]) se ordena con merge_sort_range().
// Las cubetas vacias (begin == end) se saltan para no reservar buffers
// temporales inutiles. Al final se marca el avance igual que en el Sprint 2.
void* bucket_thread_entry(void* raw) {
    BucketTask* task = static_cast<BucketTask*>(raw);

    for (std::size_t b = task->worker_id; b < task->num_buckets;
         b += task->num_workers) {
        const std::size_t begin = task->offsets[b];
        const std::size_t end = task->offsets[b + 1];

        if (begin < end) {
            merge_sort_range(task->data, begin, end);
        }
    }

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

// ---------------------------------------------------------------------------
// parallel_sort_buckets - orden local del Sample Sort
// ---------------------------------------------------------------------------
// Es la version del Sprint 3: en lugar de dividir [0, n) en bloques iguales,
// recibe offsets calculados por el scatter. Las cubetas pueden tener tamanos
// muy distintos, asi que el reparto se hace por turnos y no por rangos fijos.
bool parallel_sort_buckets(double* data, const std::size_t* offsets,
                           std::size_t num_buckets, std::size_t num_workers,
                           SharedControl* control, ParallelSortStats& stats) {
    stats = ParallelSortStats{};

    if (data == nullptr || offsets == nullptr || num_buckets == 0 ||
        num_workers == 0 || control == nullptr) {
        std::fprintf(stderr, "parallel_sort_buckets: parametros invalidos\n");
        return false;
    }

    // Validar que los offsets sean monotonicos evita que un error del scatter
    // se convierta en un rango invertido o fuera de los limites del arreglo.
    for (std::size_t b = 0; b < num_buckets; ++b) {
        if (offsets[b] > offsets[b + 1]) {
            std::fprintf(stderr,
                         "parallel_sort_buckets: offsets no monotonos en la cubeta %zu\n",
                         b);
            return false;
        }
    }

    // Igual que en parallel_sort_chunks(): reiniciar el bloque de control bajo
    // el mutex antes de lanzar una nueva generacion de workers.
    pthread_mutex_lock(&control->mutex);
    control->completed_workers = 0;
    control->error = 0;
    pthread_mutex_unlock(&control->mutex);

    std::vector<pthread_t> threads(num_workers);
    std::vector<BucketTask> tasks(num_workers);
    std::size_t created = 0;

    const auto begin = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < num_workers; ++i) {
        tasks[i].data = data;
        tasks[i].offsets = offsets;
        tasks[i].num_buckets = num_buckets;
        tasks[i].worker_id = i;
        tasks[i].num_workers = num_workers;
        tasks[i].control = control;

        const int rc = pthread_create(&threads[i], nullptr, bucket_thread_entry,
                                      &tasks[i]);
        if (rc != 0) {
            std::fprintf(stderr,
                         "parallel_sort_buckets: pthread_create fallo en el worker %zu: %s\n",
                         i, std::strerror(rc));
            break;
        }
        ++created;
    }

    for (std::size_t i = 0; i < created; ++i) {
        pthread_join(threads[i], nullptr);
    }

    const auto end = std::chrono::steady_clock::now();

    stats.sort_ms =
        std::chrono::duration<double, std::milli>(end - begin).count();
    stats.created_workers = created;
    stats.completed_workers = control->completed_workers;

    return created == num_workers;
}
