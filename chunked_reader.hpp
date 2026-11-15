// ============================================================================
// chunked_reader.hpp - Lectura secuencial por chunks con mmap
//
// Proyecto 1 - Topicos de HPC
// Fase 1 de entrega: leer matrices grandes sin cargar el archivo completo en
// heap. El archivo se mapea con mmap(PROT_READ, MAP_PRIVATE) y el sistema
// operativo trae las paginas bajo demanda; ChunkedTextReader las libera con
// madvise(MADV_DONTNEED) cuando ya fueron parseadas.
//
// Este lector es la base para el ordenamiento externo: el siguiente paso es
// generar runs ordenados de ~512 MB y mezclarlos con un k-way merge.
// ============================================================================
#ifndef CHUNKED_READER_HPP
#define CHUNKED_READER_HPP

#include <cstddef>
#include <string>

// ---------------------------------------------------------------------------
// ChunkedTextReader
// ---------------------------------------------------------------------------
// Lee numeros separados por comas, espacios, tabs, saltos de linea o CR.
// No guarda el arreglo completo: cada llamada a next() entrega hasta `capacity`
// valores en el buffer del llamador.
//
// Uso tipico:
//   ChunkedTextReader reader;
//   if (!reader.open(path)) return false;
//   std::vector<double> buffer(chunk_values);
//   std::size_t count = 0;
//   while (reader.next(buffer.data(), buffer.size(), count) && count > 0) {
//       procesar(buffer.data(), count);
//   }
//   reader.close();
//
// El destructor cierra el mapping, asi que no hace falta llamar close() de
// forma explicita, pero hacerlo permite detectar errores antes.
class ChunkedTextReader {
public:
    ChunkedTextReader() = default;
    ~ChunkedTextReader();

    // No se permite copiar: el objeto posee un descriptor y un mapping.
    ChunkedTextReader(const ChunkedTextReader&) = delete;
    ChunkedTextReader& operator=(const ChunkedTextReader&) = delete;

    // Abre el archivo, lo mapea en solo lectura y activa MADV_SEQUENTIAL.
    // Devuelve false si no se puede abrir, medir o mapear.
    bool open(const char* path);

    // Desmapea y cierra el descriptor. Es seguro llamarlo mas de una vez.
    void close();

    // Lee hasta `capacity` valores en `dst` y deja en `count` cuantos leyo.
    // Devuelve false solo si encuentra un token invalido o demasiado largo.
    // Al llegar al final del archivo devuelve true con count == 0.
    bool next(double* dst, std::size_t capacity, std::size_t& count);

    bool eof() const { return cursor_ >= size_; }
    std::size_t file_size() const { return size_; }
    std::size_t bytes_read() const { return cursor_; }
    const std::string& path() const { return path_; }

private:
    // Libera con MADV_DONTNEED las paginas completamente anteriores al cursor.
    // Nunca libera la pagina donde empieza el siguiente token.
    void release_processed_pages();

    int fd_ = -1;
    const char* base_ = nullptr;
    std::size_t size_ = 0;
    std::size_t cursor_ = 0;
    std::size_t released_ = 0;
    std::string path_;
};

#endif  // CHUNKED_READER_HPP
