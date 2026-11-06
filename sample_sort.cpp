// ============================================================================
// sample_sort.cpp - Implementacion del muestreo y los splitters
//
// Proyecto 1 - Topicos de HPC (Sprint 3 - Julian)
//
// Este archivo construye el "plan" de Sample Sort: una lista de valores
// frontera (splitters) y una forma rapida de decidir a que cubeta va cada
// elemento. No mueve datos ni ordena cubetas; eso lo hara el scatter/gather.
// ============================================================================
#include "sample_sort.hpp"

#include "merge_sort.hpp"

// ---------------------------------------------------------------------------
// XorShift64 - PRNG pequeno y reproducible
// ---------------------------------------------------------------------------
// xorshift64* aplica tres desplazamientos y una multiplicacion final. Es muy
// rapido y suficiente para elegir una muestra; no se usa para criptografia.
//
// La semilla 0 se evita porque un estado 0 produce siempre 0 en xorshift.
// La constante 0x9e3779b97f4a7c15 es la razon dorada de 64 bits, un valor
// tipico para inicializar generadores cuando no hay semilla util.
XorShift64::XorShift64(std::uint64_t seed)
    : state(seed != 0 ? seed : 0x9e3779b97f4a7c15ULL) {}

std::uint64_t XorShift64::next() {
    std::uint64_t x = state;

    // Mezcla los bits del estado. Los desplazamientos 12, 25 y 27 son los del
    // generador xorshift64* clasico.
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state = x;

    // Multiplicacion final: mejora la calidad de los bits bajos, que son los
    // que se usan al calcular residuos (next() % algo).
    return x * 0x2545F4914F6CDD1DULL;
}

// ---------------------------------------------------------------------------
// random_sample - reservoir sampling en una pasada
// ---------------------------------------------------------------------------
// Idea del algoritmo:
//   - Los primeros k elementos entran directo al "reservorio".
//   - Para cada elemento i >= k, se elige un indice r en [0, i]. Si r < k, el
//     elemento reemplaza al que este en sample[r]; si no, se descarta.
//
// Esto garantiza que cada elemento del arreglo tenga la misma probabilidad de
// terminar en la muestra, sin reemplazo y sin conocer el total de antemano.
std::size_t random_sample(const double* data, std::size_t n, double* sample,
                          std::size_t sample_size, std::uint64_t seed) {
    if (data == nullptr || sample == nullptr || n == 0 || sample_size == 0) {
        return 0;
    }

    // No se puede muestrear mas elementos de los que existen.
    const std::size_t k = (sample_size < n) ? sample_size : n;

    // Llena el reservorio con los primeros k valores.
    for (std::size_t i = 0; i < k; ++i) {
        sample[i] = data[i];
    }

    XorShift64 rng(seed);
    for (std::size_t i = k; i < n; ++i) {
        // r es uniforme en [0, i]; hay i + 1 posiciones posibles.
        const std::uint64_t r =
            rng.next() % static_cast<std::uint64_t>(i + 1);

        // Con probabilidad k / (i + 1), el nuevo elemento entra al reservorio.
        if (r < static_cast<std::uint64_t>(k)) {
            sample[static_cast<std::size_t>(r)] = data[i];
        }
    }

    return k;
}

// ---------------------------------------------------------------------------
// choose_splitters - quantiles de la muestra ordenada
// ---------------------------------------------------------------------------
// Si se ordena la muestra y se corta en B partes iguales, los valores en las
// fronteras son una buena aproximacion de los quantiles del arreglo completo.
// Esa es la base de Sample Sort: con datos uniformes, cada cubeta recibe una
// cantidad similar de elementos.
std::size_t choose_splitters(double* sample, std::size_t sample_size,
                             std::size_t num_buckets, double* splitters) {
    if (sample == nullptr || splitters == nullptr || sample_size == 0 ||
        num_buckets < 2) {
        return 0;
    }

    // La muestra se ordena in-place; el llamador no la necesita desordenada.
    merge_sort(sample, sample_size);

    // Con B cubetas hacen falta B - 1 fronteras.
    const std::size_t num_splitters = num_buckets - 1;

    for (std::size_t i = 0; i < num_splitters; ++i) {
        // Para la frontera i se toma el punto (i + 1) / B de la muestra.
        // Ejemplo con B = 4: indices en 1/4, 2/4 y 3/4 de la muestra.
        std::size_t index = ((i + 1) * sample_size) / num_buckets;

        // Si la muestra es mas chica que B, la division entera puede repetir
        // indices. El clamp evita salir del arreglo; los splitters repetidos
        // solo producen cubetas vacias, lo cual es valido.
        if (index >= sample_size) {
            index = sample_size - 1;
        }

        splitters[i] = sample[index];
    }

    return num_splitters;
}

// ---------------------------------------------------------------------------
// find_bucket - upper_bound manual
// ---------------------------------------------------------------------------
// Se busca el primer splitter que NO sea menor que value:
//
//   splitters[mid] < value  ->  value esta a la derecha  -> low = mid + 1
//   splitters[mid] >= value ->  value esta a la izquierda -> high = mid
//
// Al terminar, low es la cantidad de splitters estrictamente menores que
// value, que es exactamente el indice de la cubeta.
//
// Invariante del ciclo:
//   - Todos los indices < low tienen splitter < value.
//   - Todos los indices >= high tienen splitter >= value.
//   - low <= high siempre.
std::size_t find_bucket(const double* splitters, std::size_t num_splitters,
                        double value) {
    if (splitters == nullptr || num_splitters == 0) {
        return 0;
    }

    std::size_t low = 0;
    std::size_t high = num_splitters;

    while (low < high) {
        // Se calcula el punto medio sin desbordar: low + (high - low) / 2.
        const std::size_t mid = low + (high - low) / 2;

        if (splitters[mid] < value) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    return low;
}

// ---------------------------------------------------------------------------
// build_sample_plan - une muestreo, ordenamiento y splitters
// ---------------------------------------------------------------------------
// Es la funcion de conveniencia que usara el scatter. Deja el vector
// `splitters` con exactamente num_buckets - 1 valores, listo para pasarse a
// find_bucket().
bool build_sample_plan(const double* data, std::size_t n,
                       std::size_t sample_size, std::size_t num_buckets,
                       std::uint64_t seed, std::vector<double>& splitters) {
    // La salida se limpia primero: si algo falla, el llamador no recibe un
    // vector con datos viejos de una corrida anterior.
    splitters.clear();

    if (data == nullptr || n == 0 || sample_size == 0 || num_buckets < 2) {
        return false;
    }

    // La muestra no puede ser mas grande que el arreglo.
    const std::size_t take = (sample_size < n) ? sample_size : n;

    // Solo se reserva espacio para la muestra, no para todo el arreglo.
    std::vector<double> sample(take);

    const std::size_t got =
        random_sample(data, n, sample.data(), take, seed);
    if (got == 0) {
        return false;
    }

    // Se reserva el tamano final esperado. choose_splitters() puede escribir
    // menos si los parametros no permiten construir todas las fronteras.
    const std::size_t num_splitters = num_buckets - 1;
    splitters.resize(num_splitters);

    const std::size_t written =
        choose_splitters(sample.data(), got, num_buckets, splitters.data());

    // Si se escribieron menos splitters, se ajusta el vector para que su
    // tamano real coincida con los datos validos.
    splitters.resize(written);

    return written == num_splitters;
}
