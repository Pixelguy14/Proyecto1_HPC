#!/usr/bin/env bash
# ============================================================================
# empaquetar_entrega.sh - Arma la carpeta de entrega del Proyecto 1
#
# Copia el codigo y los documentos a entrega/ e incluye matriz_a.txt.
# Si el sistema de archivos lo permite, usa un hardlink para no duplicar los
# ~16.8 GB del archivo de datos. Si no, copia y avisa.
#
# Uso:
#   ./empaquetar_entrega.sh            # crea/actualiza entrega/
#   DESTINO=otra_carpeta ./empaquetar_entrega.sh
# ============================================================================
set -euo pipefail

ORIGEN="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DESTINO="${DESTINO:-$ORIGEN/entrega}"
DATOS="$ORIGEN/matriz_a.txt"

if [[ ! -f "$DATOS" ]]; then
    echo "ERROR: no existe $DATOS" >&2
    echo "Coloca el archivo completo en la raiz del proyecto antes de empaquetar." >&2
    exit 1
fi

echo "Origen : $ORIGEN"
echo "Destino: $DESTINO"

mkdir -p "$DESTINO"

# Codigo y documentos del entregable (sin binarios ni objetos).
ARCHIVOS=(
    Makefile
    readme.md
    LICENSE
    main.cpp
    csv_parser.cpp
    csv_parser.hpp
    merge_sort.cpp
    merge_sort.hpp
    sort_worker.cpp
    sort_worker.hpp
    shared_memory.cpp
    shared_memory.hpp
    parallel_sort.cpp
    parallel_sort.hpp
    sample_sort.cpp
    sample_sort.hpp
    scatter_gather.cpp
    scatter_gather.hpp
    chunked_reader.cpp
    chunked_reader.hpp
    file_io.cpp
    file_io.hpp
    empaquetar_entrega.sh
)

for archivo in "${ARCHIVOS[@]}"; do
    if [[ -f "$ORIGEN/$archivo" ]]; then
        cp -f "$ORIGEN/$archivo" "$DESTINO/$archivo"
    else
        echo "AVISO: falta $archivo (se omite)" >&2
    fi
done

# matriz_a.txt: hardlink si se puede, copia si no.
DATOS_DESTINO="$DESTINO/matriz_a.txt"
if [[ -e "$DATOS_DESTINO" ]]; then
    echo "matriz_a.txt ya existe en el destino; se reutiliza."
elif ln "$DATOS" "$DATOS_DESTINO" 2>/dev/null; then
    echo "matriz_a.txt enlazado con hardlink (no ocupa espacio extra)."
else
    echo "Hardlink no disponible; copiando 16.8 GB (puede tardar)..."
    cp --reflink=auto "$DATOS" "$DATOS_DESTINO"
fi

echo
echo "Entrega lista en: $DESTINO"
echo "Compila con:      make"
echo "Prueba de lectura: ./ordenar_matriz matriz_a.txt 40000 40000 64"
echo
echo "Nota: el .bin de salida NO se entrega; lo genera el profesor al ejecutar"
echo "      el programa con --muestreo y luego lo valida con --verificar-bin."
