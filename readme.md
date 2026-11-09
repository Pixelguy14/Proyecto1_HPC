# Proyecto 1 HPC
## Descripción
Ordenamiento de matrices de 10,000x10,000 mediante el uso de samplesort implementado para procesamiento multinucleo y multihilo


## Instalación

## Uso local / Cargar Interfaz de Usuario

```bash
make

# Solo lectura
./leer_csv matriz_1000x1000.csv 1000 1000

# Secuencial: Merge Sort + guardado
./leer_csv matriz_1000x1000.csv 1000 1000 ordenada.csv

# Sprint 2: ordenamiento por bloques con hilos
./leer_csv matriz_1000x1000.csv 1000 1000 --hilos 8

# Sprint 3: Sample Sort completo con scatter/gather
./leer_csv matriz_1000x1000.csv 1000 1000 --muestreo 8 ordenada_sample.csv
```

Generar datos de prueba:

```bash
python3 tools/generador_csv.py -f 1000 -c 1000
python3 tools/generador_csv.py -f 10000 -c 10000
```

## Caracteristicas principales

## Licencia
[LICENCIA MIT](LICENSE)