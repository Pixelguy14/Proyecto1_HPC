// ============================================================================
// shared_memory.cpp - Implementacion de shm_open/ftruncate/mmap y mutex
//
// Proyecto 1 - Topicos de HPC (Sprint 2 - Paco)
//
// Tres recursos POSIX se combinan aqui:
//   - shm_open(): crea/abre un objeto en /dev/shm.
//   - ftruncate(): le da un tamano concreto.
//   - mmap(): lo vuelve direccionable como un arreglo normal.
//
// El mutex se inicializa aparte, con atributo process-shared, para que el
// mismo bloque de control sirva tanto con hilos como, en el Sprint 4, con
// procesos.
// ============================================================================
#include "shared_memory.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// shared_create - creacion completa del segmento
// ---------------------------------------------------------------------------
// El orden de las llamadas es importante:
//   unlink viejo -> shm_open -> ftruncate -> mmap
//
// Si algo falla, se libera exactamente lo que ya se habia creado. Asi no se
// quedan descriptores abiertos ni nombres huerfanos en /dev/shm.
bool shared_create(SharedMemory& shm, const char* name, std::size_t bytes) {
    // Se parte de un descriptor vacio para que shared_release() sea seguro
    // incluso si la validacion falla de inmediato.
    shm = SharedMemory{};

    if (name == nullptr || name[0] == '\0' || bytes == 0) {
        std::fprintf(stderr, "shared_create: nombre o tamano invalido\n");
        return false;
    }

    // Se copia el nombre porque shm_unlink() lo necesitara despues; el puntero
    // original podria dejar de ser valido.
    std::snprintf(shm.name, sizeof(shm.name), "%s", name);

    // Limpia un segmento viejo de una corrida anterior. Si no existe, el error
    // ENOENT es normal y se ignora: el objetivo es que shm_open cree uno nuevo
    // con el tamano actual, no reutilizar uno con tamano distinto.
    ::shm_unlink(name);

    // O_CREAT crea el objeto si no existe; O_RDWR permite leer y escribir.
    // 0600 = solo el usuario dueno puede acceder.
    const int fd = ::shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        std::fprintf(stderr, "shared_create: shm_open('%s') fallo: %s\n", name,
                     std::strerror(errno));
        return false;
    }

    // Un objeto recien creado mide 0 bytes. ftruncate() le da el tamano exacto
    // que se va a mapear.
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        std::fprintf(stderr, "shared_create: ftruncate('%s', %zu) fallo: %s\n",
                     name, bytes, std::strerror(errno));
        // Limpieza parcial: cerrar descriptor y borrar el nombre.
        ::close(fd);
        ::shm_unlink(name);
        return false;
    }

    // MAP_SHARED hace que los cambios se vean en el objeto compartido, no en
    // una copia privada del proceso. PROT_READ | PROT_WRITE da acceso completo.
    void* addr = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        std::fprintf(stderr, "shared_create: mmap('%s') fallo: %s\n", name,
                     std::strerror(errno));
        ::close(fd);
        ::shm_unlink(name);
        return false;
    }

    // Solo se publica el resultado cuando todo salio bien.
    shm.fd = fd;
    shm.addr = addr;
    shm.bytes = bytes;
    return true;
}

// ---------------------------------------------------------------------------
// shared_release - liberacion ordenada
// ---------------------------------------------------------------------------
// Se desmapea primero (mientras el descriptor sigue abierto), luego se cierra
// el descriptor y al final se elimina el nombre. Cualquier orden que cierre el
// descriptor antes de munmap tambien seria valido, pero este es el habitual:
// el mapeo sigue vivo aunque el fd se cierre, y se prefiere liberar el mapeo
// antes de soltar la referencia al objeto.
void shared_release(SharedMemory& shm, bool unlink) {
    if (shm.addr != nullptr) {
        ::munmap(shm.addr, shm.bytes);
    }
    if (shm.fd >= 0) {
        ::close(shm.fd);
    }
    if (unlink && shm.name[0] != '\0') {
        ::shm_unlink(shm.name);
    }

    // Se deja el descriptor en cero para que un segundo release sea inofensivo
    // y para que nadie use un puntero que ya fue desmapeado.
    shm = SharedMemory{};
}

// ---------------------------------------------------------------------------
// shared_control_init - mutex con alcance entre procesos
// ---------------------------------------------------------------------------
// PTHREAD_PROCESS_SHARED es la pieza clave: sin ese atributo, el mutex solo
// funcionaria entre hilos del mismo proceso. Con el, el mismo pthread_mutex_t
// puede usarse desde procesos distintos que mapeen el mismo segmento.
bool shared_control_init(SharedControl* control, std::size_t total_workers) {
    if (control == nullptr) {
        std::fprintf(stderr, "shared_control_init: control nulo\n");
        return false;
    }

    // El atributo es temporal: se usa para inicializar el mutex y se destruye
    // enseguida. El mutex conserva internamente la propiedad process-shared.
    pthread_mutexattr_t attr;
    int rc = pthread_mutexattr_init(&attr);
    if (rc != 0) {
        std::fprintf(stderr, "shared_control_init: pthread_mutexattr_init: %s\n",
                     std::strerror(rc));
        return false;
    }

    rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (rc != 0) {
        std::fprintf(stderr,
                     "shared_control_init: pthread_mutexattr_setpshared: %s\n",
                     std::strerror(rc));
        pthread_mutexattr_destroy(&attr);
        return false;
    }

    // Se inicializa el mutex dentro del segmento compartido. A partir de aqui
    // el bloque puede usarse desde cualquier hilo o proceso que lo mapee.
    rc = pthread_mutex_init(&control->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (rc != 0) {
        std::fprintf(stderr, "shared_control_init: pthread_mutex_init: %s\n",
                     std::strerror(rc));
        return false;
    }

    control->completed_workers = 0;
    control->total_workers = total_workers;
    control->error = 0;
    return true;
}

// ---------------------------------------------------------------------------
// shared_control_destroy - fin del mutex
// ---------------------------------------------------------------------------
// Se debe invocar despues de que todos los workers terminaron (pthread_join)
// y antes de desmapear el segmento. Destruir un mutex en uso es UB.
void shared_control_destroy(SharedControl* control) {
    if (control == nullptr) return;

    pthread_mutex_destroy(&control->mutex);

    // Se deja el bloque limpio por si el segmento se reutiliza.
    control->completed_workers = 0;
    control->total_workers = 0;
    control->error = 0;
}

// ---------------------------------------------------------------------------
// shared_control_mark_done - incremento atomico a nivel de mutex
// ---------------------------------------------------------------------------
// Es la unica escritura sincronizada del bloque. Se hace al final del worker,
// no durante el ordenamiento, para que la contencion sea minima.
void shared_control_mark_done(SharedControl* control) {
    if (control == nullptr) return;

    pthread_mutex_lock(&control->mutex);
    ++control->completed_workers;
    pthread_mutex_unlock(&control->mutex);
}
