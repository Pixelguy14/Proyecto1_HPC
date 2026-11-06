// ============================================================================
// shared_memory.hpp - Memoria compartida y bloque de control
//
// Proyecto 1 - Topicos de HPC
// Sprint 2 (Paco): shm_open + ftruncate + mmap para el arreglo gigante y un
// bloque de control con mutex process-shared para los contadores de progreso.
//
// Se usa memoria compartida desde el Sprint 2 (aunque por ahora los hilos
// viven en un solo proceso) porque en el Sprint 4 los procesos hijos podran
// mapear exactamente los mismos segmentos.
//
// Ciclo de vida tipico:
//
//   SharedMemory data_shm;
//   shared_create(data_shm, "/proyecto1_hpc_data", bytes);   // crea + mapea
//   double* data = static_cast<double*>(data_shm.addr);      // usa
//   ...
//   shared_release(data_shm, true);                           // desmapea + unlink
//
// Para el bloque de control:
//
//   SharedControl* ctrl = static_cast<SharedControl*>(ctrl_shm.addr);
//   shared_control_init(ctrl, num_workers);   // inicializa mutex + contadores
//   ...
//   shared_control_destroy(ctrl);             // destruye mutex
//   shared_release(ctrl_shm, true);           // libera el segmento
//
// El orden importa: el mutex vive dentro del segmento, asi que debe destruirse
// antes de desmapear la memoria.
// ============================================================================
#ifndef SHARED_MEMORY_HPP
#define SHARED_MEMORY_HPP

#include <cstddef>
#include <pthread.h>

// ---------------------------------------------------------------------------
// SharedMemory - descriptor de un segmento POSIX
// ---------------------------------------------------------------------------
// Agrupa todo lo necesario para liberar el recurso correctamente. No reserva
// memoria por si mismo; solo guarda el resultado de shm_open + mmap.
struct SharedMemory {
    int fd = -1;              // descriptor de /dev/shm; -1 si no hay segmento
    void* addr = nullptr;     // direccion devuelta por mmap(); nullptr si no hay
    std::size_t bytes = 0;    // tamano mapeado, necesario para munmap()
    char name[64] = {0};      // nombre POSIX (con '/' inicial) para shm_unlink()
};

// ---------------------------------------------------------------------------
// shared_create - crea y mapea un segmento de memoria compartida
// ---------------------------------------------------------------------------
// Pasos internos:
//   1. Elimina cualquier segmento viejo con el mismo nombre (shm_unlink).
//   2. shm_open(O_CREAT | O_RDWR, 0600) para crear/abrir el objeto.
//   3. ftruncate() para darle el tamano exacto en bytes.
//   4. mmap(MAP_SHARED) para obtener una direccion usable por el proceso.
//
// Parametros:
//   shm    Salida: descriptor con fd, addr, bytes y name listos para usarse.
//   name   Nombre POSIX del segmento. Debe empezar con '/' (por ejemplo,
//          "/proyecto1_hpc_data") y caber en shm.name.
//   bytes  Tamano en bytes. Debe ser > 0.
//
// Devuelve:
//   true   si el segmento quedo creado y mapeado.
//   false  si algun parametro es invalido o falla shm_open/ftruncate/mmap.
//          En caso de fallo, libera lo que se haya creado; `shm` queda con
//          fd = -1 y addr = nullptr, en un estado seguro para shared_release().
//
// Notas:
//   - El contenido inicial del segmento no esta definido; el llamador debe
//     escribir los datos antes de leerlos.
//   - Si ya existia un segmento con ese nombre, se destruye. Esto evita
//     reutilizar por accidente un tamano distinto de una corrida anterior.
// ---------------------------------------------------------------------------
bool shared_create(SharedMemory& shm, const char* name, std::size_t bytes);

// ---------------------------------------------------------------------------
// shared_release - libera un segmento
// ---------------------------------------------------------------------------
// Desmapea (munmap), cierra el descriptor (close) y, si unlink == true,
// elimina el nombre del sistema (shm_unlink). Al final deja `shm` en cero para
// que no pueda usarse por accidente.
//
// Parametros:
//   shm     Segmento a liberar. Puede estar vacio; en ese caso no hace nada.
//   unlink  true para eliminar el nombre de /dev/shm. Usar false solo si otro
//           proceso todavia necesita abrir el mismo nombre.
//
// Notas:
//   - Es seguro llamarla sobre un SharedMemory ya liberado.
//   - No se debe llamar mientras algun hilo siga usando shm.addr.
// ---------------------------------------------------------------------------
void shared_release(SharedMemory& shm, bool unlink);

// ---------------------------------------------------------------------------
// SharedControl - datos compartidos entre workers
// ---------------------------------------------------------------------------
// Vive dentro de un segmento mmap para que pueda compartirse entre hilos y,
// mas adelante, entre procesos. El mutex se inicializa con
// PTHREAD_PROCESS_SHARED por esa razon.
struct SharedControl {
    // Protege los contadores de abajo. No protege `data`: cada worker escribe
    // solo en su propio bloque y no hay condicion de carrera sobre el arreglo.
    pthread_mutex_t mutex;

    // Workers que ya terminaron. Se incrementa al final de cada thread_entry().
    std::size_t completed_workers;

    // Workers esperados. Sirve para comparar contra completed_workers.
    std::size_t total_workers;

    // 0 = sin error. Se reserva para que el Sprint 4 pueda propagar fallos de
    // un proceso hijo al proceso coordinador.
    int error;
};

// ---------------------------------------------------------------------------
// shared_control_init - inicializa el mutex y los contadores
// ---------------------------------------------------------------------------
// Crea un pthread_mutex_t con atributo PTHREAD_PROCESS_SHARED. Ese atributo es
// el que permite que el mutex funcione aunque los workers sean procesos
// distintos (Sprint 4), no solo hilos del mismo proceso.
//
// Parametros:
//   control        Bloque ya mapeado en memoria compartida. No puede ser nulo.
//   total_workers  Cantidad de workers que se esperan en esta corrida.
//
// Devuelve:
//   true   si el mutex y los contadores quedaron inicializados.
//   false  si `control` es nulo o falla la inicializacion del mutex.
//
// Notas:
//   - Llamar una sola vez por segmento, antes de crear los hilos.
//   - El atributo temporal se destruye despues de inicializar el mutex; el
//     mutex conserva el atributo process-shared internamente.
// ---------------------------------------------------------------------------
bool shared_control_init(SharedControl* control, std::size_t total_workers);

// ---------------------------------------------------------------------------
// shared_control_destroy - destruye el mutex
// ---------------------------------------------------------------------------
// Debe llamarse cuando ya no queda ningun worker usando el bloque y antes de
// shared_release() (porque el mutex vive dentro de la memoria que se desmapea).
// Tambien reinicia los contadores para dejar el bloque limpio.
//
// Parametros:
//   control  Bloque a destruir. Si es nullptr, no hace nada.
//
// Notas:
//   - Destruir un mutex mientras otro hilo lo usa es comportamiento indefinido.
//     Por eso se llama despues de pthread_join().
// ---------------------------------------------------------------------------
void shared_control_destroy(SharedControl* control);

// ---------------------------------------------------------------------------
// shared_control_mark_done - registra que un worker termino
// ---------------------------------------------------------------------------
// Toma el mutex, incrementa completed_workers y lo libera. Es la unica
// operacion que modifica el contador, y por eso la unica que necesita
// sincronizacion.
//
// Parametros:
//   control  Bloque compartido. Si es nullptr, no hace nada.
//
// Notas:
//   - Se llama al final de thread_entry(), despues de ordenar el bloque.
//   - El mutex no esta en la ruta caliente del ordenamiento: solo se toma una
//     vez por worker, no una vez por elemento.
// ---------------------------------------------------------------------------
void shared_control_mark_done(SharedControl* control);

#endif  // SHARED_MEMORY_HPP
