# Proyecto 1 HPC

## Notas:
El proyecto es en equipos de 2. para el 20 de noviembre!!!
"No es dificil, requiere astucia...",
Se entrega: El código, cómo se compila, el archivo de datos debe estar en la misma carpeta.
En las primeras lineas (como comentarios) los nombres, instrucciones de compilacion e indicaciones adicionales, y reporte (como, que se uso, tiempos de ejecucion...)

A groso modo, Usando el archivo "GeneradorCadenas.c" generar archivo de 10,000x10,000, ordenar ya sea de menor a mayor, o de mayor a menor. usando multinucleo, multiproceso y multihilo. mostrar ademas el task manager para mostrar el % de uso del cpu. añadir el equipo, compilador, hardware software...
El reporte impreso, 1 reporte por pareja.

nota: Tal vez usar el metodo de bucket sort debido a su alta paralelización

Aproximacion del caso 1 pag. 5 de 4_Multi-nucleo-P2.pdf de lo que se hara...
Tambien se puede tomar como referencia la pag. 23 del mismo pdf...
En el ejemplo de pag 19 del pdf, posix_spawn con el multihilo podemos ajustar donde se inicia la matriz y darle multiples ejecuciones recursivas, estan los IPCs para guardar en mini archivos... tuberias, canales, memoria compartida, mensajes...


## Consideraciones

Para pasar a programación multinucleo / multiproceso, hay que tener en cuenta multiples consideraciones:
- Entender el problema y la solución (condiciones, requisitos no funcionales...)
- Tendremos un modulo que lea archivos, otro que haga operaciones... dividiendo el programa en las areas lógicas.
- Para el volumen de datos de una matriz de 10,000 x 10,000 (100 millones de elementos). No cargar todo en memoria RAM local (heap) en un solo proceso.
- Mapeo del archivo de datos en memoria Memoria compartida (mmap) desde el inicio para que todos los procesos e hilos accedan a él sin duplicar los gigabytes de información.
- A diferencia de un Bucket Sort tradicional con rangos fijos (donde unas cubetas pueden quedar con millones de elementos y otras casi vacías), Samplesort distribuye los datos equitativamente muestreando el conjunto original para definir los límites (splitters).

Arquitectura de procesos e hilos:

    El proceso principal (maestro) creará el espacio de memoria compartida (shm_open, ftruncate) y lanzará múltiples procesos hijos usando posix_spawn.

    Todos los procesos hijos realizarán el mapeo (mmap) para acceder al mismo bloque de memoria.

    Cada proceso hijo creará sus propios hilos (pthread_create). Los hilos dentro del mismo proceso compartirán la memoria del proceso de forma nativa.

    Se reservará un pequeño espacio en la memoria compartida para sincronización: mutexes (pthread_mutex_t con atributo PTHREAD_PROCESS_SHARED), semáforos y contadores atómicos para coordinar el llenado y la asignación de cubetas entre distintos procesos.

    Cada proceso/hilo notificará al terminar su bloque de trabajo.

    Códigos/Funciones útiles:producer.c y consumer.c (para mapear el archivo), Procesos-Mutex.c (para sincronización).
    Fórmulas y llamadas clave: pthread_mutex_t, shm_open, ftruncate, mmap, pthread_cond_t, waitpid, posix_spawn.

## Flujo de trabajo:
### Tarea principal: Ordenar ya sea de menor a mayor o vice versa usando multinucleo, multiproceso y multihilo
### Restricciones: No usar bibliotecas de llamada de archivos csv, crear lo nuestro, no usar std::sort o la biblioteca estandar.
Pasos de Ejecución:
Paso 0: Preparación y Muestreo (El Maestro)

    El Maestro crea los bloques gigantes de memoria compartida (shm_open + ftruncate + mmap).

    Lee y parsea el archivo de texto generado sin librerías externas y deposita los 100 millones de números en la Memoria A.

    Muestreo para Samplesort:

        El maestro toma una muestra representativa de la Memoria A (por ejemplo, k×p elementos aleatorios o distribuidos).

        Ordena localmente esa muestra (usando Merge Sort propio).

        Selecciona p−1 splitters (puntos de corte) que dividan hipotéticamente los datos en p partes iguales.

    Escribe los splitters en el bloque de memoria compartida reservado para ello.

    Lanza los procesos trabajadores usando posix_spawn.

Fase 1: Dispersión y Conteo (Scatter)

    Cada proceso hijo genera sus hilos.

    Los hilos leen sus bloques asignados de la Memoria A.

    Usando Búsqueda Binaria contra los splitters guardados en memoria compartida, cada hilo determina a qué cubeta (0 a p−1) pertenece cada uno de sus elementos.

    Los hilos contabilizan cuántos elementos aporta su bloque a cada cubeta para calcular de forma global los desplazamientos (offsets) exactos en la Memoria B.

Fase 2: Clasificación y Ordenamiento Local (Sort)

    Con los offsets de cada cubeta definidos, los hilos mueven los elementos a su cubeta correspondiente en la Memoria B.

    Cada hilo/proceso toma su cubeta asignada y le aplica el algoritmo de ordenamiento interno escrito desde cero (ej. Merge Sort local).

    Como los splitters del Samplesort garantizan un balanceo de carga óptimo, las cubetas tienen un tamaño muy similar, evitando que un hilo trabaje de más mientras los otros quedan ociosos.

Fase 3: Concatenación Final (Gather)

    Debido a que las cubetas ya están delimitadas en sus posiciones ordenadas dentro de la Memoria B y cada una está internamente ordenada, el arreglo global de 100 millones de elementos queda completamente ordenado de forma implícita.

    El proceso maestro valida la finalización mediante semáforos/waitpid, guarda el resultado ordenado en el archivo de salida y detiene los cronómetros de ejecución.

Num de nucleos hijo, obtener el num. de procesos del sistema ? 
4 kb tamaño de pagina 

verificar depurador de vscode 

En c++, string, usar vector...
