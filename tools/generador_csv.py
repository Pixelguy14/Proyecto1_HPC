#!/usr/bin/env python3
"""Genera una matriz de numeros aleatorios en formato CSV (sin encabezado).

Uso:
    python3 generador_csv.py                        # 1000 x 1000 -> matriz_1000x1000.csv
    python3 generador_csv.py -f 10000 -c 10000      # 10,000 x 10,000 
    python3 generador_csv.py -f 3 -c 4 -o chico.csv # matriz chica para depurar

El archivo resultante tiene una fila por linea y los valores separados por comas
"""

import argparse
import os
import random
import sys
import time


def generar(filas, columnas, minimo, maximo, semilla, salida):
    rng = random.Random(semilla)
    randint = rng.randint

    inicio = time.perf_counter()
    with open(salida, "w", encoding="ascii", newline="") as archivo:
        for _ in range(filas):
            archivo.write(",".join(str(randint(minimo, maximo)) for _ in range(columnas)))
            archivo.write("\n")
    fin = time.perf_counter()

    total = filas * columnas
    print(f"Archivo generado : {salida}")
    print(f"Dimensiones      : {filas} x {columnas} ({total} valores)")
    print(f"Rango de valores : [{minimo}, {maximo}]  semilla={semilla}")
    print(f"Tamano en disco  : {os.path.getsize(salida) / (1024 * 1024):.2f} MB")
    print(f"Tiempo generacion: {fin - inicio:.2f} s")


def main():
    parser = argparse.ArgumentParser(
        description="Genera una matriz de enteros aleatorios en CSV."
    )
    parser.add_argument("-f", "--filas", type=int, default=1000, help="numero de filas (default: 1000)")
    parser.add_argument("-c", "--columnas", type=int, default=1000, help="numero de columnas (default: 1000)")
    parser.add_argument("-o", "--salida", default=None, help="archivo de salida (default: matriz_<filas>x<columnas>.csv)")
    parser.add_argument("--minimo", type=int, default=0, help="valor minimo (default: 0)")
    parser.add_argument("--maximo", type=int, default=1000000, help="valor maximo (default: 1000000)")
    parser.add_argument("--semilla", type=int, default=42, help="semilla del generador aleatorio (default: 42)")
    args = parser.parse_args()

    if args.filas <= 0 or args.columnas <= 0:
        parser.error("filas y columnas deben ser enteros positivos")
    if args.minimo > args.maximo:
        parser.error("el minimo no puede ser mayor que el maximo")

    salida = args.salida or f"matriz_{args.filas}x{args.columnas}.csv"
    generar(args.filas, args.columnas, args.minimo, args.maximo, args.semilla, salida)
    return 0


if __name__ == "__main__":
    sys.exit(main())
