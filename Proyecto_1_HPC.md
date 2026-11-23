\noindent
\begin{minipage}[c]{0.23\textwidth}
  \centering
  \includegraphics[width=\linewidth]{Imagenes_reporte/cinves_logo.png}
\end{minipage}
\hfill
\begin{minipage}[c]{0.73\textwidth}
  {\Huge \textbf{Proyecto 1}}\\[0.3cm]
  \textbf{Centro de Investigación y de Estudios Avanzados del Instituto Politécnico Nacional}\\
  Unidad Zacatenco - Departamento de Computación\\[0.3cm]
  \textbf{Tópicos selectos: Cómputo de alto desempeño I}\\
  Dr. José Rodríguez García\\[0.3cm]
  Francisco Javier Meléndez Ruiz\\
  José Julián Sierra Álvarez\\
  \textit{Maestría en Ciencias en Computación}
\end{minipage}
\vspace{0.5cm}
\hrule
\vspace{0.5cm}

---

## 1. Introducción

En el marco de la materia de Tópicos Selectos: Cómputo de Alto Desempeño (HPC), este documento presenta el diseño, desarrollo y evaluación experimental del proyecto enfocado en el procesamiento y ordenamiento ascendente a gran escala de un archivo de datos masivo ($40,000 \times 40,000$ elementos, equivalente a $1,600,000,000$ valores de tipo `float` de precisión simple con una huella en memoria binaria de $5.96\text{ GiB}$ [$6.4\text{ GB}$] y un tamaño en disco de aproximadamente $16.8\text{ GB}$ en formato de texto ASCII), este es el archivo proporcionado por el profesor.

El desarrollo del trabajo fue llevado a cabo de manera colaborativa en conjunto por Francisco Javier Meléndez Ruiz y José Julián Sierra Álvarez, establecida como fecha límite de entrega el 20 de noviembre de 2026. Para cumplir con los requerimientos de la asignatura, el sistema fue implementado desde los cimientos mediante una filosofía de sistemas de bajo nivel, adoptando restricciones como la prohibición de utilizar bibliotecas externas para la gestión de archivos CSV —desarrollando un analizador léxico manual en *streaming* respaldado por rutinas estándar de lectura y conversión de bajo nivel [1, 2, 3]—, y principalmente aprovechar el equipo usando las técnicas aprendidas de programación multinúcleo, multiproceso y multihilo para maximizar el uso de los recursos de hardware disponibles [4].

A lo largo de este reporte se detallarán las dificultades técnicas encontradas frente al volumen masivo de datos, los cambios de paradigma arquitectónico desde un procesamiento puramente en memoria hasta un esquema híbrido por segmentación.

---

## 2. Marco Teórico

Para fundamentar la arquitectura del sistema, se estudiaron e implementaron los siguientes conceptos y tecnologías clave en el cómputo de alto rendimiento:

### 2.1 Multithreading, Multicore y SMT

El paralelismo a nivel de hilo se implementó mediante la interfaz estándar POSIX threads (`pthreads`), permitiendo distribuir cargas de trabajo discretas entre múltiples hilos de ejecución [4, 5]. Para la sincronización eficiente en entornos concurrentes sin incurrir en cuellos de botella en las rutas críticas, se utilizaron primitivas de exclusión mutua configuradas con el atributo de compartición de procesos (`PTHREAD_PROCESS_SHARED`) [5, 6]. A nivel de hardware, el diseño explota la arquitectura *Simultaneous Multi-Threading* (SMT) de los núcleos lógicos para solapar la latencia de acceso a la memoria principal durante las operaciones de reubicación de datos [4].

### 2.2 Memoria Mapeada (`mmap`) y Procesamiento por Chunks

Para evitar la saturación de la memoria RAM del sistema al interactuar con ficheros masivos, se emplearon llamadas al sistema Unix de mapeo de archivos en memoria (`mmap`, `shm_open`, `ftruncate`) [5, 7, 8]. Asimismo, se integraron directivas de optimización de paginación mediante `madvise` (`MADV_SEQUENTIAL` y `MADV_DONTNEED`), permitiendo liberar de manera síncrona las páginas físicas ya procesadas y garantizando que el consumo de memoria residente (*Resident Set Size*, RSS) permanezca acotado a unos cuantos megabytes durante la lectura en *streaming* [5, 7].

### 2.3 Algoritmos de Ordenamiento y Evolución de Paradigmas

* **Aplicamos un Merge Sort Propio:** Algoritmo base de ordenamiento interno basado en la estrategia *divide y vencerás* (*top-down*), de complejidad temporal determinista $\mathcal{O}(N \log N)$ y estabilidad garantizada [9]. Se clasifica como un algoritmo *out-of-place* (fuera de lugar), ya que requiere un arreglo auxiliar de memoria temporal del mismo tamaño que el segmento a ordenar para realizar la mezcla de las mitades de manera eficiente [9].
* **Decisión de Métodos Algorítmicos:** En las fases iniciales de diseño se ponderó el uso del algoritmo *Bucket Sort* tradicional con rangos fijos. Sin embargo, ante la necesidad de paralelizar el ordenamiento de grandes volúmenes de datos, nuestra investigación principal nos reveló que ante datos sesgados o distribuciones no uniformes, las cubetas habrían sufrido un desbalance de carga (inanición en unos hilos y saturación en otros) [10]. Por esta razón, se evolucionó la arquitectura hacia *Sample Sort* obtenido de la literatura [10, 11]: el cual utiliza un muestreo aleatorio (*reservoir sampling*) [12] para calcular dinámicamente puntos de corte (*splitters*) equidistantes. Esto garantiza matemáticamente un balanceo de carga óptimo [10, 11]. Operativamente, combina fases *out-of-place* durante la dispersión o *scatter* hacia un buffer temporal, con ordenamientos locales que pueden optimizarse en el uso de memoria.
* **Finalmente Nosotros Implementamos el External Merge Sort con K-Way Merge:** Ante problemas de hardware al alojar estructuras masivas de $11.92\text{ GB}$ en memoria compartida ($17.88\text{ GB}$ en memoria RAM con búferes) en equipos con cuota limitada en `/dev/shm` ($7.39\text{ GB}$) y memoria RAM disponible (~$9.75\text{ GB}$), incorporamos un sistema de ordenamiento externo de dos fases [13, 14, 15]: la generación de bloques ordenados o *runs* en paralelo mediante *Sample Sort*, y su unificación posterior en *streaming* utilizando una estructura de datos de *Min-Heap* (*Tournament Tree*) [13, 16]. Esta aproximación reduce drásticamente el espacio auxiliar requerido en memoria principal a solo unos cuantos megabytes dedicados a búferes de lectura, siguiendo los modelos teóricos de algoritmos eficientes en entrada/salida (*I/O-efficient algorithms*) [17, 18].

### 2.4 Acceso a Memoria, Parte Secuencial Contra Parte Paralela

El rendimiento de los sistemas de alto rendimiento está condicionado por el ancho de banda del bus de memoria y las restricciones físicas del almacenamiento secundario [4, 17]. Todo sistema paralelo posee componentes intrínsecamente secuenciales (como el análisis léxico inicial de lectura de ficheros o la escritura final de resultados) y componentes altamente paralelizables (como el ordenamiento local de bloques). La cuantificación matemática de cómo estas fracciones secuenciales acotan la aceleración máxima (*speedup*) del sistema se desarrolla mediante las ecuaciones de la Ley de Amdahl en la Sección 3.4 [4, 19, 20].

### 2.5 Auditoría Dinámica de Memoria y Sanitizadores

En el desarrollo de sistemas concurrentes en C++ que manipulan punteros crudos y segmentos de memoria mapeada, los errores de acceso fuera de límites o condiciones de carrera silenciosas pueden comprometer la integridad de los datos [21]. Para mitigar estos riesgos, se emplean herramientas de instrumentación en tiempo de compilación y ejecución como *AddressSanitizer* (`ASan`), el cual utiliza un mapeo de memoria sombra (*shadow memory*) para detectar desbordamientos de búfer y fugas de memoria [21, 22, 23], junto con *UndefinedBehaviorSanitizer* (`UBSan`), encargado de capturar desbordamientos enteros, alineaciones inválidas y operaciones no definidas por el estándar del lenguaje [24].

---

## 3. Metodología y Decisiones de Diseño

### 3.1 Transición de Bucket Sort a Sample Sort

En las sesiones iniciales de planeación colaborativa del equipo, se evaluó preliminarmente el uso del algoritmo *Bucket Sort* tradicional con rangos fijos de cubetas para repartir los datos. Sin embargo, identificamos un posible riesgo antes de recibir la matriz característica (`matriz_a.txt`): ante conjuntos de datos sesgados o distribuciones no uniformes, las cubetas habrían sufrido un desbalance severo de carga, dejando a algunos hilos con millones de elementos y a otros completamente ociosos, inhabilitando el paralelismo efectivo [10].

Para evitar esta vulnerabilidad teórica, tomamos la decisión de cambiar hacia el paradigma de **Sample Sort** [10, 11]. Este enfoque aplica un muestreo aleatorio sin reemplazo, conocido como *reservoir sampling*, sobre un subconjunto de tamaño fijo de los datos [12]. Posteriormente, ordena dicha muestra mediante nuestro Merge Sort interno [9] y calcula dinámicamente $k$ puntos de corte o *splitters* equidistantes. Esto garantiza matemáticamente un balanceo de carga uniforme entre los hilos trabajadores, independientemente de la distribución subyacente de los valores numéricos [10, 11].

### 3.2 Arquitectura Híbrida: Multiproceso, Memoria Compartida (POSIX IPC) y Multihilo

Para cumplir estrictamente con los objetivos formativos de la asignatura (cómputo concurrente multinúcleo, multiproceso y multihilo) y procesar este volumen masivo de datos sin duplicar la memoria en el espacio dinámico (*heap*) local de cada proceso, se diseñó una arquitectura híbrida jerárquica en tres niveles basada en llamadas nativas del sistema operativo Unix [5, 6]:

1. **Nivel Superior (Multiproceso con `fork` y `waitpid`):** Un proceso maestro orquesta el flujo global. Durante la Fase 1, tras cargar cada bloque o *chunk* en el segmento compartido, engendra un proceso hijo independiente mediante `fork()`. El proceso hijo asume la responsabilidad exclusiva de ordenar dicho *run* y volcarlo a disco, finalizando con `_Exit(0)`. El proceso maestro espera y audita la sincronización del trabajador mediante `waitpid()`, garantizando aislamiento de fallos y gestión estricta del ciclo de vida de los procesos [5].
2. **Nivel Intermedio (Memoria Compartida POSIX IPC cero copias):** Se utiliza `shm_open`, `ftruncate` y `mmap` con la bandera `MAP_SHARED` para reservar los segmentos globales `/proyecto1_hpc_data` y `/proyecto1_hpc_temp` en el directorio temporal del kernel (`/dev/shm`) [5]. Tanto el proceso maestro como los procesos hijos proyectan exactamente las mismas páginas físicas en sus respectivos espacios virtuales de direcciones, eliminando por completo la sobrecarga de copia por tuberías (*pipes*) o canales de paso de mensajes [5, 7, 8]. Asimismo, se incorporaron primitivas `pthread_mutex_t` con el atributo `PTHREAD_PROCESS_SHARED` para la sincronización y control interprocesos sin contención en la ruta crítica [5, 6].
3. **Nivel Inferior (Multihilo Multinúcleo con `pthreads`):** Al interior de cada proceso hijo, el ordenamiento del *run* se paraleliza masivamente dividiendo la carga entre $P$ hilos POSIX (`pthread_create` y `pthread_join`), los cuales saturan los núcleos físicos y lógicos del procesador ejecutando simultáneamente las fases de conteo, particionamiento (*scatter*) y ordenamiento local de cubetas en memoria compartida [4].

### 3.3 Restricciones Físicas y Activación del Paradigma *Out-of-Core*

La lectura del archivo de entrada es inherentemente secuencial (un solo hilo de ejecución). La clase `ChunkedTextReader` mapea el archivo mediante `mmap` con la directiva `MADV_SEQUENTIAL` para activar la lectura anticipada del kernel y libera las páginas procesadas mediante `MADV_DONTNEED` [5, 7]. No obstante, el análisis léxico carácter a carácter no se paraleliza debido a que el ancho de banda de entrada/salida (I/O) del almacenamiento constituye el cuello de botella real del sistema, y no la velocidad de cómputo del procesador [17]. El paralelismo se aplica exclusivamente a las fases de ordenamiento (dispersión/recolección y ordenamiento local de cubetas con `pthreads`) [4].

#### Demostración Formal de Complejidad Espacial

Sea $N = 1.6 \times 10^9$ el total de elementos en `matriz_a.txt` y $S_{\text{float}} = 4\text{ bytes}$ el tamaño en memoria de cada número de punto flotante de precisión simple (IEEE 754 de 32 bits). El espacio neto requerido para alojar un solo arreglo lineal de datos es:

$$M_{\text{base}} = N \times S_{\text{float}} = (1.6 \times 10^9) \times 4\text{ bytes} = 6.4 \times 10^9\text{ bytes} \approx 5.96\text{ GiB}$$

Un esquema puramente en memoria (*in-memory*) de Sample Sort requiere mantener simultáneamente en el sistema el arreglo original de entrada (`data`), el arreglo temporal de dispersión (`temp`) y los búferes auxiliares para el Merge Sort local de cada hilo ($\mathcal{O}(N/P)$) [9, 10]:

$$M_{\text{in-memory}} = 2(N \times S_{\text{float}}) + \mathcal{O}\left(\frac{N}{P}\right) \approx 11.92\text{ GB} + 0.8\text{ GB} \approx 12.72\text{ GB}$$

Si a esto se suma la memoria requerida por los búferes de lectura del texto, la demanda pico en memoria RAM supera los $17.88\text{ GB}$. Dado que este requerimiento excede la memoria RAM física disponible (~$9.75\text{ GB}$ libres) y la cuota asignada a `/dev/shm` ($7.39\text{ GB}$) en nuestro entorno de ejecución (cuyas especificaciones detalladas se describen en la Sección 4.1), y descartando el uso de memoria virtual (*swap*) en disco por provocar degradación catastrófica debido al *page fault thrashing* [25, 26], se implementó un sistema de **activación automática Out-of-Core (External Merge Sort)** en dos fases [13, 14]:

1. **Fase 1 (Generación de *Runs* Paralelos con Multiproceso y Multihilo):** Se define un ***run*** como un bloque contiguo de datos de tamaño $M$ ($M \ll N$) que cabe íntegramente en la memoria compartida, es ordenado de forma independiente en paralelo por un proceso hijo y volcado a disco en formato binario compacto [13, 14]. El archivo `matriz_a.txt` se lee en *streaming* por bloques de tamaño $M = 1024\text{ MB}$ ($268,435,456$ valores `float`), se ordenan en paralelo mediante Sample Sort multihilo (12 hilos) y se escriben temporalmente en el directorio `runs/run_XXXX.bin` (archivos que son eliminados automáticamente al finalizar la ejecución). Se generan $K = \lceil 1.6 \times 10^9 / 268,435,456 \rceil = 6$ *runs*. La complejidad espacial en esta fase queda acotada a $\mathcal{O}(M) \approx 1.0\text{ GiB}$ ($2.0\text{ GiB}$ considerando segmento de datos y temporal) [17].
2. **Fase 2 (K-Way Merge en Streaming):** Los $K = 6$ *runs* ordenados en disco se unifican concurrentemente utilizando una estructura de datos de cola de prioridad mínima (*Min-Heap* o *Tournament Tree*) [13, 16]. Al mantener únicamente un búfer de lectura por cada *run* ($B = 0.50\text{ MB}$), el consumo de memoria RAM en esta etapa se mantiene estrictamente acotado a $\mathcal{O}(K \cdot B) = 3\text{ MB} < 10\text{ MB}$, emitiendo el flujo globalmente ordenado hacia el archivo destino [14, 15].

### 3.4 Modelado Analítico y Ley de Amdahl

Para justificar formalmente las métricas de rendimiento y los límites de escalabilidad del sistema, el tiempo total de ejecución se modeló mediante la **Ley de Amdahl** [4, 19, 20].

#### Derivación de las Fracciones Secuencial ($s$) y Paralela ($p$)

A partir de las mediciones empíricas de tiempo de pared (*wall-clock time*) sobre la ejecución completa de `matriz_a.txt` con $K = 6$ *runs* y $12$ hilos lógicos en procesador AMD Ryzen 5 6600H, se aislaron los tiempos de cada etapa del *pipeline*:

* **Componentes secuenciales:** La Fase 1 consumió un tiempo total de $203.75\text{ s}$. El tiempo acumulado de ordenamiento paralelo de los 6 *runs* fue de $T_{\text{sort}}(12) = 13.48 + 12.93 + 12.59 + 12.69 + 12.81 + 12.23 = 76.73\text{ s}$ (promedio de $12.79\text{ s}$ por *run*). Por consiguiente, el tiempo dedicado a la lectura secuencial de disco y análisis léxico del texto ASCII fue $T_{\text{lectura}} = 203.75 - 76.73 = 127.02\text{ s}$. Sumando la fase de mezcla final del *K-Way Merge* ($T_{\text{merge}} \approx 24.68\text{ s}$), el tiempo secuencial total es:
  $$T_{\text{sec}} = T_{\text{lectura}} + T_{\text{merge}} = 127.02 + 24.68 = 151.70\text{ s}$$
* **Componente paralelizable:** El ordenamiento multihilo de los $6$ *runs* en memoria consumió $T_{\text{sort}}(12) \approx 76.73\text{ s}$. Considerando el factor de aceleración interno medido para el ordenamiento local de cubetas ($\approx 5.8\times$ respecto a la ejecución monohilo), el tiempo equivalente de ordenamiento secuencial puro se estima en $T_{\text{sort}}(1) \approx 76.73 \times 5.8 = 445.03\text{ s}$.

De este modo, el tiempo base de referencia monohilo es $T_{\text{total}}(1) = 151.70 + 445.03 = 596.73\text{ s}$, permitiendo obtener las proporciones exactas del sistema [4, 19]:

$$p = \frac{T_{\text{sort}}(1)}{T_{\text{total}}(1)} = \frac{445.03}{596.73} \approx 0.7458 \quad (74.58\%)$$

$$s = 1 - p = 1 - 0.7458 = 0.2542 \quad (25.42\%)$$

#### Cálculo de Aceleración y Cota Asintótica Máxima

La expresión general de la Ley de Amdahl establece que la aceleración (*speedup*) $S(N)$ al emplear $N$ unidades de procesamiento está dada por [4, 20]:

$$S(N) = \frac{1}{(1 - p) + \frac{p}{N}}$$

Evaluando el modelo para los $N = 12$ hilos lógicos de nuestro equipo de referencia:

$$S(12) = \frac{1}{(1 - 0.7458) + \frac{0.7458}{12}} = \frac{1}{0.2542 + 0.06215} = \frac{1}{0.31635} \approx 3.16\times$$

Para determinar el límite teórico máximo de aceleración ($S_{\max}$) que podría alcanzar esta arquitectura si dispusiera de infinitos núcleos de procesamiento ($N \to \infty$), se evalúa el límite asintótico de la ecuación [19, 20]:

$$S_{\max} = \lim_{N \to \infty} \frac{1}{(1 - p) + \frac{p}{N}} = \frac{1}{1 - p} = \frac{1}{0.2542} \approx 3.93\times$$

Este resultado demuestra analíticamente que la aceleración global del sistema está acotada asintóticamente a $3.93\times$, debido al peso ineludible del $25.42\%$ del tiempo gobernado por las operaciones de entrada/salida secuenciales [4, 20].

### 3.5 Determinación Analítica y Empírica de Parámetros ($M^*$ y $P^*$)

Para fundamentar la configuración operativa del programa —en estrecha relación con la arquitectura de hardware y el sistema operativo descritos en la Sección 4.1—, los parámetros críticos se determinaron combinando análisis de complejidad y validación heurística con diversos tamaños de bloque ($64\text{ MB}$, $256\text{ MB}$, $512\text{ MB}$, $1024\text{ MB}$ y $2048\text{ MB}$):

* **Tamaño óptimo de chunks ($M^* \in [512\text{ MB}, 1024\text{ MB}]$):** El costo computacional de la fase de mezcla en un *K-Way Merge* es $\mathcal{O}(N \log_2 K)$, donde $K = \lceil N/M \rceil$ es el número de *runs* generados [13, 16]. Las pruebas empíricas demostraron que bloques muy pequeños (como $64\text{ MB}$) generan decenas de *runs*, incrementando la profundidad del *Min-Heap* y fragmentando las lecturas secuenciales en disco [14, 17]. Por el contrario, establecer $M = 1024\text{ MB}$ para elementos de tipo `float` reduce el problema a solo $K = 6$ *runs* (profundidad de árbol $\lceil \log_2 6 \rceil = 3$), maximizando el aprovechamiento de la lectura anticipada (*read-ahead*) del kernel de Linux sobre el almacenamiento NVMe sin comprometer el límite físico de $7.39\text{ GiB}$ en `/dev/shm` [5, 18].
* **Número de hilos óptimo ($P^* = 12$):** A partir de los barridos experimentales de escalabilidad (con $1, 2, 4, 8$ y $12$ hilos) y la topología del procesador utilizado en la Sección 4.1, se fijó $P^* = 12$ para aprovechar la totalidad de los hilos lógicos (SMT) [4]. Esto permite solapar las latencias de fallos de caché durante el *scatter/gather* y saturar eficientemente el ancho de banda de la memoria principal sin provocar penalizaciones por cambios de contexto (*context switching*) excesivos en el planificador del sistema operativo [4, 6].

---

## 4. Desarrollo e Implementación

### 4.1 Entorno de Ejecución y Especificaciones de Hardware/Software

Todas las pruebas de rendimiento, análisis de consumo de memoria y validaciones de escalabilidad se ejecutaron bajo las siguientes especificaciones técnicas:

* **Procesador (CPU):** AMD Ryzen 5 6600H with Radeon Graphics (6 núcleos físicos, 12 hilos lógicos SMT).
* **Memoria Principal (RAM):** 14.85 GiB instalados (~10.6–10.7 GiB disponibles para espacio de usuario).
* **Memoria Compartida POSIX (`/dev/shm`):** 7.5 GiB montados sobre sistema de archivos temporal en memoria (`tmpfs`).
* **Almacenamiento Secundario:** Unidad de estado sólido NVMe (`/dev/nvme0n1p3`).
* **Sistema Operativo y Entorno:** Fedora Linux 45 (KDE Plasma Desktop Edition) x86_64
* **Compilador y Banderas:** `g++ (GCC) 15.2.0`, estándar C++17, compilado con optimización `-O3 -Wall -Wextra -std=c++17` y enlazado contra las bibliotecas del sistema `-lrt -lpthread`.

### 4.2 Arquitectura Modular del Sistema

Para mantener una estructura ordenada entre los componentes, distinguir responsabilidades de manera clara y facilitar la depuración aislada de fallos (*debugging*), el código fuente se organizó en módulos desacoplados con interfaces bien definidas:

* **`chunked_reader.hpp / .cpp`:** Implementa la clase `ChunkedTextReader`, cuya función principal `next()` lee y procesa bloques acotados del archivo de entrada directamente desde el espacio de memoria mapeado (`mmap` con la directiva `MADV_SEQUENTIAL` para activar la lectura anticipada del sistema operativo). Tras avanzar el cursor sobre tokens numéricos completos —evitando cortes accidentales de cifras entre fronteras de página—, invoca `madvise` con `MADV_DONTNEED` sobre las páginas ya consumidas. Esto libera la memoria física de forma inmediata y garantiza un consumo de memoria residente (RSS) acotado a ~152 MB independientemente del tamaño del fichero.


* **`csv_parser.hpp / .cpp`:** Contiene el analizador léxico manual carácter a carácter articulado en las rutinas `parse_csv_buffer()` y `parse_csv_into()`, así como su envoltorio de alto nivel `load_csv_into()`. Estas funciones recorren el flujo de bytes identificando delimitadores (espacios, comas, tabuladores y saltos de línea) y transformando las cadenas ASCII a punto flotante de precisión simple (`float`) directamente sobre los arreglos destino en memoria compartida, prescindiendo por completo de bibliotecas externas o copias intermedias en el *heap*.


* **`shared_memory.hpp / .cpp`:** Gestiona el ciclo de vida completo de los segmentos de memoria compartida POSIX (`/proyecto1_hpc_data`, `/proyecto1_hpc_temp` y `/proyecto1_hpc_ctrl`) mediante las funciones `shared_create()` (que encadena `shm_unlink`, `shm_open`, `ftruncate` y `mmap`) y `shared_release()` (que ejecuta `munmap`, `close` y el desligado final). Adicionalmente, encapsula la inicialización (`shared_control_init()`) y actualización atómica (`shared_control_mark_done()`) de la estructura `SharedControl`, protegiendo los contadores de avance de los trabajadores mediante un mutex configurado con el atributo `PTHREAD_PROCESS_SHARED`.


* **`sample_sort.hpp / .cpp`:** Implementa la lógica estadística y de particionamiento del algoritmo Sample Sort. Incluye el generador pseudoaleatorio determinista `XorShift64` (sin depender de `<random>`), la rutina `random_sample()` que ejecuta un muestreo sin reemplazo (*reservoir sampling*) en una sola pasada $\mathcal{O}(n)$, `choose_splitters()` para extraer los puntos de corte equidistantes tras ordenar la muestra, y `find_bucket()`, una búsqueda binaria manual equivalente a `upper_bound` que asigna cada valor a su cubeta correspondiente asegurando que los elementos duplicados se dirijan consistentemente hacia la cubeta izquierda.


* **`scatter_gather.hpp / .cpp`:** Coordina la distribución global de los datos a través de la función principal `sample_sort_parallel()` y registra las métricas de telemetría por etapa en la estructura `ScatterGatherStats`. Su flujo interno ejecuta el plan de muestreo (`build_sample_plan()`), realiza el conteo de frecuencia por cubeta, calcula los desplazamientos iniciales (*offsets*) mediante suma de prefijos, dispersa los elementos (*scatter*) hacia el segmento temporal compartido y delega el ordenamiento concurrente, logrando una recolección (*gather*) implícita al quedar las cubetas contiguas y ordenadas.


* **`merge_sort.hpp / .cpp` y `parallel_sort.hpp / .cpp`:** Proveen el motor de ordenamiento interno y la ejecución concurrente. El primer módulo implementa `merge_sort()` y `merge_sort_range()` (un Merge Sort *top-down* estable con gestión de búfer temporal acotado al rango). Por su parte, `parallel_sort_buckets()` administra el ciclo de vida de los hilos POSIX (`pthread_create` y `pthread_join`), asignando las cubetas de tamaño variable de forma intercalada (*round-robin*) entre los trabajadores para equilibrar la carga sin sincronización fina.


* **`sort_worker.hpp / .cpp`:** Encapsula la rutina de ejecución concurrente de los trabajadores a través de la función `sort_worker()` (con firma compatible con `pthread_create`) y la función auxiliar de particionamiento `worker_range()`. Permite subdividir un intervalo unidimensional $[0, n)$ en rangos contiguos equilibrados entre múltiples hilos de procesamiento, desacoplando la lógica de cómputo del ordenamiento de la gestión de primitivas del sistema operativo.


* **`file_io.hpp / .cpp`:** Administra la serialización de salidas en formatos `.txt`, `.csv` y binario crudo `.bin` mediante `save_matrix()`. Para los ficheros binarios, escribe una cabecera estructurada (`MATBIN1` con metadatos de versión, tipo de dato, filas, columnas y conteo total) seguida de los valores `float` (4 bytes IEEE 754) en formato *little-endian*. Asimismo, incorpora `verify_bin()`, una rutina que recorre archivos binarios masivos por bloques para auditar su integridad estructural y ordenamiento sin saturar la memoria.


* **`external_sort.hpp / .cpp`:** Implementa el motor de ordenamiento externo *Out-of-Core* en dos fases mediante la función `run_external_sort()`, diseñado específicamente para procesar volúmenes masivos de datos que exceden la memoria física disponible del sistema. En la Fase 1, articula la jerarquía multiproceso mediante llamadas nativas `fork()` y `waitpid()`, asignando a cada proceso hijo independiente la tarea de ordenar su respectivo bloque de datos en memoria compartida (`MAP_SHARED`) utilizando Sample Sort multihilo, serializándolo posteriormente como un archivo de *run* binario temporal (`runs/run_XXXXX.bin`). En la Fase 2, coordina la mezcla global de los $K$ *runs* en *streaming* mediante la estructura `RunReader` (con búferes de lectura anticipada de $4\text{ MB}$ por *run*) y una cola de prioridad mínima (*Min-Heap*), auditando en caliente la monotonía ascendente y el *checksum* acumulado de bits de cada elemento emitido, y liberando automáticamente todos los ficheros temporales al concluir.


* **`main.cpp`:** Actúa como el orquestador central del sistema. Procesa los argumentos de la línea de comandos, inspecciona en tiempo de ejecución la memoria RAM libre en `/proc/meminfo` y la capacidad de `/dev/shm` mediante `statvfs` para activar automáticamente el modo *Out-of-Core* cuando la matriz excede los recursos físicos, y ejecuta tanto la generación paralela de *runs* como el *K-Way Merge* final en *streaming*.



### 4.3 Mecanismos de Verificación e Integridad de Datos

Al procesar y reordenar $1,600,000,000$ de números de punto flotante de precisión simple (`float` de 32 bits) sin el respaldo de bibliotecas externas, existe el riesgo de enfrentar corrupciones silenciosas, desbordamientos de índice o pérdidas de precisión durante las transferencias entre memoria y disco. Para blindar la exactitud matemática del resultado, se implementaron tres capas rigurosas de validación automática en tiempo de ejecución. En primer lugar, se aplica una verificación de monotonía estricta tanto al concluir cada ordenamiento local en memoria (mediante la función `is_sorted_ascending()`) como durante la emisión continua en *streaming* del *K-Way Merge*; en cada paso se evalúa la invariante $x_i \le x_{i+1}$, garantizando que el flujo final sea globalmente ascendente sin excepción.

Dado que la suma aritmética tradicional de números de punto flotante es no asociativa y sufre errores de redondeo según el orden en que se sumen los términos, validar la conservación de los elementos mediante sumas convencionales habría producido falsos negativos. Para resolverlo, se diseñó un verificador basado en un *checksum* de bits invariante al orden: el programa reinterpreta la representación binaria cruda de 32 bits de cada `float` como un entero sin signo (`uint32_t`) y acumula su suma entera (en un acumulador entero de 64 bits `uint64_t`) tanto durante el análisis léxico inicial como al emitir el resultado ordenado. Al tratarse de aritmética entera modular, el resultado es matemáticamente idéntico sin importar la permutación de los datos, pero cambia drásticamente si un solo bit se altera, se duplica o se pierde; en todas las corridas sobre `matriz_a.txt`, el *checksum* de entrada y salida coincidió exactamente en `3447059619426937470`.

Finalmente, para asegurar la estabilidad operativa del sistema a nivel de gestión de memoria y recursos del sistema operativo, el código completo fue auditado dinámicamente utilizando las herramientas *AddressSanitizer* (`ASan`) y *UndefinedBehaviorSanitizer* (`UBSan`) bajo escenarios límite —tales como cubetas vacías, arreglos con alta densidad de valores duplicados y matrices de dimensiones mínimas—, descartando fugas de memoria (*memory leaks*), accesos fuera de límites (*buffer overflows*) o comportamiento indefinido. De igual manera, se verificó que todas las rutas de ejecución (incluidas las salidas por error) invoquen la liberación explícita de los descriptores de memoria compartida en `/dev/shm` y eliminen los archivos temporales del directorio `runs/`, dejando el entorno del sistema completamente limpio al finalizar.



### 4.4 Ejecuciones del Programa y Evidencias (Capturas de Pantalla)

A continuación se documentan las salidas reales de consola obtenidas durante las pruebas completas sobre `matriz_a.txt` ($40,000 \times 40,000$, precisión `float`) utilizando la arquitectura multiproceso (`fork`/`waitpid`) con $12$ hilos para tamaños de *run* de $1024\text{ MB}$ y $2048\text{ MB}$:

**Ejecución con bloques de 1024 MB (6 runs, 12 hilos, salida a texto):**
```text
$ ./ordenar_matriz 1024 --hilos 12 salida.txt

TEST: La matriz (1600000000 valores = 5.96 GB) no cabe en memoria
disponible (requiere 11.92 GB en shm / 17.88 GB en RAM;
disponible: 7.39 GB en shm / 8.66 GB en RAM).
-> Activando el Ordenamiento Externo Out-of-Core
   (runs en disco + K-Way Merge con Min-Heap y streaming)

--- FASE 1: Generacion y ordenamiento de runs en paralelo ---
Archivo de entrada : matriz_a.txt
Dimensiones        : 40000 x 40000 (1600000000 valores)
Tamano de run      : 1024.00 MB (268435456 valores)
Hilos por run      : 12 (Sample Sort con memoria compartida)
Proceso Maestro    : PID 5737

  [Run 0000] 268435456 valores ordenados en 13573.84 ms [Hijo PID 5766] -> runs/run_00000.bin
  [Run 0001] 268435456 valores ordenados en 13178.12 ms [Hijo PID 5792] -> runs/run_00001.bin
  [Run 0002] 268435456 valores ordenados en 13247.52 ms [Hijo PID 5822] -> runs/run_00002.bin
  [Run 0003] 268435456 valores ordenados en 13482.40 ms [Hijo PID 5851] -> runs/run_00003.bin
  [Run 0004] 268435456 valores ordenados en 13675.10 ms [Hijo PID 5877] -> runs/run_00004.bin
  [Run 0005] 257822720 valores ordenados en 13097.45 ms [Hijo PID 5902] -> runs/run_00005.bin

Fase 1 completada : 6 runs generados en 205644.77 ms
Estadistica runs  : Promedio 13375.74 ms +/- 213.37 ms (min 13097.45 ms, max 13675.10 ms)

--- FASE 2: K-Way Merge en streaming con Min-Heap ---
Runs a mezclar     : 6
Buffer por run     : 4.00 MB
Destino de salida  : salida.txt

  Progreso Merge: 160000000/1600000000 valores (10.0%)
  Progreso Merge: 320000000/1600000000 valores (20.0%)
  Progreso Merge: 480000000/1600000000 valores (30.0%)
  Progreso Merge: 640000000/1600000000 valores (40.0%)
  Progreso Merge: 800000000/1600000000 valores (50.0%)
  Progreso Merge: 960000000/1600000000 valores (60.0%)
  Progreso Merge: 1120000000/1600000000 valores (70.0%)
  Progreso Merge: 1280000000/1600000000 valores (80.0%)
  Progreso Merge: 1440000000/1600000000 valores (90.0%)
  Progreso Merge: 1600000000/1600000000 valores (100.0%)

--- Resumen Final del Ordenamiento Externo ---
Valores ordenados  : 1600000000 / 1600000000
Min / Max          : -10 / 10
Verificacion orden : ordenado en orden ascendente
Checksum de bits   : 3447059619426937470 vs 3447059619426937470 -> OK (mismos elementos)
Tiempo Fase 1      : 205644.766 ms (I/O entrada + runs paralelos)
Tiempo Fase 2      : 174460.156 ms (K-Way Merge streaming)
Tiempo Total Pared : 380105.156 ms
Archivo guardado   : salida.txt
```

**Ejecución con bloques de 2048 MB (3 runs, 12 hilos, salida a texto):**
```text
$ ./ordenar_matriz 2048 --hilos 12 salida.txt

TEST: La matriz (1600000000 valores = 5.96 GB) no cabe en memoria
disponible (requiere 11.92 GB en shm / 17.88 GB en RAM;
disponible: 7.39 GB en shm / 8.47 GB en RAM).
-> Activando el Ordenamiento Externo Out-of-Core
   (runs en disco + K-Way Merge con Min-Heap y streaming)

--- FASE 1: Generacion y ordenamiento de runs en paralelo ---
Archivo de entrada : matriz_a.txt
Dimensiones        : 40000 x 40000 (1600000000 valores)
Tamano de run      : 2048.00 MB (536870912 valores)
Hilos por run      : 12 (Sample Sort con memoria compartida)
Proceso Maestro    : PID 5029

  [Run 0000] 536870912 valores ordenados en 28626.80 ms [Hijo PID 5050] -> runs/run_00000.bin
  [Run 0001] 536870912 valores ordenados en 27684.70 ms [Hijo PID 5076] -> runs/run_00001.bin
  [Run 0002] 526258176 valores ordenados en 27252.21 ms [Hijo PID 5101] -> runs/run_00002.bin

Fase 1 completada : 3 runs generados en 212031.56 ms
Estadistica runs  : Promedio 27854.57 ms +/- 573.88 ms (min 27252.21 ms, max 28626.80 ms)

--- FASE 2: K-Way Merge en streaming con Min-Heap ---
Runs a mezclar     : 3
Buffer por run     : 4.00 MB
Destino de salida  : salida.txt

  Progreso Merge: 160000000/1600000000 valores (10.0%)
  Progreso Merge: 320000000/1600000000 valores (20.0%)
  Progreso Merge: 480000000/1600000000 valores (30.0%)
  Progreso Merge: 640000000/1600000000 valores (40.0%)
  Progreso Merge: 800000000/1600000000 valores (50.0%)
  Progreso Merge: 960000000/1600000000 valores (60.0%)
  Progreso Merge: 1120000000/1600000000 valores (70.0%)
  Progreso Merge: 1280000000/1600000000 valores (80.0%)
  Progreso Merge: 1440000000/1600000000 valores (90.0%)
  Progreso Merge: 1600000000/1600000000 valores (100.0%)

--- Resumen Final del Ordenamiento Externo ---
Valores ordenados  : 1600000000 / 1600000000
Min / Max          : -10 / 10
Verificacion orden : ordenado en orden ascendente
Checksum de bits   : 3447059619426937470 vs 3447059619426937470 -> OK (mismos elementos)
Tiempo Fase 1      : 212031.562 ms (I/O entrada + runs paralelos)
Tiempo Fase 2      : 165750.391 ms (K-Way Merge streaming)
Tiempo Total Pared : 377782.250 ms
Archivo guardado   : salida.txt
```

**Evidencia Visual**

![Inicio de ejecución con chunks de 2048 MB y 12 hilos](Imagenes_reporte/2048_chunks_12_hilos_start.png)\
*Imagen 1. El inicio del proceso al ejecutarse con 2048 MB de tamaño de chunk y a 12 hilos, la lectura es secuencial pero el ordenamiento de cada chunk es paralelo.*

![Fin de la fase 1 e inicio de la fase 2](Imagenes_reporte/2048_chunks_12_hilos_fase2.png)\
*Imagen 2. Fin de la fase 1 e inicio de la fase 2.*

![Fin de la fase 2](Imagenes_reporte/2048_chunks_12_hilos_fin.png)\
*Imagen 3. Fin de la fase 2, como se puede observar, el tiempo de ejecución de esta fase es la más rápida.*

![Ejecución con chunks de 1024 MB](Imagenes_reporte/1024_chunks_12_hilos_start.png)\
*Imagen 4. Mismo proceso en la fase 1, ejecutándose en chunks de 1024 MB.*

![Captura de la consola y validación](Imagenes_reporte/captura_consola.png)\
*Imagen 5. Captura de la terminal al ejecutar el ordenamiento y con el comando de validación del ordenamiento.*

---

## 5. Conclusiones y Análisis de Resultados

### 5.1 Hallazgos Principales y Desempeño Empírico

La ejecución experimental sobre la matriz `matriz_a.txt` ($40,000 \times 40,000 = 1.6 \times 10^9$ valores de tipo `float`) permitió validar la eficiencia del diseño híbrido *Out-of-Core* frente a distintas configuraciones de tamaño de bloque ($M$) con $12$ hilos y multiproceso real:

Tabla 1. Comparativa de resultados empíricos entre bloques de 1024 MB y 2048 MB con 12 hilos y salida a texto formateado (`salida.txt`).

| Configuración ($M$ / Hilos) | *Runs* Generados ($K$) | Tiempo Fase 1 (Lectura + Sort) | Tiempo Fase 2 (*K-Way Merge*) | Tiempo Total de Pared | Throughput *K-Way Merge* |
| --- | --- | --- | --- | --- | --- |
| **1024 MB / 12 hilos** | 6 | 205.64 s | 174.46 s | **380.11 s (~6.33 min)** | 9.17 M val/s |
| **2048 MB / 12 hilos** | 3 | 212.03 s | 165.75 s | **377.78 s (~6.30 min)** | 9.65 M val/s |

De las mediciones experimentales obtenidas se desprenden tres hallazgos fundamentales que validan el diseño arquitectónico del sistema. En primer lugar, respecto a la estabilidad y el balanceo de carga en *Sample Sort*, en ambas configuraciones se observó una estabilidad sobresaliente en los procesos hijos: con bloques de $1024\text{ MB}$ el tiempo promedio por *run* fue de **$13,375.74\text{ ms} \pm 213.37\text{ ms}$** (desviación estándar de apenas **$1.6\%$**), mientras que con bloques de $2048\text{ MB}$ fue de **$27,854.57\text{ ms} \pm 573.88\text{ ms}$** (desviación de **$2.0\%$**). Esto confirma empíricamente que el muestreo *reservoir* y la selección de *splitters* logran un particionamiento casi perfecto entre las cubetas, evitando la inanición de hilos y logrando una utilización uniforme de los 12 hilos lógicos en cada proceso hijo.

En segundo lugar, se comprobó la ventaja de reducir el número de *runs* en el *K-Way Merge*: la configuración de $2048\text{ MB}$ redujo el número de *runs* a solo $K=3$ (profundidad de árbol $\lceil \log_2 3 \rceil = 2$), frente a $K=6$ (profundidad $\lceil \log_2 6 \rceil = 3$) en $1024\text{ MB}$. Esta menor profundidad del *Min-Heap* y el uso de búferes de lectura de $4\text{ MB}$ por *run* permitieron que la Fase 2 fuera más rápida en la corrida de $2048\text{ MB}$ ($165.75\text{ s}$ vs $174.46\text{ s}$, con un throughput superior de **$9.65\text{ millones de valores/s}$**), obteniendo el mejor tiempo global total de **$377.78\text{ s}$** (~6.30 minutos). Asimismo, se corroboró que cuando la salida se realiza en binario puro (`salida.bin`), esta fase se reduce a apenas **$24.68\text{ s}$** ($64.82\text{ M val/s}$), demostrando que la mayor parte del tiempo de la Fase 2 al generar ficheros `.txt` corresponde al formateo léxico de $16.8\text{ GB}$ de números ASCII con 6 decimales.

Finalmente, los resultados empíricos corroboran la cota teórica establecida por la Ley de Amdahl: independientemente del tamaño de bloque, el análisis léxico secuencial del texto de entrada domina más del $60\%$ de la Fase 1 (~125 s), confirmando que el rendimiento global del sistema se encuentra condicionado por las limitaciones físicas de la entrada/salida de almacenamiento.




### 5.2 Consumo de Memoria y Eficiencia Espacial

Uno de los logros más significativos del proyecto es la drástica reducción de memoria: mientras un algoritmo puramente en memoria habría exigido al menos **$11.92\text{ GB}$** en memoria compartida (y hasta **$17.88\text{ GB}$** en memoria RAM total con búferes de texto) para alojar simultáneamente los arreglos `data` y `temp` (provocando un fallo inmediato por memoria insuficiente), durante la Fase 1 de ordenamiento externo con bloques de $1024\text{ MB}$, el consumo máximo de memoria compartida se mantuvo acotado a **$2.05\text{ GiB}$** ($1024\text{ MB}$ para `data` y $1024\text{ MB}$ para `temp`), y durante la Fase 2 (*K-Way Merge*) descendió a menos de **$30\text{ MB}$** (con búferes optimizados de $4.00\text{ MB}$ por cada *run*), operando completamente libre de paginación hacia *swap*. Asimismo, el mecanismo de paginación bajo demanda (`mmap` + `MADV_DONTNEED`) mantuvo el consumo de lectura residente acotado a **~152 MB**. 



### 5.3 Cumplimiento de los Objetivos de la Asignatura

Se verificó el cumplimiento íntegro de las especificaciones establecidas por la cátedra y las restricciones impuestas por el equipo:

Se implementaron desde cero tanto el analizador del archivo de texto en *streaming* como los motores de ordenamiento (`merge_sort`, `sample_sort` y el *Min-Heap* del *K-Way Merge*). Se articuló una arquitectura multinúcleo, multiproceso y multihilo combinando la creación de procesos independientes con `fork()` y `waitpid()`, el uso de memoria compartida POSIX (`shm_open`, `ftruncate`, `mmap`), primitivas de sincronización `PTHREAD_PROCESS_SHARED` e hilos `pthreads` para saturar al $100\%$ los núcleos durante el cómputo intensivo, logrando un balance óptimo entre aislamiento de procesos y paralelismo masivo.
Los $1,600,000,000$ valores de `matriz_a.txt` fueron ordenados de forma ascendente (rango $[-10, 10]$), validados mediante verificación binaria independiente y confirmación exacta del *checksum* de bits (`3447059619426937470`), además de superar auditorías de memoria con `ASan` y `UBSan`.

---

## 6. Bibliografía

[1] "CSV file management using C++," GeeksforGeeks, Jul. 11, 2025. [Online]. Available: [https://www.geeksforgeeks.org/cpp/csv-file-management-using-c/](https://www.geeksforgeeks.org/cpp/csv-file-management-using-c/). [Accesado el: Oct. 26, 2026].

[2] "std::strtod," cppreference.com. [Online]. Available: [https://en.cppreference.com/w/cpp/string/byte/strtod](https://en.cppreference.com/w/cpp/string/byte/strtod). [Accesado el: Oct. 26, 2026].

[3] "std::fread," cppreference.com. [Online]. Available: [https://en.cppreference.com/w/cpp/io/c/fread](https://en.cppreference.com/w/cpp/io/c/fread). [Accesado el: Oct. 26, 2026].

[4] P. S. Pacheco, *An Introduction to Parallel Programming*. Burlington, MA, USA: Morgan Kaufmann, 2011. [Online]. Available: [https://drive.uqu.edu.sa/_/gmaahmed/files/D&%20Parallel%20Textbook/An%20Introduction%20to%20Parallel%20Programming2011.pdf](https://drive.uqu.edu.sa/_/gmaahmed/files/D&%20Parallel%20Textbook/An%20Introduction%20to%20Parallel%20Programming2011.pdf).

[5] W. R. Stevens and S. A. Rago, *Advanced Programming in the UNIX Environment*, 3rd ed. Upper Saddle River, NJ, USA: Addison-Wesley, 2013. [Online]. Available: [https://raw.githubusercontent.com/zwan074/technical-books/master/Advanced.Programming.in.the.UNIX.Environment.3rd.Edition.0321637739.pdf](https://raw.githubusercontent.com/zwan074/technical-books/master/Advanced.Programming.in.the.UNIX.Environment.3rd.Edition.0321637739.pdf).

[6] A. Corral, "Tema 3: Gestión de Memoria y Comunicación entre Procesos," Diseño de Sistemas Operativos, Universidad de Almería. [Online]. Available: [https://w3.ual.es/~acorral/DSO/Tema_3.pdf](https://w3.ual.es/~acorral/DSO/Tema_3.pdf).

[7] J. Lee, "How does memory mapping (mmap) work?," Medium. [Online]. Available: [https://medium.com/@jyjimmylee/how-does-memory-mapping-mmap-work-c8a6a550ba0d](https://medium.com/@jyjimmylee/how-does-memory-mapping-mmap-work-c8a6a550ba0d).

[8] IBM Documentation, "mmap() — Map pages of memory," z/OS 2.1.0. [Online]. Available: [https://www.ibm.com/docs/es/zos/2.1.0?topic=functions-mmap-map-pages-memory](https://www.ibm.com/docs/es/zos/2.1.0?topic=functions-mmap-map-pages-memory).

[9] T. H. Cormen, C. E. Leiserson, R. L. Rivest, and C. Stein, *Introduction to Algorithms*, 2nd ed. Cambridge, MA, USA: MIT Press, 2001, pp. 28–29.

[10] "Samplesort," Wikipedia, The Free Encyclopedia, Oct. 23, 2026. [Online]. Available: [https://en.wikipedia.org/wiki/Samplesort](https://en.wikipedia.org/wiki/Samplesort). [Accesado el: Oct. 26, 2026].

[11] D. Patel, "Parallel Sorting using Regular Sampling (PSRS)," Medium. [Online]. Available: [https://medium.com/@dhairyapatelad/parallel-sorting-using-regular-sampling-psrs-4e27eb63b307](https://medium.com/@dhairyapatelad/parallel-sorting-using-regular-sampling-psrs-4e27eb63b307).

[12] "Reservoir Sampling," GeeksforGeeks. [Online]. Available: [https://www.geeksforgeeks.org/dsa/reservoir-sampling/](https://www.geeksforgeeks.org/dsa/reservoir-sampling/).

[13] C. A. Shaffer, *Data Structures and Algorithm Analysis in C++*, 3rd ed. Mineola, NY, USA: Courier Corporation, 2012.

[14] S. Maheshwari, "Sorting Large Datasets with Limited Memory: The Chunked Merge Sort Approach," Medium. [Online]. Available: [https://satyadeepmaheshwari.medium.com/sorting-large-datasets-with-limited-memory-the-chunked-merge-sort-approach-318275275c81](https://satyadeepmaheshwari.medium.com/sorting-large-datasets-with-limited-memory-the-chunked-merge-sort-approach-318275275c81).

[15] "External memory graph traversal," Wikipedia, The Free Encyclopedia. [Online]. Available: [https://en.wikipedia.org/wiki/External_memory_graph_traversal](https://en.wikipedia.org/wiki/External_memory_graph_traversal).

[16] "K-way merge algorithm," Wikipedia, The Free Encyclopedia. [Online]. Available: [https://en.wikipedia.org/wiki/K-way_merge_algorithm](https://en.wikipedia.org/wiki/K-way_merge_algorithm).

[17] J. S. Vitter, "Algorithms and Data Structures for External Memory," *Foundations and Trends in Theoretical Computer Science*, vol. 2, no. 4, pp. 305–474, 2008. [Online]. Available: [https://perso.ens-lyon.fr/loris.marchal/docs-data-aware/ExternalMemorySurveyVitter.pdf](https://perso.ens-lyon.fr/loris.marchal/docs-data-aware/ExternalMemorySurveyVitter.pdf).

[18] L. Arge, "I/O-Efficient Algorithms and Data Structures," SciSpace. [Online]. Available: [https://scispace.com/pdf/i-o-efficient-algorithms-and-data-structures-1nk4tww96g.pdf](https://scispace.com/pdf/i-o-efficient-algorithms-and-data-structures-1nk4tww96g.pdf).

[19] HPC101 Documentation, "Session 3: Parallel Programming and Amdahl's Law," Read the Docs. [Online]. Available: [https://hpc101.readthedocs.io/en/latest/tutorial/session3/parallel.html](https://hpc101.readthedocs.io/en/latest/tutorial/session3/parallel.html).

[20] "Ley de Amdahl y Escalamiento en Sistemas Paralelos," Scribd. [Online]. Available: [https://www.scribd.com/doc/247004185/Ley-de-Amdahl](https://www.scribd.com/doc/247004185/Ley-de-Amdahl).

[21] J. Miller, "C++ Beyond the Syllabus #11: Address Sanitizers," Medium. [Online]. Available: [https://jaredmil.medium.com/c-beyond-the-syllabus-11-address-sanitizers-9cc98d1e5b15](https://jaredmil.medium.com/c-beyond-the-syllabus-11-address-sanitizers-9cc98d1e5b15).

[22] Microsoft Learn, "AddressSanitizer (ASan) para C++," Documentación de MSVC. [Online]. Available: [https://learn.microsoft.com/es-es/cpp/sanitizers/asan?view=msvc-170](https://learn.microsoft.com/es-es/cpp/sanitizers/asan?view=msvc-170).

[23] Ohio Supercomputer Center (OSC), "How to Use Address Sanitizer." [Online]. Available: [https://www.osc.edu/resources/getting_started/howto/howto_use_address_sanitizer](https://www.osc.edu/resources/getting_started/howto/howto_use_address_sanitizer).

[24] LLVM Project, "UndefinedBehaviorSanitizer — Clang Documentation." [Online]. Available: [https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html).

[25] "Techniques to handle Thrashing," GeeksforGeeks. [Online]. Available: [https://www.geeksforgeeks.org/operating-systems/techniques-to-handle-thrashing/](https://www.geeksforgeeks.org/operating-systems/techniques-to-handle-thrashing/).

[26] Lenovo México, "¿Qué es el thrashing?," Glosario de Tecnología. [Online]. Available: [https://www.lenovo.com/mx/es/glosario/thrashing/](https://www.lenovo.com/mx/es/glosario/thrashing/).

---

