// ============================================================================
// chunked_reader.cpp - Implementacion de la lectura por chunks con mmap
//
// Proyecto 1 - Topicos de HPC
// Fase 1 de entrega.
// ============================================================================
#include "chunked_reader.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Mismos separadores que csv_parser.cpp: coma, salto de linea, CR, espacio y
// tabulador. Asi el mismo lector sirve para .csv y para .txt.
inline bool is_separator(char c) {
    return c == ',' || c == '\n' || c == '\r' || c == ' ' || c == '\t';
}

}  // namespace

ChunkedTextReader::~ChunkedTextReader() { close(); }

bool ChunkedTextReader::open(const char* path) {
    close();

    if (path == nullptr || path[0] == '\0') {
        std::fprintf(stderr, "ChunkedTextReader: ruta invalida\n");
        return false;
    }

    path_ = path;
    fd_ = ::open(path, O_RDONLY);
    if (fd_ < 0) {
        std::fprintf(stderr, "ChunkedTextReader: open('%s') fallo: %s\n", path,
                     std::strerror(errno));
        return false;
    }

    struct stat st;
    if (::fstat(fd_, &st) != 0) {
        std::fprintf(stderr, "ChunkedTextReader: fstat('%s') fallo: %s\n", path,
                     std::strerror(errno));
        close();
        return false;
    }
    if (st.st_size < 0) {
        std::fprintf(stderr, "ChunkedTextReader: tamano invalido en '%s'\n",
                     path);
        close();
        return false;
    }

    size_ = static_cast<std::size_t>(st.st_size);
    cursor_ = 0;
    released_ = 0;

    if (size_ == 0) {
        return true;  // archivo vacio: eof() es true
    }

    void* addr = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (addr == MAP_FAILED) {
        std::fprintf(stderr, "ChunkedTextReader: mmap('%s') fallo: %s\n", path,
                     std::strerror(errno));
        close();
        return false;
    }
    base_ = static_cast<const char*>(addr);

    // Lectura secuencial: el kernel puede hacer read-ahead y descartar paginas
    // limpias con facilidad. DONTDUMP evita que un core dump incluya 16.8 GB.
    ::madvise(const_cast<char*>(base_), size_, MADV_SEQUENTIAL);
#ifdef MADV_DONTDUMP
    ::madvise(const_cast<char*>(base_), size_, MADV_DONTDUMP);
#endif

    return true;
}

void ChunkedTextReader::close() {
    if (base_ != nullptr) {
        ::munmap(const_cast<char*>(base_), size_);
        base_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    size_ = 0;
    cursor_ = 0;
    released_ = 0;
    path_.clear();
}

void ChunkedTextReader::release_processed_pages() {
    if (base_ == nullptr || cursor_ <= released_) return;

    const long page_long = ::sysconf(_SC_PAGESIZE);
    if (page_long <= 0) return;
    const std::size_t page = static_cast<std::size_t>(page_long);

    // Redondear hacia abajo: solo se liberan paginas completas anteriores al
    // cursor. La pagina que contiene el siguiente token queda intacta.
    const std::size_t end = cursor_ & ~(page - 1);
    if (end > released_) {
        ::madvise(const_cast<char*>(base_) + released_, end - released_,
                  MADV_DONTNEED);
        released_ = end;
    }
}

bool ChunkedTextReader::next(double* dst, std::size_t capacity,
                             std::size_t& count) {
    count = 0;
    if (dst == nullptr || capacity == 0) {
        std::fprintf(stderr,
                     "ChunkedTextReader::next: buffer nulo o capacidad cero\n");
        return false;
    }
    if (base_ == nullptr || cursor_ >= size_) {
        return true;  // fin del archivo
    }

    const char* const begin = base_;
    const char* p = base_ + cursor_;
    const char* const end = base_ + size_;
    char token[64];  // cualquier double cabe de sobra

    while (count < capacity && p < end) {
        while (p < end && is_separator(*p)) ++p;
        if (p >= end) break;

        const char* const token_begin = p;
        while (p < end && !is_separator(*p)) ++p;
        const std::size_t len = static_cast<std::size_t>(p - token_begin);

        if (len >= sizeof(token)) {
            std::fprintf(stderr,
                         "ChunkedTextReader::next: token demasiado largo en "
                         "el byte %zu de '%s'\n",
                         static_cast<std::size_t>(token_begin - begin),
                         path_.c_str());
            cursor_ = static_cast<std::size_t>(p - begin);
            return false;
        }

        std::memcpy(token, token_begin, len);
        token[len] = '\0';

        char* next = nullptr;
        const double value = std::strtod(token, &next);
        if (next == token || *next != '\0') {
            std::fprintf(stderr,
                         "ChunkedTextReader::next: valor invalido \"%s\" en "
                         "el byte %zu de '%s'\n",
                         token,
                         static_cast<std::size_t>(token_begin - begin),
                         path_.c_str());
            cursor_ = static_cast<std::size_t>(p - begin);
            return false;
        }

        dst[count++] = value;
    }

    cursor_ = static_cast<std::size_t>(p - begin);
    release_processed_pages();
    return true;
}
