# Proyecto 1 
<insertar imagen del logo cinvestav>
Centro de Investigación y de Estudios Avanzados del Instituto Politécnico Nacional

Unidad Zacatenco - Departamento de Computación

Tópicos selectos: Cómputo de alto desempeño I
Dr. José Rodríguez García

Francisco Javier Meléndez Ruiz
José Julián Sierra Álvarez
Maestría en Ciencias en Computación

## Introducción

En el marco de la materia de Tópicos Selectos: Cómputo de Alto Desempeño (HPC), este documento presenta el diseño, desarrollo y evaluación experimental del proyecto enfocado en el procesamiento y ordenamiento ascendente a gran escala de un archivo de datos masivo (*40,000 x 40,000* elementos, equivalente a *1,600,000,000* valores de tipo double y un tamaño en disco de aproximadamente 16.8 GB), este es el archivo proporcionado por el profesor. 
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

* **Aplicamos un Merge Sort Propio** Algoritmo base de ordenamiento interno basado en la estrategia *divide y vencerás* (top-down), de complejidad temporal determinista $\mathcal{O}(N \log N)$ y estabilidad garantizada. Se clasifica como un algoritmo out-of-place (fuera de lugar), ya que requiere un arreglo auxiliar de memoria temporal del mismo tamaño que el segmento a ordenar para realizar la mezcla de las mitades de manera eficiente.
* **Decisión de Métodos Algorítmicos** En las fases iniciales de diseño se ponderó el uso del algoritmo *Bucket Sort* tradicional con rangos fijos. Sin embargo, ante la necesidad de paralelizar el ordenamiento de grandes volúmenes de datos y despues de nuestra investigación principal nos reveló que ante datos sesgados, o distribuciones no uniformes, las cubetas habrían sufrido un desbalance de carga (inanición en unos hilos y saturación en otros). Por esta razón, se evolucionó la arquitectura hacia *Sample Sort* obtenido de la literatura: mel cual utiliza un muestreo aleatorio (*reservoir sampling*) para calcular dinámicamente puntos de corte (splitters) equidistantes. Esto garantiza matemáticamente un balanceo de carga óptimo. Operativamente, combina fases out-of-place durante la dispersión o scatter hacia un buffer temporal, con ordenamientos locales que pueden optimizarse en el uso de memoria.
* **Finalmente Nosotros Implementamos el External Merge Sort con K-Way Merge** Ante problemas de hardware al alojar estructuras masivas de *25.6 GB* en equipos con menos de esa cantidad de memoria RAM y Swap, incorporamos un sistema de ordenamiento externo de dos fases, la generación de bloques ordenados o runs en paralelo mediante Sample Sort, y su unificación posterior en streaming utilizando una estructura de datos de Min-Heap (Tournament Tree). Esta aproximación reduce drásticamente el espacio auxiliar requerido en memoria principal a solo unos cuantos megabytes dedicados a búferes de lectura.

### 2.4 Acceso a Memoria, Parte Secuencial Contra Parte Paralela

El rendimiento de los sistemas de alto rendimiento está condicionado por el ancho de banda del bus de memoria y las restricciones físicas del almacenamiento secundario. Todo sistema paralelo posee componentes intrínsecamente secuenciales (como el análisis léxico inicial de lectura de ficheros o la escritura final de resultados) y componentes altamente paralelizables (como el ordenamiento local de bloques). El análisis formal del impacto de estas fracciones sobre el rendimiento global se aborda mediante modelos matemáticos de aceleración en la sección de metodología. **a que se refiere con analisis formal?**

---

## 3. Metodología y Decisiones de Diseño

### 3.1 Transición de Bucket Sort a Sample Sort

En las sesiones iniciales de planeación colaborativa del equipo, se evaluó preliminarmente el uso del algoritmo *Bucket Sort* tradicional con rangos fijos de cubetas para repartir los datos. Sin embargo, identificamos un posible riesgo antes de recibir la matríz característica: ante conjuntos de datos sesgados o distribuciones no uniformes, las cubetas habrían sufrido un desbalance severo de carga, dejandonos a algunos hilos con multiples de elementos y a otros completamente ociosos, inhabilitando el paralelismo efectivo.

Para no tener que resolver esta vulnerabilidad teórica, tomamos la decision de cambiar hacia el paradigma de **Sample Sort**. Este enfoque intermedio aplica un muestreo aleatorio, conocido como *reservoir sampling*, que maneja un tamaño fijo sobre el conjunto de datos, ordena dicha muestra mediante un Merge Sort interno, y calcula dinámicamente k puntos de corte, ó *splitters* equidistantes. Esto garantiza matemáticamente un balanceo de carga óptimo entre los hilos trabajadores, independientemente de la distribución subyacente de los valores numéricos.

### 3.2 Arquitectura de Memoria Compartida e Interprocesos (POSIX IPC)

Para cumplir con la restricción de procesar un volumen masivo de datos sin duplicar los 12.8 GB en el espacio de memoria dinámico (*heap*) local de múltiples procesos, se diseñó una capa de abstracción basada en llamadas nativas del sistema operativo Unix, y esto se logró al implementar diversas herramientas, como la creación del segmento global, para el cual se utilizo `shm_open` en conjunto con `ftruncate` para reservar un objeto de memoria compartida con los permisos y dimensiones requeridas en el directorio temporal del kernel; despues de esto se nos ocurrio realizar el mapeo de memoria cero copias mediante `mmap`, tanto el proceso maestro como los hilos concurrentes proyectan el mismo bloque físico en sus respectivos espacios virtuales de direcciones, eliminando la sobrecarga de transferencia por tuberías o canales de copia, logrando una transferencia IPC eficiente. y tambien se implementaron primitivas de tipo `pthread_mutex_t` para una sincronizacion de exclusión mutua, inicializadas con el atributo de compartición entre procesos (`PTHREAD_PROCESS_SHARED`), lo que nos permitio lograr el objetivo de coordinar de manera segura el avance de los hilos sin generar contención excesiva en la ruta crítica.

### 3.3 Restricciones Físicas y Activación del Paradigma *Out-of-Core*

la lectura del archivo de entrada es inherentemente secuencial (un hilo). `ChunkedTextReader` usa `mmap` + `MADV_SEQUENTIAL` y libera páginas con `MADV_DONTNEED`, pero el parseo carácter a carácter no se paraleliza: el ancho de banda de I/O es el cuello de botella, en este proyecto, no la velocidad el CPU. El paralelismo se aplica únicamente al ordenamiento (scatter/gather + orden local de cubetas con pthreads).

**demostracion de este analisis formal de complejidad espacial?**
El análisis formal de complejidad espacial demostró que un esquema puramente en memoria requiere al menos *25.6 GB* (*2N x 8* bytes para alojar simultáneamente los arreglos `data` y `temp`), lo cual supera la capacidad de la memoria RAM física disponible y el espacio temporal asignado en el sistema de ficheros en memoria `/dev/shm` en el ambiente de desarrollo principal que empleamos.

Para superar esta limitación física sin recurrir al uso de *swap* en disco (lo cual degradaría el rendimiento catastróficamente debido al *page fault thrashing*), se implementó un sistema de **activación automática Out-of-Core (External Merge Sort)** en dos fases:

1. **Fase 1 (Generación de Runs Paralelos):** El archivo "matriz_a,txt" se lee en streaming mediante un lector optimizado por bloques de tamaño *M*, los cuales se cargan en la memoria compartida, se ordenan en paralelo mediante Sample Sort multihilo y se guardan en el disco como archivos binarios temporales, ubicados en `runs/run_XXXX.bin` del proyecto, estos archivos temporales evenutalmente son eliminados.


2. **Fase 2 (K-Way Merge en Streaming):** Los *K* bloques ordenados se unifican concurrentemente utilizando una estructura de datos de Min-Heap (*Tournament Tree*), emitiendo el flujo globalmente ordenado con un consumo mínimo de memoria RAM que se mantiene acotado por debajo de los *50 MB*.

### 3.4 Modelado Analítico y Ley de Amdahl

Para justificar formalmente las métricas de rendimiento y el comportamiento de escalabilidad del sistema, el tiempo total de ejecución se descompuso bajo el modelo de la **Ley de Amdahl** en dos fracciones:

**falta indicar como se obtuvo cual era la parte secuencial y paralela**
* **Fracción secuencial ($s = 23.43\%$):** Integrada por el análisis léxico inicial de lectura carácter a carácter del archivo de texto y la operación de mezcla final (*K-Way Merge*) hacia el disco.


* **Fracción paralela ($p = 76.57\%$):** Conformada por las rutinas intensivas de ordenamiento local mediante Sample Sort distribuido entre los hilos de ejecución.

Evaluando el modelo para *N = 12* hilos lógicos en el hardware de referencia:

$$S(12) = \frac{1}{(1 - p) + \frac{p}{12}} \approx 3.35\times$$

**Falta determinar la formula para aceleracion global/valor máximo?**
Finalmente podemos observar que al aplicar esta formula, nuestro calculo demuestra analíticamente que la aceleración global del sistema está acotada asintóticamente a un valor máximo de $S_{\max} \approx 4.27\times$, independientemente de la cantidad de núcleos adicionales que se integren al clúster, debido al peso ineludible de las operaciones de entrada/salida secuenciales.

### 3.5 Determinación Analítica de Parámetros ($M^*$ y $P^*$)

Para evitar la heurística basada en prueba y error, los parámetros operativos clave del sistema se derivaron analíticamente:
**Igual se realizaron pruebas heuristicas de diversos tamaños de chunks...**

**recordar que son las runs?**
* **Tamaño óptimo de chunks ($M^* \in [512\text{ MB}, 1024\text{ MB}$):** Se estableció $M = 1024\text{ MB}$ como el punto de equilibrio que minimiza la profundidad del Min-Heap en la fase de mezcla ($K=12$ runs), maximizando al mismo tiempo el rendimiento de lectura secuencial del almacenamiento NVMe sin rebasar la capacidad de `/dev/shm`.


* **Número de hilos óptimo ($P^* = 12$):** Se fijó en concordancia exacta con la cantidad de hilos lógicos (SMT) del procesador de prueba, logrando saturar eficientemente el bus de memoria DDR5 sin incurrir en penalizaciones por cambios de contexto (*context switching*) excesivos del planificador del sistema operativo.
**mencionar que era al respecto de el equipo empleado, redirigir a la parte del texto que menciona el sistema operativo usado, para P y M**

---

## Bibliografía

Referencias - Proyecto 1 (Tópicos de HPC)

[1] "CSV file management using C++," GeeksforGeeks, Jul. 11, 2025. [Online]. Available: https://www.geeksforgeeks.org/cpp/csv-file-management-using-c/. [Accesado el: Oct. 26, 2026].

[2] "Samplesort," Wikipedia, The Free Encyclopedia, Oct. 23, 2026. [Online]. Available: https://en.wikipedia.org/wiki/Samplesort. [Accesado el: Oct. 26, 2026].

[3] "std::strtod," cppreference.com. [Online]. Available: https://en.cppreference.com/w/cpp/string/byte/strtod. [Accesado el: Oct. 26, 2026].

[4] "std::fread," cppreference.com. [Online]. Available: https://en.cppreference.com/w/cpp/io/c/fread. [Accesado el: Oct. 26, 2026].

https://satyadeepmaheshwari.medium.com/sorting-large-datasets-with-limited-memory-the-chunked-merge-sort-approach-318275275c81

https://en.wikipedia.org/wiki/K-way_merge_algorithm

Thomas H. Cormen; Charles E. Leiserson; Ronald L. Rivest; Clifford Stein (2001). Introduction To Algorithms. MIT Press. pp. 28–29. ISBN 978-0-262-03293-3.

Shaffer, Clifford A. (2012-07-26). Data Structures and Algorithm Analysis in C++, Third Edition. Courier Corporation. ISBN 9780486172620.

bibliografía sobre:
sample sort (done)
    reservoir sampling
    sampling
paginacion
    fault page thrashing
memoria dinamica
    mmap
external merge sort
k-way-merge sort (done)
    out-of-core
    min-heap
        tournament tree
        
ley de amdhal
    aplicada en ordenamiento de matrices

