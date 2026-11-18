// ============================================================================
// external_sort.cpp - Implementacion del Ordenamiento Externo Out-of-Core
//
// Proyecto 1 - Topicos de HPC
// Fase 2: Ordenamiento de matrices que exceden la RAM fisica mediante
// External Merge Sort en 2 fases con activacion automatica.
// ============================================================================
#include "external_sort.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <queue>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "chunked_reader.hpp"
#include "file_io.hpp"
#include "merge_sort.hpp"
#include "scatter_gather.hpp"
#include "shared_memory.hpp"

namespace {

// ---------------------------------------------------------------------------
// Constantes y utilidades de tiempo
// ---------------------------------------------------------------------------
constexpr char kBinMagic[8] = {'M', 'A', 'T', 'B', 'I', 'N', '1', '\0'};
constexpr std::size_t kMergeBufferElements = 131072; // 1 MB por run (128K floats)
constexpr std::size_t kOutBufferElements   = 262144; // 2 MB buffer de salida

inline float ms_since(std::chrono::steady_clock::time_point begin) {
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<float, std::milli>(end - begin).count();
}

inline std::uint64_t checksum_bits_buffer(const float* data, std::size_t n) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < n; ++i) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &data[i], sizeof(bits));
        sum += bits;
    }
    return sum;
}

std::string format_run_path(std::size_t index) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "runs/run_%05zu.bin", index);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// RunReader - Lector con buffer y prefetch para un archivo binario de run
// ---------------------------------------------------------------------------
struct RunReader {
    int fd = -1;
    std::string path;
    std::size_t total_elements = 0;
    std::size_t elements_consumed = 0;

    std::vector<float> buffer;
    std::size_t buf_pos = 0;
    std::size_t buf_count = 0;

    bool open(const std::string& p, std::size_t buf_capacity) {
        path = p;
        fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            std::fprintf(stderr, "RunReader: no se pudo abrir '%s'\n", path.c_str());
            return false;
        }

        #ifdef POSIX_FADV_SEQUENTIAL
        ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
        #endif

        BinHeader header{};
        if (::read(fd, &header, sizeof(header)) != sizeof(header)) {
            std::fprintf(stderr, "RunReader: cabecera invalida en '%s'\n", path.c_str());
            close();
            return false;
        }

        total_elements = static_cast<std::size_t>(header.count);
        elements_consumed = 0;
        buffer.resize(buf_capacity);
        buf_pos = 0;
        buf_count = 0;
        return refill();
    }

    bool refill() {
        if (elements_consumed >= total_elements) {
            buf_count = 0;
            buf_pos = 0;
            return false;
        }

        std::size_t want = total_elements - elements_consumed;
        if (want > buffer.size()) want = buffer.size();

        const ssize_t bytes_to_read = static_cast<ssize_t>(want * sizeof(float));
        const ssize_t bytes_read = ::read(fd, buffer.data(), bytes_to_read);
        if (bytes_read <= 0) {
            buf_count = 0;
            buf_pos = 0;
            return false;
        }

        buf_count = static_cast<std::size_t>(bytes_read) / sizeof(float);
        buf_pos = 0;
        elements_consumed += buf_count;
        return buf_count > 0;
    }

    inline bool has_next() const {
        return buf_pos < buf_count;
    }

    inline float next() {
        const float val = buffer[buf_pos++];
        if (buf_pos >= buf_count) {
            refill();
        }
        return val;
    }

    void close() {
        if (fd >= 0) {
            #ifdef POSIX_FADV_DONTNEED
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            #endif
            ::close(fd);
            fd = -1;
        }
    }

    ~RunReader() { close(); }
};

// ---------------------------------------------------------------------------
// Nodo para el Min-Heap de K-Way Merge
// ---------------------------------------------------------------------------
struct HeapNode {
    float value;
    std::size_t run_idx;

    // Mayor que para obtener un Min-Heap (el menor elemento en la cima)
    bool operator>(const HeapNode& other) const {
        return value > other.value;
    }
};

}  // namespace

bool run_external_sort(const char* input_path, std::size_t rows, std::size_t cols,
                       std::size_t num_threads, const char* output_path,
                       std::size_t chunk_bytes, ExternalSortStats& stats) {
    stats = ExternalSortStats{};
    const auto total_clock_begin = std::chrono::steady_clock::now();

    if (input_path == nullptr || rows == 0 || cols == 0 || num_threads < 2) {
        std::fprintf(stderr, "run_external_sort: parametros invalidos\n");
        return false;
    }
    if (cols > static_cast<std::size_t>(-1) / rows) {
        std::fprintf(stderr, "run_external_sort: desbordamiento en rows * cols\n");
        return false;
    }

    const std::size_t total_expected = rows * cols;
    stats.total_values = total_expected;
    stats.chunk_mb = chunk_bytes / (1024 * 1024);
    if (stats.chunk_mb == 0) stats.chunk_mb = 1;

    // Crear directorio temporal runs/
    ::mkdir("runs", 0755);

    // Calcular capacidad del chunk en floats
    std::size_t chunk_values = chunk_bytes / sizeof(float);
    if (chunk_values == 0) chunk_values = 1;
    if (chunk_values > total_expected) chunk_values = total_expected;
    const std::size_t allocated_chunk_bytes = chunk_values * sizeof(float);

    std::printf("\n\n");
    std::printf("--- FASE 1: Generacion y ordenamiento de runs en paralelo ---\n");
    std::printf("\n");
    std::printf("Archivo de entrada : %s\n", input_path);
    std::printf("Dimensiones        : %zu x %zu (%zu valores)\n", rows, cols, total_expected);
    std::printf("Tamano de run      : %.2f MB (%zu valores)\n",
                static_cast<float>(allocated_chunk_bytes) / (1024.0 * 1024.0),
                chunk_values);
    std::printf("Hilos por run      : %zu (Sample Sort con memoria compartida)\n\n", num_threads);

    // Segmentos de memoria compartida reutilizables para ordenar cada run
    SharedMemory data_shm;
    if (!shared_create(data_shm, "/proyecto1_ext_data", allocated_chunk_bytes)) {
        return false;
    }

    SharedMemory temp_shm;
    if (!shared_create(temp_shm, "/proyecto1_ext_temp", allocated_chunk_bytes)) {
        shared_release(data_shm, true);
        return false;
    }

    SharedMemory ctrl_shm;
    if (!shared_create(ctrl_shm, "/proyecto1_ext_ctrl", sizeof(SharedControl))) {
        shared_release(temp_shm, true);
        shared_release(data_shm, true);
        return false;
    }

    SharedControl* control = static_cast<SharedControl*>(ctrl_shm.addr);
    if (!shared_control_init(control, num_threads)) {
        shared_release(ctrl_shm, true);
        shared_release(temp_shm, true);
        shared_release(data_shm, true);
        return false;
    }

    float* shm_data = static_cast<float*>(data_shm.addr);
    float* shm_temp = static_cast<float*>(temp_shm.addr);

    // Lector por chunks del archivo de entrada original
    ChunkedTextReader reader;
    if (!reader.open(input_path)) {
        shared_control_destroy(control);
        shared_release(ctrl_shm, true);
        shared_release(temp_shm, true);
        shared_release(data_shm, true);
        return false;
    }

    std::vector<std::string> run_files;
    std::vector<float> run_durations_ms;
    std::size_t total_read = 0;
    std::uint64_t accumulated_input_checksum = 0;

    const auto phase1_begin = std::chrono::steady_clock::now();

    while (total_read < total_expected) {
        std::size_t want = total_expected - total_read;
        if (want > chunk_values) want = chunk_values;

        std::size_t count = 0;
        if (!reader.next(shm_data, want, count)) {
            std::fprintf(stderr, "run_external_sort: error al leer chunk de entrada\n");
            break;
        }
        if (count == 0) break; // EOF

        // Sumar al checksum de entrada acumulado
        accumulated_input_checksum += checksum_bits_buffer(shm_data, count);

        // Ordenar el run en memoria usando Sample Sort paralelo
        const auto run_clock_begin = std::chrono::steady_clock::now();
        constexpr std::size_t kSampleSize = 10000;
        constexpr std::uint64_t kSeed = 42;

        bool sort_ok = false;
        if (count >= num_threads * 2) {
            ScatterGatherStats sg_stats;
            sort_ok = sample_sort_parallel(shm_data, shm_temp, count, num_threads,
                                           kSampleSize, kSeed, control, sg_stats);
        } else {
            // Caso borde: run muy pequeno, merge sort directo
            std::memcpy(shm_temp, shm_data, count * sizeof(float));
            merge_sort(shm_temp, count);
            sort_ok = true;
        }

        const float run_ms = ms_since(run_clock_begin);
        run_durations_ms.push_back(run_ms);

        if (!sort_ok) {
            std::fprintf(stderr, "run_external_sort: error al ordenar run %zu\n", run_files.size());
            break;
        }

        // Escribir el run ordenado a disco con cabecera BinHeader
        const std::string run_filename = format_run_path(run_files.size());
        std::FILE* run_fp = std::fopen(run_filename.c_str(), "wb");
        if (run_fp == nullptr) {
            std::fprintf(stderr, "run_external_sort: no se pudo crear '%s'\n", run_filename.c_str());
            break;
        }

        BinHeader header{};
        std::memcpy(header.magic, kBinMagic, sizeof(kBinMagic));
        header.version = 1;
        header.dtype = 8;
        header.rows = static_cast<std::uint64_t>(count);
        header.cols = 1;
        header.count = static_cast<std::uint64_t>(count);

        if (std::fwrite(&header, sizeof(header), 1, run_fp) != 1 ||
            std::fwrite(shm_temp, sizeof(float), count, run_fp) != count) {
            std::fprintf(stderr, "run_external_sort: error al escribir '%s'\n", run_filename.c_str());
            std::fclose(run_fp);
            break;
        }
        std::fclose(run_fp);

        run_files.push_back(run_filename);
        total_read += count;

        std::printf("  [Run %04zu] %zu valores ordenados en %8.2f ms -> %s\n",
                    run_files.size() - 1, count, run_ms, run_filename.c_str());
        std::fflush(stdout);
    }

    reader.close();

    // Liberar memoria compartida de la Fase 1 inmediatamente
    shared_control_destroy(control);
    shared_release(ctrl_shm, true);
    shared_release(temp_shm, true);
    shared_release(data_shm, true);

    stats.read_and_runs_ms = ms_since(phase1_begin);
    stats.num_runs = run_files.size();
    stats.input_checksum = accumulated_input_checksum;

    if (total_read != total_expected || run_files.empty()) {
        std::fprintf(stderr, "run_external_sort: fallo la generacion de runs (%zu/%zu valores)\n",
                     total_read, total_expected);
        // Limpiar runs parciales
        for (const auto& rf : run_files) ::unlink(rf.c_str());
        return false;
    }

    // Calcular estadisticas micro de los runs (promedio, desviacion estandar, min, max)
    float sum_t = 0.0;
    stats.min_run_ms = run_durations_ms[0];
    stats.max_run_ms = run_durations_ms[0];
    for (float t : run_durations_ms) {
        sum_t += t;
        if (t < stats.min_run_ms) stats.min_run_ms = t;
        if (t > stats.max_run_ms) stats.max_run_ms = t;
    }
    stats.avg_run_ms = sum_t / run_durations_ms.size();

    float sum_variance = 0.0;
    for (float t : run_durations_ms) {
        const float diff = t - stats.avg_run_ms;
        sum_variance += diff * diff;
    }
    stats.stddev_run_ms = std::sqrt(sum_variance / run_durations_ms.size());

    std::printf("\nFase 1 completada : %zu runs generados en %.2f ms\n",
                stats.num_runs, stats.read_and_runs_ms);
    std::printf("Estadistica runs  : Promedio %.2f ms +/- %.2f ms (min %.2f ms, max %.2f ms)\n",
                stats.avg_run_ms, stats.stddev_run_ms, stats.min_run_ms, stats.max_run_ms);

    // ========================================================================
    // FASE 2: K-Way Merge con Min-Heap y streaming
    // ========================================================================
    std::printf("\n\n");
    std::printf("--- FASE 2: K-Way Merge en streaming con Min-Heap ---\n");
    std::printf("\n");
    std::printf("Runs a mezclar     : %zu\n", stats.num_runs);
    std::printf("Buffer por run     : %.2f MB\n",
                static_cast<float>(kMergeBufferElements * sizeof(float)) / (1024.0 * 1024.0));
    std::printf("Destino de salida  : %s\n\n", output_path != nullptr ? output_path : "(solo verificacion)");

    const auto phase2_begin = std::chrono::steady_clock::now();

    // Abrir todos los RunReaders
    std::vector<RunReader> readers(stats.num_runs);
    for (std::size_t i = 0; i < stats.num_runs; ++i) {
        if (!readers[i].open(run_files[i], kMergeBufferElements)) {
            std::fprintf(stderr, "run_external_sort: error al abrir '%s' para merge\n", run_files[i].c_str());
            for (const auto& rf : run_files) ::unlink(rf.c_str());
            return false;
        }
    }

    // Inicializar el Min-Heap de K elementos
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> heap;
    for (std::size_t i = 0; i < stats.num_runs; ++i) {
        if (readers[i].has_next()) {
            heap.push(HeapNode{readers[i].next(), i});
        }
    }

    // Preparar archivo de salida si fue especificado
    bool is_bin_out = false;
    bool is_txt_out = false;
    std::FILE* out_fp = nullptr;
    std::vector<float> out_bin_buffer;
    std::size_t out_bin_count = 0;

    if (output_path != nullptr) {
        const std::string op = output_path;
        if (op.size() >= 4 && op.compare(op.size() - 4, 4, ".bin") == 0) {
            is_bin_out = true;
            out_fp = std::fopen(output_path, "wb");
            if (out_fp != nullptr) {
                BinHeader header{};
                std::memcpy(header.magic, kBinMagic, sizeof(kBinMagic));
                header.version = 1;
                header.dtype = 8;
                header.rows = static_cast<std::uint64_t>(rows);
                header.cols = static_cast<std::uint64_t>(cols);
                header.count = static_cast<std::uint64_t>(total_expected);
                std::fwrite(&header, sizeof(header), 1, out_fp);
                out_bin_buffer.resize(kOutBufferElements);
            }
        } else {
            is_txt_out = true;
            out_fp = std::fopen(output_path, "w");
        }

        if (out_fp == nullptr) {
            std::fprintf(stderr, "run_external_sort: advertencia: no se pudo crear archivo de salida '%s'\n", output_path);
        }
    }

    // Streaming del K-Way Merge con verificacion en caliente
    std::size_t merged_count = 0;
    std::uint64_t accumulated_output_checksum = 0;
    float prev_value = 0.0;
    bool is_first = true;
    bool sorted_check = true;
    float min_v = 0.0;
    float max_v = 0.0;

    const std::size_t progress_interval = total_expected / 10 == 0 ? 1 : total_expected / 10;

    while (!heap.empty()) {
        const HeapNode top = heap.top();
        heap.pop();

        const float val = top.value;
        const std::size_t run_idx = top.run_idx;

        // Verificacion en caliente de orden monotono no decreciente
        if (is_first) {
            prev_value = val;
            min_v = val;
            max_v = val;
            is_first = false;
        } else {
            if (val < prev_value) {
                sorted_check = false;
            }
            prev_value = val;
            if (val > max_v) max_v = val;
            if (val < min_v) min_v = val;
        }

        // Acumular checksum de bits
        std::uint64_t bits = 0;
        std::memcpy(&bits, &val, sizeof(bits));
        accumulated_output_checksum += bits;

        // Escritura bufferizada al archivo de salida
        if (out_fp != nullptr) {
            if (is_bin_out) {
                out_bin_buffer[out_bin_count++] = val;
                if (out_bin_count >= out_bin_buffer.size()) {
                    std::fwrite(out_bin_buffer.data(), sizeof(float), out_bin_count, out_fp);
                    out_bin_count = 0;
                }
            } else if (is_txt_out) {
                std::fprintf(out_fp, "%.10g ", val);
                if ((merged_count + 1) % cols == 0) {
                    std::fputc('\n', out_fp);
                }
            }
        }

        ++merged_count;
        if (merged_count % progress_interval == 0 || merged_count == total_expected) {
            std::printf("  Progreso Merge: %zu/%zu valores (%.1f%%)\n",
                        merged_count, total_expected,
                        100.0 * static_cast<float>(merged_count) / static_cast<float>(total_expected));
            std::fflush(stdout);
        }

        // Reponer el siguiente elemento del mismo run al heap
        if (readers[run_idx].has_next()) {
            heap.push(HeapNode{readers[run_idx].next(), run_idx});
        }
    }

    // Vaciar buffers de salida pendientes
    if (out_fp != nullptr) {
        if (is_bin_out && out_bin_count > 0) {
            std::fwrite(out_bin_buffer.data(), sizeof(float), out_bin_count, out_fp);
        }
        std::fclose(out_fp);
        stats.saved = true;
    }

    // Cerrar y eliminar todos los runs temporales
    for (std::size_t i = 0; i < stats.num_runs; ++i) {
        readers[i].close();
        ::unlink(run_files[i].c_str());
    }
    ::rmdir("runs");

    stats.merge_ms = ms_since(phase2_begin);
    stats.total_ms = ms_since(total_clock_begin);
    stats.output_checksum = accumulated_output_checksum;
    stats.min_value = min_v;
    stats.max_value = max_v;
    stats.sorted = sorted_check && (merged_count == total_expected);
    stats.checksum_ok = (stats.output_checksum == stats.input_checksum);

    std::printf("\n--- Resumen Final del Ordenamiento Externo ---\n");
    std::printf("Valores ordenados  : %zu / %zu\n", merged_count, total_expected);
    std::printf("Min / Max          : %g / %g\n", stats.min_value, stats.max_value);
    std::printf("Verificacion orden : %s\n", stats.sorted ? "ordenado en orden ascendente" : "ERROR");
    std::printf("Checksum de bits   : %llu vs %llu -> %s\n",
                static_cast<unsigned long long>(stats.output_checksum),
                static_cast<unsigned long long>(stats.input_checksum),
                stats.checksum_ok ? "OK (mismos elementos)" : "ERROR");
    std::printf("Tiempo Fase 1      : %.3f ms (I/O entrada + runs paralelos)\n", stats.read_and_runs_ms);
    std::printf("Tiempo Fase 2      : %.3f ms (K-Way Merge streaming)\n", stats.merge_ms);
    std::printf("Tiempo Total Pared : %.3f ms\n", stats.total_ms);
    if (stats.saved && output_path != nullptr) {
        std::printf("Archivo guardado   : %s\n", output_path);
    }

    return stats.sorted && stats.checksum_ok;
}
