# Proyecto 1 
Centro de Investigación y de Estudios Avanzados del Instituto Politécnico Nacional

Unidad Zacatenco - Departamento de Computación

Tópicos selectos: Cómputo de alto desempeño I
Dr. José Rodríguez García

Francisco Javier Meléndez Ruiz
José Julián Sierra Álvarez
Maestría en Ciencias en Computación

## Introducción

En el marco de la materia de Tópicos Selectos: Cómputo de Alto Desempeño (HPC), este documento presenta el diseño, desarrollo y evaluación experimental del proyecto enfocado en el procesamiento y ordenamiento ascendente a gran escala de un archivo de datos masivo ($40,000 \times 40,000$ elementos, equivalente a $1,600,000,000$ valores de tipo double y un tamaño en disco de aproximadamente 16.8 GB) proporcionado por el profesor. 
El desarrollo del trabajo fue llevado a cabo de manera colaborativa en conjunto por Francisco Javier Meléndez Ruiz y José Julián Sierra Álvarez, estableciendo como fecha límite de entrega el 20 de noviembre de 2026. Para cumplir con los requerimientos de la asignatura, el sistema fue implementado desde los cimientos mediante una filosofía de sistemas de bajo nivel, adoptando restricciones como la prohibición de utilizar bibliotecas externas para la gestión de archivos CSV (desarrollando un analizador léxico manual en streaming), y principalmente aprovechar el equipo usando las tecnicas aprendidas de programación multinúcleo, multiproceso y multihilo para maximizar el uso de los recursos de hardware disponibles. 
A lo largo de este reporte se detallarán las dificultades técnicas encontradas frente al volumen masivo de datos, los cambios de paradigma arquitectónico desde un procesamiento puramente en memoria hasta un esquema híbrido por segmentación.

---

## Marco Teórico

Para fundamentar la arquitectura del sistema, se estudiaron e implementaron los siguientes conceptos y tecnologías clave en el cómputo de alto rendimiento:

### 2.1 Multithreading, Multicore y SMT

El paralelismo a nivel de hilo se implementó mediante la interfaz estándar POSIX threads (`pthreads`), permitiendo distribuir cargas de trabajo discretas entre múltiples hilos de ejecución. Para la sincronización eficiente en entornos concurrentes sin incurrir en cuellos de botella en las rutas críticas, se utilizaron primitivas de exclusión mutua configuradas con el atributo de compartición de procesos (`PTHREAD_PROCESS_SHARED`). A nivel de hardware, el diseño explota la arquitectura *Simultaneous Multi-Threading* (SMT) de los núcleos lógicos para solapar la latencia de acceso a la memoria principal durante las operaciones de reubicación de datos.

### 2.2 Memoria Mapeada (`mmap`) y Procesamiento por Chunks

Para evitar la saturación de la memoria RAM del sistema al interactuar con ficheros masivos, se emplearon llamadas al sistema Unix de mapeo de archivos en memoria (`mmap`, `shm_open`, `ftruncate`). Asimismo, se integraron directivas de optimización de paginación mediante `madvise` (`MADV_SEQUENTIAL` y `MADV_DONTNEED`), permitiendo liberar de manera síncrona las páginas físicas ya procesadas y garantizando que el consumo de memoria residente (*Resident Set Size*, RSS) permanezca acotado a unos cuantos megabytes durante la lectura en streaming.

### 2.3 Algoritmos de Ordenamiento y Evolución de Paradigmas

* **Merge Sort Propio:** Algoritmo base de ordenamiento interno basado en la estrategia *divide y vencerás* (top-down), de complejidad temporal determinista $\mathcal{O}(N \log N)$ y estabilidad garantizada. Se clasifica como un algoritmo out-of-place (fuera de lugar), ya que requiere un arreglo auxiliar de memoria temporal del mismo tamaño que el segmento a ordenar para realizar la mezcla de las mitades de manera eficiente.
* **Decision de métodos Algorítmico (De Bucket Sort a Sample Sort):** En las fases iniciales de diseño se ponderó el uso del algoritmo *Bucket Sort* tradicional con rangos fijos. Sin embargo, ante la necesidad de paralelizar el ordenamiento de grandes volúmenes de datos y despues de nuestra investigación principal nos reveló que ante datos sesgados, o distribuciones no uniformes, las cubetas habrían sufrido un desbalance de carga (inanición en unos hilos y saturación en otros). Por esta razón, se evolucionó la arquitectura hacia *Sample Sort* obtenido de la literatura: mel cual utiliza un muestreo aleatorio (*reservoir sampling*) para calcular dinámicamente puntos de corte (splitters) equidistantes. Esto garantiza matemáticamente un balanceo de carga óptimo. Operativamente, combina fases out-of-place durante la dispersión o scatter hacia un buffer temporal, con ordenamientos locales que pueden optimizarse en el uso de memoria.
* **External Merge Sort / K-Way Merge:** Ante problemas de hardware al alojar estructuras masivas de $25.6\text{ GB}$ en equipos con menos de esa cantidad de memoria RAM y Swap, incorporamos un sistema de ordenamiento externo de dos fases, la generación de bloques ordenados o runs en paralelo mediante Sample Sort, y su unificación posterior en streaming utilizando una estructura de datos de Min-Heap (Tournament Tree). Esta aproximación reduce drásticamente el espacio auxiliar requerido en memoria principal a solo unos cuantos megabytes dedicados a búferes de lectura.

### 2.4 Acceso a Memoria, Parte Secuencial Contra Parte Paralela

El rendimiento de los sistemas de alto rendimiento está condicionado por el ancho de banda del bus de memoria y las restricciones físicas del almacenamiento secundario. Todo sistema paralelo posee componentes intrínsecamente secuenciales (como el análisis léxico inicial de lectura de ficheros o la escritura final de resultados) y componentes altamente paralelizables (como el ordenamiento local de bloques). El análisis formal del impacto de estas fracciones sobre el rendimiento global se aborda mediante modelos matemáticos de aceleración en la sección de metodología.

---
