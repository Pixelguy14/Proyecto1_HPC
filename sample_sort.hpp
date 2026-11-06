// ============================================================================
// sample_sort.hpp - Muestreo, splitters y busqueda binaria (Sample Sort)
//
// Proyecto 1 - Topicos de HPC
// Sprint 3 (Julian): seleccionar una muestra aleatoria, ordenarla, elegir los
// splitters y decidir a que cubeta pertenece cada elemento.
//
// Sample Sort en tres ideas:
//   1. Muestrear una parte del arreglo y ordenarla.
//   2. Elegir "splitters" (valores frontera) equiespaciados en esa muestra.
//   3. Repartir cada elemento en una cubeta segun los splitters.
//
// Este modulo NO hace el scatter ni el gather; solo construye el "plan"
// (los splitters) y responde "a que cubeta va este valor". El Sprint 3 de Paco
// usa estas funciones para mover los datos y ordenar cada cubeta.
//
// Semantica de cubetas (importante para el Scatter/Gather del Sprint 3-Paco):
//   con splitters s[0..k-1], el elemento x va a la cubeta
//       find_bucket(...) = cantidad de splitters estrictamente menores que x
//   Es decir:
//       cubeta 0      : x <= s[0]
//       cubeta i      : s[i-1] < x <= s[i]
//       cubeta k      : x > s[k-1]
//
//   Los empates van a la cubeta de la izquierda (<=). Mantener esta regla es
//   clave para que conteo, scatter y gather sean consistentes.
// ============================================================================
#ifndef SAMPLE_SORT_HPP
#define SAMPLE_SORT_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// XorShift64 - generador pseudoaleatorio propio
// ---------------------------------------------------------------------------
// Se usa en lugar de <random> para que:
//   - el muestreo sea reproducible entre corridas con la misma semilla,
//   - no dependa de la implementacion de la biblioteca estandar,
//   - el costo por numero sea muy bajo.
//
// El estado nunca debe quedar en 0: xorshift se quedaria atrapado en 0. Por eso
// el constructor sustituye una semilla 0 por una constante distinta de cero.
struct XorShift64 {
    std::uint64_t state;

    // Inicializa el generador. Si seed == 0, usa un valor por defecto.
    explicit XorShift64(std::uint64_t seed);

    // Avanza el estado y devuelve un numero pseudoaleatorio de 64 bits.
    std::uint64_t next();
};

// ---------------------------------------------------------------------------
// random_sample - muestra aleatoria sin reemplazo
// ---------------------------------------------------------------------------
// Recorre data[0..n) una sola vez y escribe en `sample` una muestra aleatoria
// uniforme sin reemplazo, de tamano min(n, sample_size).
//
// Parametros:
//   data         Arreglo de entrada de `n` doubles.
//   n            Cantidad de elementos de entrada.
//   sample       Buffer de salida con espacio para al menos
//                min(n, sample_size) doubles.
//   sample_size  Tamano deseado de la muestra.
//   seed         Semilla del PRNG. La misma semilla produce la misma muestra.
//
// Devuelve:
//   La cantidad de elementos escritos en `sample`:
//   min(n, sample_size), o 0 si algun parametro es invalido.
//
// Notas:
//   - No modifica `data`.
//   - No reserva memoria: todo se escribe en el buffer del llamador.
//   - Usa reservoir sampling; no necesita conocer n de antemano para el
//     algoritmo, aunque aqui n se recibe como parametro.
// ---------------------------------------------------------------------------
std::size_t random_sample(const double* data, std::size_t n, double* sample,
                          std::size_t sample_size, std::uint64_t seed);

// ---------------------------------------------------------------------------
// choose_splitters - convierte una muestra en fronteras de cubetas
// ---------------------------------------------------------------------------
// Ordena `sample` (la modifica) con merge_sort y elige num_buckets - 1 valores
// equiespaciados en la muestra ordenada.
//
// Parametros:
//   sample       Muestra de `sample_size` valores. Se ordena in-place.
//   sample_size  Cantidad de valores en `sample`.
//   num_buckets  Cantidad de cubetas deseadas. Debe ser >= 2.
//   splitters    Buffer de salida para num_buckets - 1 valores.
//
// Devuelve:
//   num_buckets - 1 (cantidad de splitters escritos), o 0 si los parametros
//   son invalidos.
//
// Notas:
//   - Si sample_size < num_buckets, algunos splitters pueden repetirse; eso es
//     valido: las cubetas vacias simplemente no reciben elementos.
//   - Los splitters quedan en orden no decreciente.
// ---------------------------------------------------------------------------
std::size_t choose_splitters(double* sample, std::size_t sample_size,
                             std::size_t num_buckets, double* splitters);

// ---------------------------------------------------------------------------
// find_bucket - busqueda binaria del splitter
// ---------------------------------------------------------------------------
// Devuelve la cubeta a la que pertenece `value`, es decir, la cantidad de
// splitters estrictamente menores que `value`.
//
// Parametros:
//   splitters      Arreglo ordenado de `num_splitters` fronteras.
//   num_splitters  Cantidad de splitters (k).
//   value          Valor a clasificar.
//
// Devuelve:
//   Un indice en [0, num_splitters]. Con k splitters hay k + 1 cubetas.
//
// Notas:
//   - Implementa upper_bound a mano: primer indice cuyo splitter NO es menor
//     que value. No usa std::upper_bound.
//   - Si splitters == nullptr o num_splitters == 0, devuelve 0 (una sola cubeta).
// ---------------------------------------------------------------------------
std::size_t find_bucket(const double* splitters, std::size_t num_splitters,
                        double value);

// ---------------------------------------------------------------------------
// build_sample_plan - paquete completo de muestreo + splitters
// ---------------------------------------------------------------------------
// Hace el trabajo que necesitara el scatter:
//   1. Toma una muestra de tamano min(n, sample_size).
//   2. La ordena con merge_sort.
//   3. Calcula num_buckets - 1 splitters.
//   4. Deja el resultado en `splitters`.
//
// Parametros:
//   data         Arreglo de entrada de `n` doubles.
//   n            Numero de elementos.
//   sample_size  Tamano de la muestra (> 0).
//   num_buckets  Cantidad de cubetas (>= 2).
//   seed         Semilla del PRNG.
//   splitters    Salida: vector con num_buckets - 1 valores.
//
// Devuelve:
//   true   si se construyeron los num_buckets - 1 splitters.
//   false  si algun parametro es invalido o no se pudo construir el plan.
//
// Notas:
//   - `splitters` se limpia al entrar, incluso si la funcion falla.
//   - La muestra es local a la funcion; no se reserva memoria para todo el
//     arreglo, solo para min(n, sample_size) doubles.
// ---------------------------------------------------------------------------
bool build_sample_plan(const double* data, std::size_t n,
                       std::size_t sample_size, std::size_t num_buckets,
                       std::uint64_t seed, std::vector<double>& splitters);

#endif  // SAMPLE_SORT_HPP
