// ============================================================================
// external_sort.hpp - Ordenamiento Externo Out-of-Core (Runs + K-Way Merge)
//
// Proyecto 1 - Topicos de HPC
// Fase 2: Procesar matrices que exceden la memoria fisica (RAM y /dev/shm)
// como matriz_a.txt (1.6 G doubles = 12.8 GB crudos, 25.6 GB con temp).
//
// Arquitectura del algoritmo (External Merge Sort en 2 Fases):
//
// Fase 1: Generacion y ordenamiento de runs (CPU-bound, altamente paralela)
//   - Lee bloques de tamano chunk_mb (ej: 256 MB o 512 MB) usando ChunkedTextReader.
//   - Cada bloque se ordena en memoria compartida mediante Sample Sort paralelo
//     (pthreads + mutex process-shared) aprovechando el 100% de la CPU.
//   - El bloque ordenado se escribe en disco como un archivo binario temporal:
//     runs/run_0000.bin, runs/run_0001.bin, etc.
//
// Fase 2: K-Way Merge con Min-Heap (I/O streaming y bajo consumo de memoria)
//   - Abre los K runs concurrentemente con buffers de lectura (prefetch).
//   - Un min-heap de K elementos mantiene en la cima el menor valor global
//     entre todos los runs activos.
//   - Se extrae el minimo y se emite al archivo de salida (si se especifico)
//     mientras se verifica en streaming que cada elemento cumpla prev <= curr
//     y se acumula el checksum de bits.
//   - Memoria consumida en el merge: O(K * buffer) ~ 25-50 MB.
//   - Al finalizar, se eliminan los archivos temporales de runs/.
// ============================================================================
#ifndef EXTERNAL_SORT_HPP
#define EXTERNAL_SORT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Estadisticas detalladas de la corrida del ordenamiento externo.
// Se usan para el reporte formal y para verificar la integridad del pipeline.
struct ExternalSortStats {
    std::size_t total_values = 0;       // Total de elementos procesados
    std::size_t num_runs = 0;           // Cantidad de runs generados
    std::size_t chunk_mb = 0;           // Tamano de cada run en MB

    // Tiempos de ejecucion (en milisegundos)
    double read_and_runs_ms = 0.0;     // Tiempo total de Fase 1 (I/O + Sort de runs)
    double avg_run_ms = 0.0;           // Tiempo promedio de ordenamiento por run
    double stddev_run_ms = 0.0;        // Desviacion estandar del tiempo por run
    double min_run_ms = 0.0;           // Tiempo del run mas rapido
    double max_run_ms = 0.0;           // Tiempo del run mas lento
    double merge_ms = 0.0;             // Tiempo total de Fase 2 (K-Way Merge)
    double total_ms = 0.0;             // Tiempo de pared total (Fase 1 + Fase 2)

    // Verificacion de integridad
    std::uint64_t input_checksum = 0;  // Suma de bits de los datos leidos
    std::uint64_t output_checksum = 0; // Suma de bits de los datos emitidos
    double min_value = 0.0;            // Valor minimo global observado
    double max_value = 0.0;            // Valor maximo global observado
    bool sorted = false;               // Confirmacion de orden monotono no decreciente
    bool checksum_ok = false;          // input_checksum == output_checksum
    bool saved = false;                // Se guardo exitosamente en output_path
};

// ---------------------------------------------------------------------------
// run_external_sort
// ---------------------------------------------------------------------------
// Coordina el ordenamiento externo out-of-core completo.
//
// Parametros:
//   input_path   Ruta del archivo de entrada (.txt o .csv).
//   rows, cols   Dimensiones de la matriz (rows * cols elementos).
//   num_threads  Cantidad de hilos para el Sample Sort en cada run.
//   output_path  Ruta de salida (.bin, .txt o .csv), o nullptr si solo se
//                desea ordenar y verificar en streaming sin guardar en disco.
//   chunk_bytes  Tamano del bloque en bytes (define el tamano de cada run).
//   stats        Estructura de salida con metricas y tiempos.
//
// Devuelve:
//   true si todo el pipeline completo fue exitoso y el orden global verificado.
// ---------------------------------------------------------------------------
bool run_external_sort(const char* input_path, std::size_t rows, std::size_t cols,
                       std::size_t num_threads, const char* output_path,
                       std::size_t chunk_bytes, ExternalSortStats& stats);

#endif  // EXTERNAL_SORT_HPP
