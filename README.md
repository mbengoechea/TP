Estructuras principales
1.	t_storage_fs (la estructura principal del filesystem)
•	Campos relevantes (resumen):
o	uint32_t fs_size: tamaño total del FS en bytes.
o	uint32_t block_size: tamaño de cada bloque físico en bytes.
o	uint32_t block_count: cantidad total de bloques físicos.
o	t_bitarray* bitmap: bitarray que representa bloques libres/ocupados.
o	char bitmap_path[MAX_BLOCK_PATH], superblock_path[], hash_index_path[]: rutas de archivos en disco.
o	char physical_blocks_dir[MAX_BLOCK_PATH]: directorio donde están los archivos de bloques físicos.
o	char files_dir[MAX_BLOCK_PATH]: directorio donde están los archivos lógicos (files/<file>/<tag>/metadata).
•	Propósito:
o	Representa la configuración y estado global del almacenamiento en memoria.
o	Contiene el bitarray para encontrar/gestionar bloques libres.
o	Tiene las rutas para persistencia (bitmap, superblock, hash index, directorio de bloques, directorio files).
Cómo se usa:
•	Se instancia y rellena al arrancar la Storage (función setear_o_usar_fs en storage.c).
•	cant_bloques = fs_size / block_size y se reserva un array de mutexes por bloque físico.
•	Las funciones reservar_bloque_fisico() y liberar_bloque_fisico() operan sobre el bitmap y actualizan este struct.
2.	t_file_tag_metadata (metadata por File:Tag)
•	Campos principales:
o	char file_name[MAX_FILE_NAME]
o	char tag_name[MAX_TAG_NAME]
o	uint32_t size: tamaño actual del File:Tag en bytes
o	t_file_tag_state state: enum { WORK_IN_PROGRESS, COMMITED }
o	int* block_numbers: array dinámico con índices de bloques físicos asignados
o	uint32_t block_count: cantidad de bloques asignados
•	Propósito:
o	Representa la metadata de un par file:tag — su tamaño, estado y qué bloques físicos ocupan.
o	Se guarda en disco como un archivo metadata (se construye BLOCKS=[...], SIZE=..., STATE=...).
•	Funciones relacionadas:
o	t_file_tag_metadata_create(...) — inicializa la estructura en memoria.
o	t_file_tag_metadata_save(path, m) — serializa y graba la metadata en disk (usa mutex_metadata).
o	t_file_tag_metadata_destroy(m) — libera memoria (block_numbers y struct).
3.	t_worker
•	Campos:
o	char* id (identificador de worker)
o	int socket (socket asociado)
•	Propósito:
o	Mantener lista/diccionario de workers conectados al Storage (diccionario_workers).
o	Se usa para handshake y envío de info como block_size.
Arquitectura en disco (layout)
•	Punto de montaje (punto_montaje) — raíz del FS configurado.
•	Archivos y directorios creados bajo ese punto:
o	bitmap.bin (archivo con bitmap persistente)
o	superblock.config (config con parámetros fs_size/block_size)
o	blocks_hash_index.config (índice/hash de bloques — para mapeos/referencias)
o	physical_blocks/ — directorio con archivos de bloques físicos (cada bloque físico representado por un archivo)
o	files/ — directorio con la estructura lógica de files:
	files/<nombre_archivo>/<nombre_tag>/metadata (archivo de metadata por File:Tag)
	posiblemente otras estructuras lógicas (archivos temporales, etc.)
•	Comportamiento de fresh_start:
o	Si fresh_start = TRUE el código limpia ciertos estados anteriores (se preserva superblock según comentarios).
Relación entre metadata, bitmap y bloques físicos
•	Cuando se necesita espacio para un File:Tag:
o	Se llaman funciones que buscan bits libres en bitmap (reservar_bloque_fisico).
o	El índice de bloque físico se añade al array block_numbers de t_file_tag_metadata.
o	Se crea el bloque físico correspondiente en physical_blocks (p. ej. archivo block<N>).
o	Cuando se libera espacio, se desasignan bloques y se actualiza bitmap y metadata.
•	El campo block_numbers almacena índices (enteros) que son usados para leer/escribir en bloques físicos.
•	Existe un archivo blocks_hash_index.config que hace de índice adicional entre bloques y rutas lógicas (útil para búsquedas inversas o reconstrucción).
Sincronización / concurrencia
•	mutex_bitmap: protege operaciones sobre el bitmap para evitar races al reservar/liberar bloques.
•	mutex_metadata: protege lecturas/escrituras de archivos de metadata (t_file_tag_metadata_save/load).
•	mutex_hash_index: protege el acceso al archivo/estructura hash index.
•	mutexes_bloques_fisicos: array de mutexes, uno por cada bloque físico, para serializar accesos al contenido de cada bloque individual (lectura/escritura de un bloque).
•	Además hay un mutex para diccionario de workers (mutex_workers).
•	Las funciones de t_file_tag_metadata_save usan pthread_mutex_lock(&mutex_metadata) al grabar.
Funciones API que operan sobre estas estructuras (prototipos en storage/include/storage.h)
•	reservar_bloque_fisico(t_storage_fs* fs)
•	liberar_bloque_fisico(t_storage_fs* fs, int bloque)
•	crear_logical_block(t_storage_fs* fs, char* logical_path, int bloque_fisico)
•	eliminar_logical_block(char* logical_path)
•	actualizar_metadata_config(char* path_metadata, int nuevo_tamanio, char* blocks_str)
•	path_metadata(char* file, char* tag)
•	asignar_bloques_logicos(...) y desasignar_bloques_logicos(...) — funciones de alto nivel que actualizan metadata y bitmap según tamaño nuevo. Estas funciones implementan la lógica de CREATE / TRUNCATE / WRITE / DELETE / COMMIT en storage.
Flujo típico de operaciones (resumen práctico)
•	CREATE file:tag:
o	crear directorio files/<file>/<tag>
o	crear metadata inicial (size=0, state=WORK_IN_PROGRESS, block_numbers=NULL)
o	guardar metadata en disco.
•	WRITE/TRUNCATE:
o	calcular cuántos bloques necesita el nuevo tamaño
o	asignar/desasignar bloques físicos (bitmap + crear/eliminar archivos en physical_blocks)
o	actualizar block_numbers y block_count en la metadata
o	guardar metadata (con mutex_metadata)
•	COMMIT:
o	cambiar state -> COMMITED en metadata y persistir
o	posiblemente acciones de sincronización con master/otros.
•	DELETE:
o	leer metadata, liberar los bloques indicados en bitmap y borrar entradas en blocks_hash_index
o	eliminar directorio files/<file>/<tag> y su metadata.
Dónde mirar en el código para detalles concretos
•	Definición: storage/include/structs.h
•	Guardado y formato de metadata: storage/src/structs.c (t_file_tag_metadata_save, load y destroy)
•	Inicialización de FS y paths/bitmap: storage/src/storage.c (setear_o_usar_fs, variables globales, creación de mutexes por bloque)
•	Handlers de protocolo (lo que responde a las llamadas de los workers/master): storage/src/handlers.c
•	main y lectura de config: storage/src/main.c (inicializa punto_montaje, block_size, fs_size y llama a setear_o_usar_fs)
Notas adicionales / advertencias
•	La metadata guarda los índices de bloques físicos (no punteros a contenido). El contenido real está en archivos de physical_blocks.
•	Hay protección por mutex a distintos niveles para evitar condiciones de carrera: bitmap global, metadata por archivo y mutex por bloque físico para accesos concurrentes al contenido.
•	El código hace uso de commons/bitarray para gestionar el bitmap, y de archivos config para persistir la metadata.


•	#define MAX_BLOCK_PATH 256
o	Macro que define la longitud máxima de rutas relacionadas con bloques (buffer estático para rutas).
•	#define MAX_FILE_NAME 128
o	Tamaño máximo en caracteres para el nombre lógico de un archivo.
•	#define MAX_TAG_NAME 64
o	Tamaño máximo para el nombre de una tag.
•	// Estados de un File:Tag
o	Comentario explicativo.
•	typedef enum { WORK_IN_PROGRESS, COMMITED } t_file_tag_state;
o	Enum que modela el estado de un File:Tag:
	WORK_IN_PROGRESS: cambios aún no "commitados" (permiten escrituras).
	COMMITED: estado final, probablemente lectura permitida y no más escrituras.
•	// Estructura principal del FileSystem
o	Comentario explicativo.
•	typedef struct { uint32_t fs_size; // Tamaño total del FS (bytes) uint32_t block_size; // Tamaño de cada bloque físico (bytes) uint32_t block_count; // Cantidad total de bloques físicos t_bitarray* bitmap; // Bitarray de bloques físicos char bitmap_path[MAX_BLOCK_PATH]; char superblock_path[MAX_BLOCK_PATH]; char hash_index_path[MAX_BLOCK_PATH]; char physical_blocks_dir[MAX_BLOCK_PATH]; char files_dir[MAX_BLOCK_PATH]; } t_storage_fs;
o	Definición de t_storage_fs, la estructura que representa el filesystem en memoria:
	fs_size: tamaño total del filesystem en bytes (configuración).
	block_size: tamaño de cada bloque físico en bytes (configuración).
	block_count: número total de bloques físicos (normalmente fs_size / block_size).
	bitmap: puntero a t_bitarray que representa qué bloques están libres/ocupados.
	bitmap_path: ruta al archivo persistente del bitmap.
	superblock_path: ruta al archivo superblock (config con parámetros del FS).
	hash_index_path: ruta al archivo con el índice/hash de bloques (blocks_hash_index).
	physical_blocks_dir: ruta al directorio con los archivos de bloques físicos.
	files_dir: ruta al directorio raíz donde se almacenan los files lógicos (files/...).
o	Observaciones:
	Esta estructura agrupa estado en memoria y rutas de persistencia.
	block_count y bitmap deben mantenerse coherentes; muchas funciones del módulo trabajan con este struct.
•	// Metadata de un File:Tag
o	Comentario.
•	typedef struct { char file_name[MAX_FILE_NAME]; char tag_name[MAX_TAG_NAME]; uint32_t size; // Tamaño del File:Tag (bytes) t_file_tag_state state; // Estado (WORK_IN_PROGRESS/COMMITED) int* block_numbers; // Array con índices de bloques físicos uint32_t block_count; // Cantidad de bloques asignados } t_file_tag_metadata;
o	Definición de t_file_tag_metadata: metadata que describe un file:tag lógico.
	file_name: nombre del archivo lógico (fijo en buffer estático).
	tag_name: nombre de la tag asociada al archivo.
	size: tamaño actual en bytes del file:tag — usado para bounds en lectura/escritura.
	state: estado (WORK_IN_PROGRESS o COMMITED).
	block_numbers: puntero dinámico a un array de int; cada int es el índice de un bloque físico asignado.
	block_count: cantidad de entradas válidas en block_numbers.
o	Observaciones:
	block_numbers es dinámico: debe ser malloc/free apropiadamente. Cuando block_count == 0, block_numbers puede ser NULL.
	Este struct se serializa en disco (ver funciones de save/load en structs.c) y es la fuente de verdad para dónde leer/escribir datos lógicos.
	size y block_count deben mantenerse sincronizados según block_size (p. ej. block_count = ceil(size / block_size)).
•	typedef struct{ char* id; int socket; } t_worker;
o	Estructura simple para representar un worker conectado:
	id: identificador del worker (cadena dinámica).
	socket: descriptor del socket asociado.
o	Se usa para llevar una lista/diccionario de workers conectados al Storage y enviarles handshakes/info.
•	t_file_tag_metadata* t_file_tag_metadata_create( char* nombre_archivo, char* nombre_tag);
o	Prototipo de función que crea/inicializa una estructura t_file_tag_metadata en memoria, asignando file_name y tag_name y valores iniciales (size=0, state=WORK_IN_PROGRESS, block_numbers=NULL, block_count=0).
o	Implementada en storage/src/structs.c.
•	void t_file_tag_metadata_save( char* path, t_file_tag_metadata* m);
o	Prototipo para guardar/serializar la metadata en disco en la ruta path. Debe guardar SIZE=..., STATE=..., BLOCKS=[...], etc.
o	En el código la implementación usa mutex_metadata para sincronizar escrituras.
•	void t_file_tag_metadata_destroy(t_file_tag_metadata* m);
o	Libera memoria asociada a la metadata (free block_numbers y free del struct).
•	void t_file_tag_metadata_load(char* path, t_file_tag_metadata* m);
o	Carga desde disco la metadata existente en path y rellena la estructura apuntada por m.
o	Observación: firma sugiere que m ya está asignado y la función lo rellena; revisar implementación para ver si mallocs internos son hechos.
•	void agregar_worker(char* worker_id, int socket_fd);
o	Prototipo para agregar un worker al diccionario/lista de workers. Debe almacenar (id, socket).
•	void quitar_worker_por_socket(int socket_fd);
o	Elimina worker asociado a un socket (útil al detectar desconexión).
•	void free_worker(void* ptr);
o	Función auxiliar para liberar memory de un t_worker (útil al usar listas y pasar como callback).
•	#endif
o	Cierre del include guard.
Notas generales y consideraciones prácticas
•	Tamaños de buffers: file_name y tag_name son buffers estáticos con longitudes limitadas. El código que copie nombres debe asegurarse de no desbordarlos (strncpy, etc.).
•	block_numbers es un array dinámico de int. Hay que usar correctamente malloc/realloc/free y actualizar block_count.
•	Serialización y concurrencia:
o	El guardado/lectura de metadata en disco está sincronizado externamente (mutex_metadata). No verás el mutex aquí porque está declarado extern en otros módulos (main.c / storage.c).
o	El bitmap se gestiona con t_bitarray* y operaciones atómicas protegidas por mutex_bitmap.
•	Coherencia entre size y block_count: el código tiene funciones (asignar_bloques_logicos/desasignar_bloques_logicos) que se encargan de garantizar que la metadata refleje correctamente los bloques asignados para el tamaño solicitado.
•	Uso de rutas: t_storage_fs contiene rutas y directorios; la función setear_o_usar_fs construye esas rutas y crea directorios/archivos según fresh_start.
Explicación detallada, paso a paso (t_file_tag_metadata_save)
1.	pthread_mutex_lock(&mutex_metadata);
•	Se adquiere el mutex global de metadata para evitar concurrencia entre lecturas/escrituras de metadata. Esto asegura que la operación sea atómica respecto a otras cargas/guardados de metadata.
2.	const char* estado_str = ...;
•	Se elige la representación en texto del estado (WORK_IN_PROGRESS o COMMITED) para escribirlo en el archivo.
3.	Construcción del string BLOCKS=[...]
•	Calcula una capacidad tentativa:
o	size_t cap = 2 + block_count * 12;
o	Razonamiento: mínimo "[]" (2) y se reserva ~12 bytes por bloque (espacio heurístico para el número y la coma).
•	Reserva blocks_str con malloc(cap >= 3 ? cap : 3) para asegurar al menos espacio para "[]\0".
•	Si block_count == 0 => blocks_str = "[]".
•	Si hay bloques:
o	Inicia con '[' y luego itera i sobre m->block_count.
o	Para cada bloque usa snprintf para añadir "n," o "n" (sin coma en el último).
o	Verifica retorno de snprintf (n < 0 o n >= espacio restante) → en ese caso logea error, libera blocks_str y desbloquea mutex retornando (previene overflow).
o	Al final escribe ']' y termina con '\0'.
•	Comentario: la función evita overflow con comprobaciones en cada snprintf, pero la capacidad inicial es heurística (12 por bloque).
4.	Construcción del texto final
•	size_t need = 64 + strlen(blocks_str);
o	Reserva un buffer texto con margen fijo + tamaño del blocks_str.
•	snprintf(texto, need, "TAMAÑO=%u\nESTADO=%s\nBLOCKS=%s\n", ...)
o	Si snprintf indica overflow (n < 0 o n >= need) → log de error y retorno liberando recursos.
•	Libera blocks_str (ya incorporado en texto).
5.	Creación de tmp_path
•	Construye tmp_path = path + ".tmp" (alloc plen+5).
•	Esto prepara la escritura en un archivo temporal para luego renombrarlo atómicamente al path final.
6.	fopen(tmp_path, "w") y fwrite(texto)
•	Abre el archivo temporal para escritura.
•	Intenta escribir todo el contenido (writ == want).
•	Si write corto → log, fclose, unlink(tmp_path), liberar, retornar.
7.	fflush + fsync + fclose
•	fflush(f) para volcar buffers de stdio; fsync(fileno(f)) para asegurar que datos alcanzaron disco antes de cerrar.
•	Luego cierra el FILE.
8.	rename(tmp_path, path)
•	Renombra el archivo temporal al nombre final. rename es atómico en archivos del mismo filesystem: garantiza que lectores vean siempre o la versión anterior completa o la nueva completa (siempre que los lectores no tengan abiertos los descriptors anteriores y coordinen con el mutex).
•	Si falla rename → log, unlink(tmp_path), liberar, retornar.
9.	Liberación y pthread_mutex_unlock(&mutex_metadata)
•	Libera tmp_path y texto, y finalmente libera el mutex.
Puntos importantes de la implementación (por qué se hace así)
•	Mutex: protege contra lecturas/escrituras concurrentes de metadata y evita races.
•	Uso de archivo temporal + rename: patrón clásico para reemplazo atómico del archivo de metadata (evita archivos parcialmente escritos si hay fallo durante escritura).
•	fflush + fsync: asegura persistencia de datos a disco antes del rename; mejora durabilidad frente a caídas.
•	Comprobaciones de overflow y errores: el código valida snprintf y malloc, y en caso de error limpia y sale.
Posibles mejoras / riesgos / observaciones
•	Capacidad heurística para blocks_str (cap = 2 + block_count * 12): funciona en la práctica si los números de bloque son de magnitud moderada. Si los índices de bloque crecen mucho (más dígitos) o block_count es grande, podría ser insuficiente: la función detecta overflow durante snprintf y aborta, pero sería más robusto calcular exactamente los dígitos necesarios o usar un buffer dinámico con realloc cuando haga falta.
•	Alcance del lock: la función adquiere mutex_metadata antes de construir blocks_str y texto; esto hace que otras operaciones de metadata esperen mientras se formatea la cadena y se realizan mallocs. Para reducir latencia de bloqueo se podría:
o	Formatear bloques/texto fuera del lock (operación in-memory, sin acceso compartido), adquirir mutex justo antes de abrir/renombrar el archivo y escribir (manteniendo coherencia), o
o	Construir todo y luego adquirir lock sólo para la parte de I/O/persistencia. Sin embargo, mantener el lock durante todo el proceso simplifica la lógica y evita que otro hilo lea metadata intermedia.
•	Hold de mutex durante fsync/rename: fsync puede ser relativamente lento; mantener el mutex durante ese tiempo penaliza concurrencia. Evaluar trade-off entre seguridad y concurrencia.
•	Uso de funciones C estándar: el patrón tmp+rename+fsync es apropiado y portable; alternativas modernas son O_TMPFILE / renameat2 cuando se quieren garantías adicionales.
•	manejo de errores: el código hace buen trabajo liberando recursos y desbloqueando antes de retornar en la mayoría de los caminos de error.
Explicación breve de t_file_tag_metadata_load (pasos clave)
•	pthread_mutex_lock(&mutex_metadata);
•	Usa config_create(path) para parsear archivo de metadata (formato TAMAÑO/ESTADO/BLOCKS).
•	Valida que existan las claves "TAMAÑO", "ESTADO" y "BLOCKS".
•	m->size <- config_get_int_value("TAMAÑO").
•	m->state <- compara ESTADO con "WORK_IN_PROGRESS" para decidir enum; si no igual, considera COMMITED.
•	Libera cualquier block_numbers previa y pone block_numbers=NULL y block_count=0.
•	Obtiene array de strings arr = config_get_array_value(cfg, "BLOCKS") donde arr es NULL o lista terminada en NULL.
o	Cuenta elementos c; si c>0, malloc(sizeof(int)*c) para m->block_numbers.
o	Convierte cada arr[i] con atoi a int y lo guarda en m->block_numbers[i]; libera arr[i] y al final free(arr).
o	Actualiza m->block_count = c.
•	config_destroy(cfg) y pthread_mutex_unlock(&mutex_metadata).
•	Maneja errores de apertura, claves faltantes y falta de memoria, liberando y retornando.
Formato resultante en disco (ejemplo)
•	Un archivo metadata típico tendrá este aspecto: TAMAÑO=4096 ESTADO=WORK_IN_PROGRESS BLOCKS=[1,3,17,42]
•	Este formato es el que save produce y load espera.
Notas sobre concurrencia y coherencia
•	El mutex_metadata serializa las operaciones save/load, por lo tanto no hay condiciones de carrera entre threads que carguen y guarden la metadata si todos usan ese mutex.
•	Lectores externos que no respeten mutex pueden ver la metadata mientras se está renombrando o justamente antes/después; el uso de rename atómico reduce ventanas de inconsistencia a nivel de sistema de archivos.


Copilot said: Voy a trazar un ejemplo concreto (con
Voy a trazar un ejemplo concreto (con números y contenido de archivos) que muestre exactamente cómo cambian en memoria y en disco:
•	el array meta->block_numbers del File:Tag,
•	el bitmap (bits libres/ocupados),
•	y el archivo de metadata en disco (contenido que guarda t_file_tag_metadata_save).
Me baso en el comportamiento real del código que revisamos (funciones: asignar_bloques_logicos / agrandar_file_tag / desasignar_bloques_logicos / achicar_file_tag / write_block_file_tag / reservar_bloque_fisico / liberar_bloque_fisico y t_file_tag_metadata_save).
Supuestos iniciales (para el ejemplo)
•	punto_montaje = /mnt/storage
•	block_size = 1024 bytes
•	bloques físicos totales (cant_bloques) = > 10 (hay espacio)
•	Bloque físico 0 existe y está inicializado con ceros.
•	Archivo lógico: file = "myfile", tag = "BASE".
•	Estado inicial de la metadata de myfile:BASE:
o	size = 1024 (1 bloque)
o	block_count = 1
o	block_numbers = [0] // el único bloque lógico apunta al bloque físico 0
o	state = WORK_IN_PROGRESS
•	Bitmap inicial (bits por índice de bloque físico): bit0 = 1 (ocupado), resto = 0 (libres).
•	En disco hay:
o	/mnt/storage/physical_blocks/block0000.dat (bloque físico 0)
o	/mnt/storage/files/myfile/BASE/logical_blocks/000000.dat -> hardlink a physical_blocks/block0000.dat
o	/mnt/storage/files/myfile/BASE/metadata.cfg con: TAMAÑO=1024 ESTADO=WORK_IN_PROGRESS BLOCKS=[0]
Operación 1 — TRUNCATE (agrandar) a 3072 bytes (3 bloques)
•	Qué hace el código:
o	Se calcula nuevos_bloques = ceil(3072/1024) = 3.
o	Se observa que nuevos_bloques (3) > meta->block_count (1), por eso se agranda.
o	Se llaman funciones para actualizar la metadata en memoria: agrandar_file_tag (o asignar_bloques_logicos en otra ruta).
o	agrandar_file_tag:
	realloc del array meta->block_numbers a tamaño 3.
	Para i desde 1 hasta 2 (los nuevos índices) pone meta->block_numbers[i] = 0 (apunta al bloque físico 0).
	Crea hard links en disk:
	link(/mnt/storage/physical_blocks/block0000.dat, /mnt/storage/files/myfile/BASE/logical_blocks/000001.dat)
	link(/mnt/storage/physical_blocks/block0000.dat, /mnt/storage/files/myfile/BASE/logical_blocks/000002.dat)
	actualiza meta->block_count = 3.
o	asignar_bloques_logicos (si se usa) también marca en bitmap el bit 0 (pero ya estaba marcado) y hace msync(bitmap).
o	Finalmente handle_truncate / quien controle la operación guarda metadata en disco con t_file_tag_metadata_save (tmp + rename).
•	Estado resultante (en memoria):
o	meta->size = 3072
o	meta->block_count = 3
o	meta->block_numbers = [0, 0, 0]
o	meta->state = WORK_IN_PROGRESS
•	Bitmap:
o	bit0 = 1 (ocupado)
o	resto bits = 0
•	Hardlinks (en disco):
o	/mnt/storage/physical_blocks/block0000.dat
o	logical_links:
	/mnt/storage/files/myfile/BASE/logical_blocks/000000.dat -> link a block0000.dat
	/mnt/storage/files/myfile/BASE/logical_blocks/000001.dat -> link a block0000.dat
	/mnt/storage/files/myfile/BASE/logical_blocks/000002.dat -> link a block0000.dat
o	El link count (st_nlink) de block0000.dat = 1 (physical_blocks file) + 3 (los 3 logical_blocks) = 4
•	metadata.cfg (guardado por t_file_tag_metadata_save) ahora contiene (ejemplo): TAMAÑO=3072 ESTADO=WORK_IN_PROGRESS BLOCKS=[0,0,0] (se escribió vía tmp + rename; bitmap se sincronizó con msync si se marcó el bit 0)
Observación importante: agrandar/Asignar no solicita nuevos bloques físicos libres: los nuevos bloques lógicos inicialmente apuntan al bloque físico 0 (optimización del proyecto: nuevos lógicos hardlinkean al bloque 0 hasta que se escriban).
Operación 2 — WRITE en el bloque lógico 1 (índice = 1), escribir 1024 bytes
•	Contexto:
o	meta->block_numbers[1] = 0 (apunta al físico 0)
o	link count de block0000.dat = 4 (>=2), por lo tanto el código entra en la rama Copy-On-Write (CoW).
•	Pasos exactos del código (write_block_file_tag):
i.	Valida bloque_idx válido y estado != COMMITED.
ii.	Obtiene nro_bloque_fisico_actual = meta->block_numbers[1] = 0.
iii.	calcular links = obtener_link_count(path_fisico_actual) -> devuelve 4.
iv.	Como links > 1 -> entra en CoW:
	llama a reservar_bloque_fisico(fs): recorre bitmap buscando primer bit 0 libre; supongamos que encuentra el bloque físico 5 libre, lo marca (bit5 = 1), hace msync(bitmap) y devuelve nro_bloque_nuevo = 5.
	crea/abre el archivo físico nuevo /mnt/storage/physical_blocks/block0005.dat para escritura (w+b) y copia el contenido del bloque físico actual (lectura de block0000.dat) hacia el nuevo bloque (para preservar datos no sobrescritos).
	aplica memcpy del buffer de escritura sobre el buffer copiado en la posición correspondiente (en este caso toda la página) y escribe ese bloque completo en block0005.dat.
	actualiza en memoria meta->block_numbers[1] = 5.
	actualiza el hard link lógico: unlink(/.../logical_blocks/000001.dat) // borra el link viejo link(/.../physical_blocks/block0005.dat, /.../logical_blocks/000001.dat) // crea nuevo hardlink al bloque 5
	Comprueba si el bloque físico viejo (0) quedó sin referencias: llama obtener_link_count(path_fisico_actual) otra vez. Dado que todavía existen los logical_blocks 000000.dat y 000002.dat enlazando a block0000.dat, el link count será > 1 (por ejemplo 3) y por tanto NO libera el bloque 0.
	Si el link count hubiera quedado <= 1 (es decir, nadie más enlaza a ese físico), la función libera el bit en el bitmap con liberar_bloque_fisico(fs, nro_bloque_fisico_actual) (esto limpia el bit y msync) y además elimina la entrada correspondiente del índice blocks_hash_index.config.
•	Estado resultante después del WRITE (CoW):
o	meta->size sigue siendo 3072 (la operación WRITE no cambia tamaño, salvo que sea la última por encima del size; en general TRUNCATE ajusta size).
o	meta->block_count = 3
o	meta->block_numbers = [0, 5, 0] // ahora el bloque lógico 1 apunta al físico 5
o	bitmap:
	bit0 = 1 (ocupado)
	bit5 = 1 (ocupado, recién reservado)
	resto = 0
o	Hardlinks y archivos en disco:
	/mnt/storage/physical_blocks/block0000.dat : sigue existiendo y tiene logical links 000000.dat y 000002.dat apuntando a él.
	/mnt/storage/physical_blocks/block0005.dat : nuevo archivo con contenido modificado.
	/mnt/storage/files/myfile/BASE/logical_blocks/000001.dat -> hardlink a physical_blocks/block0005.dat (actualizado).
o	metadata.cfg (si se guardó tras la operación) contendrá: TAMAÑO=3072 ESTADO=WORK_IN_PROGRESS BLOCKS=[0,5,0] (t_file_tag_metadata_save hace tmp + rename y msync/fflush según el código)
Caso alternativo: WRITE directo (sin CoW)
•	Si en vez de apuntar a un bloque físico compartido, el bloque lógico apuntara a un bloque físico con link count <= 1 (es decir, sin otros logical links), el código tomaría la rama de escritura directa:
o	bloquearía el mutex del bloque físico correspondiente, abriría el physical file en "r+b", fseek(0) y fwrite(buffer) directamente sobre ese bloque (sin reservar otro bloque ni cambiar metadata).
o	En ese caso meta->block_numbers y bitmap no cambian (solo cambia el contenido físico en el archivo blockNNNN.dat).
•	Esto es más eficiente, pero solo posible cuando no hay compartición (links <= 1).
Operación 3 — TRUNCATE a tamaño menor (achicar), por ejemplo 1024 bytes (1 bloque)
•	Si el usuario pide achicar a 1024 bytes:
o	Se calcula bloques_nuevos = ceil(1024/1024) = 1.
o	achicar_file_tag / desasignar_bloques_logicos se encargan de:
	Eliminar hard links lógicos que correspondan a índices i >= 1:
	unlink(/.../logical_blocks/000001.dat) // este fue el que apuntaba a block0005.dat
	unlink(/.../logical_blocks/000002.dat) // apuntaba a block0000.dat
	Para cada bloque físico eliminado, achicar_file_tag verifica obtener_link_count(path_bloque_fisico):
	Si link_count <= 1 (solo queda physical_blocks file), entonces llama liberar_bloque_fisico(fs, nro_bloque_fisico) para limpiar el bit en bitmap y hacer msync, y también elimina su entrada en blocks_hash_index.config.
	Si link_count > 1, no libera el bloque físico (otro tag/file aún lo referencia).
	Actualiza meta->block_count = 1 y realloc/free del array meta->block_numbers según corresponda; meta->size = 1024 (y se guarda metadata.cfg con t_file_tag_metadata_save).
•	Ejemplo concreto partiendo del estado anterior ([0,5,0]):
o	eliminar logical_blocks/000002.dat (era link a block0000.dat)
o	eliminar logical_blocks/000001.dat (era link a block0005.dat)
o	Para block0005.dat: tras unlink, obtener_link_count(path_bloque_fisico) probablemente = 1 (solo physical_blocks/block0005.dat) → entonces liberar_bloque_fisico marca bit5 = 0 en bitmap y se llama blocks_hash_index_remove_entry_for_block para borrar su hash del índice.
o	Para block0000.dat: tras unlink del 000002 y 000001 ya está unlinkado por 000000.dat? En nuestro flujo, 000000.dat todavía existe, por eso link count de block0000.dat pasada la operación será >1 (por ejemplo 2: physical_blocks + logical 000000.dat), por lo tanto NO se liberará block0000.dat.
o	Resultado final:
	meta->block_numbers = [0]
	meta->block_count = 1
	meta->size = 1024
	bitmap: bit0 = 1, bit5 = 0 (libre)
	metadata.cfg: TAMAÑO=1024 ESTADO=WORK_IN_PROGRESS BLOCKS=[0]
Persistencia y atomicidad (qué garantiza el código)
•	Metadata: t_file_tag_metadata_save escribe en un archivo temporal (path + ".tmp"), hace fflush + fsync y luego rename(tmp → metadata.cfg). Esto garantiza que no se deje metadata parcialmente escrita: o está la versión vieja o la nueva completa.
•	Bitmap: cuando el código modifica el bitarray (bitarray_set_bit / bitarray_clean_bit) hace msync(bitmap->bitarray, bitmap->size, MS_SYNC) para persistir cambios en el fichero bitmap.mmap.
•	Bloques físicos: cuando se crean nuevos archivos físicos se escribe con fwrite y fflush; para CoW se crea un nuevo archivo blockNNNN.dat y se escribe el bloque completo, luego se unlink/link los hardlinks lógicos; liberar_bloque_fisico limpia el bit en el bitmap (persistido) para poder volver a reservar ese bloque en el futuro.
Resumen (pasos clave y funciones involucradas)
•	TRUNCATE (agrandar):
o	calculo nuevos_bloques, realloc meta->block_numbers, set nuevos índices a 0 (agrandar_file_tag o asignar_bloques_logicos),
o	crear hard links a block0000.dat para los nuevos índices,
o	msync(bitmap) si se modificó el bit 0,
o	t_file_tag_metadata_save para persistir TAMAÑO/BLOCKS/ESTADO.
•	WRITE en bloque lógico:
o	si link_count <= 1 → escritura directa en blockNNNN.dat (sin cambiar metadata/bitmap).
o	si link_count > 1 → CoW:
	reservar_bloque_fisico (marca bit en bitmap y msync),
	copiar contenido, aplicar escritura, escribir nuevo blockNNNN.dat,
	actualizar meta->block_numbers[idx] = nuevo,
	actualizar hardlink lógico (unlink+link),
	si el bloque viejo quedó sin referencias → liberar_bloque_fisico y remover del índice de hashes.
o	t_file_tag_metadata_save guarda metadata si la metadata se modificó (por ejemplo, CoW cambia block_numbers).
•	TRUNCATE (achicar):
o	unlink de hardlinks lógicos que sobran,
o	para cada bloque físico eliminado: si link_count <= 1 → liberar_bloque_fisico (limpiar bit), eliminar indice hash,
o	realloc/free de meta->block_numbers y actualización de meta->size,
o	t_file_tag_metadata_save para persistir.
•	

t_file_tag_metadata_save(path, m)
o	Lock mutex_metadata.
o	Formatea BLOCKS=[...] construyendo blocks_str (verifica overflow).
o	Construye texto "TAMAÑO=...\nESTADO=...\nBLOCKS=[...]\n".
o	Escribe en un archivo temporal path+".tmp": fopen(tmp, "w"), fwrite, fflush, fsync.
o	rename(tmp → path) (operación atómica).
o	Libera buffers y unlock mutex_metadata.
o	Si falla en cualquier paso, hace log, libera recursos y desbloquea antes de retornar.
•	asignar_bloques_logicos(meta, nuevo_tamanio, block_size, bitmap, total_blocks)
o	Lock mutex_bitmap.
o	Calcula nuevos_bloques = ceil(nuevo_tamanio / block_size).
o	Si nuevos_bloques <= meta->block_count => nada que hacer, unlock y return.
o	Asegura que bit 0 esté marcado en bitmap (bitarray_set_bit(bitmap, 0)).
o	realloc meta->block_numbers a tamaño nuevos_bloques (si falla libera lock y retorna).
o	Inicializa los nuevos índices con 0 (los nuevos lógicos apuntan al bloque físico 0).
o	Actualiza meta->block_count y msync(bitmap->bitarray), unlock mutex_bitmap.
•	desasignar_bloques_logicos(meta, nuevo_tamanio, bitmap, block_size)
o	Lock mutex_bitmap.
o	Calcula nuevos_bloques = ceil(nuevo_tamanio / block_size).
o	Si nuevos_bloques >= meta->block_count => nada que hacer.
o	Para i de nuevos_bloques a meta->block_count-1: bitarray_clean_bit(bitmap, meta->block_numbers[i]).
o	Ajusta meta->block_count, realloc/free del array y msync(bitmap), unlock.
•	agrandar_file_tag / achicar_file_tag
o	agrandar_file_tag: realloc del array, para cada nuevo índice crea hard link lógico apuntando a physical_blocks/block0000.dat y pone meta->block_numbers[i]=0; actualiza meta->block_count.
o	achicar_file_tag: unlink de hardlinks lógicos sobrantes; si el bloque físico quedó sin referencias (st_nlink <= 1) libera bloque en bitmap (liberar_bloque_fisico) y remueve entrada del índice de hashes; luego realloc/free del array y actualiza meta->block_count.
•	write_block_file_tag(file, tag, bloque_idx, tamanio, buffer, meta, query_id)
o	Lock mutex_metadata al inicio (protege lectura/modificación de meta).
o	Valida índices y estado (no COMMITED).
o	Obtiene nro_bloque_fisico_actual = meta->block_numbers[bloque_idx]; obtiene link count (stat.st_nlink).
o	Si links <= 1 -> escritura directa:
	lock mutexes_bloques_fisicos[nro], fopen(r+b), fwrite(buffer), fflush, unlock mutexes_bloques_fisicos[nro], unlock mutex_metadata.
o	Si links > 1 -> CoW:
	reservar_bloque_fisico(fs) (lock mutex_bitmap dentro), marca bit en bitmap y msync.
	bloquear ordenado ambos mutexes de bloque físico (first/second).
	copiar contenido del viejo bloque -> new file; aplicar memcpy del buffer y fwrite al nuevo blockNNNN.dat.
	meta->block_numbers[bloque_idx] = nro_bloque_nuevo.
	unlink(link lógico viejo) y link(path_nuevo, logical_path) para actualizar hardlink.
	si antiguo bloque quedó sin referencias entonces liberar_bloque_fisico(fs, viejo) y quitar entrada del blocks_hash_index; else solo log.
	unlock mutexes y mutex_metadata.
o	Devuelve código de resultado; si metadata cambió, el handler llama a t_file_tag_metadata_save(path_meta, meta).
Handlers: flujo, llamadas, locks y efectos
1.	CREATE (handle_create)
•	Recibe paquete con nombre_archivo, nombre_tag, query_id.
•	Crear directorio lógico: crear_directorio_file_tag(nombre, tag) (crea files/<file>/<tag>/logical_blocks).
•	path = path_metadata(file, tag).
•	Si metadata ya existe: enviar ERROR_FILE_PREEXISTENTE.
•	meta = t_file_tag_metadata_create(...) → size=0, state=WORK_IN_PROGRESS, block_numbers=NULL.
•	t_file_tag_metadata_save(path, meta):
o	Guarda TAMAÑO=0, ESTADO=WORK_IN_PROGRESS, BLOCKS=[] con tmp+rename y mutex_metadata.
•	Verificación: config_create(path) y comprobación de keys; responde LISTO_OK al worker.
•	Observaciones:
o	CREATE no toca bitmap ni bloques físicos (size=0). El save usa mutex_metadata para evitar race con otros saves/loads.
o	Error paths liberan recursos y devuelven códigos apropiados.
2.	TRUNCATE (handle_truncate)
•	Recibe file, tag, nuevo_tamanio, query_id.
•	Valida que nuevo_tamanio sea múltiplo de block_size (salvo 0).
•	path_meta = path_metadata(...); verifica existencia.
•	meta = t_file_tag_metadata_create(...) y t_file_tag_metadata_load(path_meta, meta) (load usa mutex_metadata).
•	Si meta->state == COMMITED -> error WRITE_NOT_PERMITTED.
•	Decide:
o	Si nuevo_tamanio > meta->size -> agrandar_file_tag(meta, nuevo_tamanio, ...):
	realloc del array, para cada nuevo índice: set meta->block_numbers[i]=0 y creacion de hardlinks lógicos apuntando a block0000.dat
	actualiza meta->block_count.
	NOTA: agrandar_file_tag no toca bitmap salvo que se desee marcar bit0; en el init ya se marcó bit0.
o	Si nuevo_tamanio < meta->size -> achicar_file_tag(meta, nuevo_tamanio...):
	unlink de hardlinks lógicos desde nuevos_bloques hasta final, para cada bloque fisico eliminado:
	si obtener_link_count(path_fisico) <= 1 -> liberar_bloque_fisico(fs, nro) (haz msync) y blocks_hash_index_remove_entry_for_block (con mutex_hash_index).
	realloc/free del array y meta->block_count actualizado.
•	meta->size = nuevo_tamanio.
•	t_file_tag_metadata_save(path_meta, meta) -> (tmp+rename con mutex_metadata).
•	Responde LISTO_OK.
•	Observaciones:
o	TRUNCATE agrupará operaciones que modifican filesystem: creación/eliminación de hardlinks (filesystem ops), modificación del bitmap (liberar_bloque_fisico), actualización del índice de hashes y finalmente persistencia de metadata.
o	Locks: load/save usan mutex_metadata. liberar_bloque_fisico usa mutex_bitmap internamente. Cuando se llama blocks_hash_index_remove_entry_for_block se usa mutex_hash_index internamente.
o	Error handling correcto: si un unlink o remove falla, el handler intenta continuar (logging) y responde con error solo cuando corresponde.
3.	TAG (clonado) (handle_tag)
•	Recibe src_file, src_tag, dst_file, dst_tag, query_id.
•	path_src, path_dst; si dst existe → ERROR_FILE_PREEXISTENTE.
•	meta_src = create + t_file_tag_metadata_load(path_src) (load con mutex_metadata).
•	crear_directorio_file_tag(dst_file, dst_tag).
•	meta_dst = create vacío; copia size, state->WORK_IN_PROGRESS, block_count y block_numbers (memcpy).
•	t_file_tag_metadata_save(path_dst, meta_dst) — guarda metadata del tag clon (BLOCKS iguales).
•	sincronizar_hardlinks_tag(dst_file,dst_tag, meta_dst):
o	Para cada meta_dst->block_numbers[i] crea hard link: link(physical_blocks/blockNNNN.dat, files/dst/dst_tag/logical_blocks/%06d.dat)
o	Ese paso asegura que los logical_blocks del nuevo tag estén creados y apunten al mismo físico (hard link), incrementando link count del físico.
•	Responder LISTO_OK.
•	Observaciones:
o	Guardar metadata antes de sincronizar hardlinks es seguro porque la metadata ya refleja los bloques; sincronizar hardlinks crea los links físicos.
o	Locks: load/save usan mutex_metadata; sincronizar_hardlinks no usa mutex_metadata pero opera sobre el FS; el orden implementado (save then sync links) es válido porque la metadata describe los links que luego se crean.
4.	COMMIT (handle_commit)
•	Recibe file, tag, query_id.
•	path_meta = path_metadata(...).
•	meta = create + t_file_tag_metadata_load(path_meta, meta) (load con mutex_metadata).
•	Si meta->state == COMMITED -> log y responde ok.
•	Si no:
o	Para cada bloque lógico i:
	pthread_mutex_lock(&mutexes_bloques_fisicos[nro_bloque_actual]) para leer seguro y calcular hash (calcular_hash_bloque usa mmap y crypto_md5), unlock.
	buscar_nro_bloque_por_hash(hash) (usa mutex_hash_index) → si existe un bloque distinto con mismo hash:
	hacer deduplicación: unlink(path_logico), link(path_fisico_nuevo, path_logico), meta->block_numbers[i] = nro_bloque_existente.
	si antiguo bloque quedó sin referencias (st_nlink <=1): liberar_bloque_fisico(fs, antiguo) y blocks_hash_index_remove_entry_for_block(...) (mutex_hash_index).
	si no existe el hash en índice: agregar_hash_a_indice(hash, nro_bloque_actual) (mutex_hash_index).
o	meta->state = COMMITED
o	t_file_tag_metadata_save(path_meta, meta) (mutex_metadata, tmp+rename).
•	Responder LISTO_OK.
•	Observaciones:
o	COMMIT hace deduplicación por contenido. Mecanismo seguro: se bloquea cada bloque físico individual para hashear, se usa mutex_hash_index y finalmente se hace save de metadata.
o	Mutex_metadata NO está tomado durante toda la operación (la carga y el save usan el mutex interno), por lo que puede haber pequeñas ventanas con otros actores; sin embargo las operaciones sobre hardlinks y bitmap se sincronizan con otros mutexes.
5.	READ_BLOCK (handle_pagina_bloque)
•	Recibe query_id, file, tag, nro_bloque_logico.
•	path_meta -> t_file_tag_metadata_load(path_meta, meta) (load con mutex_metadata).
•	Valida índice vs meta->block_count.
•	nro_bloque_fisico = meta->block_numbers[nro_bloque_logico]
•	pthread_mutex_lock(&mutexes_bloques_fisicos[nro_bloque_fisico])
•	fopen physical file, fread block_size bytes, fclose, unlock mutex.
•	Envia BLOCK al worker en paquete LISTO_OK con contenido.
•	Observaciones:
o	Lock por bloque físico evita lecturas concurrentes mientras se escribe (protección fine-grained).
o	meta se destruye antes de leer el bloque físico, por eso no hay lock mutex_metadata durante lectura del bloque físico (la metadata fue cargada y no se modifica por la lectura). Si alguien modifica metadata después, el bloque físico es aún correcto por index.
6.	WRITE_BLOCK (handle_write_block)
•	Recibe file, tag, nro_bloque_logico, contenido (tamaño block_size), query_id.
•	path_meta -> create + t_file_tag_metadata_load(path_meta, meta) (load con mutex_metadata).
•	Valida no COMMITED y que nro_bloque_logico < meta->block_count.
•	Llama a rc = write_block_file_tag(...):
o	Dentro de write_block_file_tag (ver arriba): lock mutex_metadata al inicio para proteger meta, decide escritura directa (links <=1) o CoW (links>1).
o	En CoW llama reservar_bloque_fisico(fs) que lockea mutex_bitmap, marca bit y msync; luego bloquea mutexes de ambos bloques, copia/applica buffer, actualiza meta->block_numbers[idx] y actualiza hardlink lógico (unlink+link). Luego, si el bloque viejo quedó sin referencias libera bloque y elimina del hash index (con mutexes apropiados).
•	Si rc==0 => handler hace t_file_tag_metadata_save(path_meta, meta) para persistir la posible actualización de block_numbers (tmp+rename con mutex_metadata).
•	Finalmente responde al worker con resultado.
•	Observaciones:
o	write_block_file_tag toma mutex_metadata al inicio y lo mantiene hasta el final, por eso handler no toma mutex_metadata de nuevo (pero sí hace save con mutex_metadata, que se bloqueará hasta liberar el lock dentro de write).
o	El patrón es consistente: metadata en memoria se modifica bajo mutex_metadata; su persistencia se hace con la función save que adquiere el mismo mutex → evita race.
7.	DELETE (handle_delete)
•	Recibe file, tag, query_id.
•	Protege caso special initial_file:BASE.
•	path_meta, verifica existencia.
•	meta = create + load (load usa mutex_metadata).
•	Copia la lista de bloques a blocks_copy bajo pthread_mutex_lock(&mutex_metadata) (es decir hace: lock metadata, memcpy meta->block_numbers a blocks_copy y remove(path_meta) para “hacer visible” la eliminación; unlock metadata).
•	t_file_tag_metadata_destroy(meta).
•	Itera blocks_copy: para cada bloque lógico i:
o	path_bloque_logico = dir_tag/logical_blocks/%06u.dat
o	path_bloque_fisico = physical_blocks/block%04d.dat
o	unlink(path_bloque_logico) // elimina el hardlink lógico
o	si obtener_link_count(path_bloque_fisico) <=1 => liberar_bloque_fisico(fs, nro) y blocks_hash_index_remove_entry_for_block(...) (mutex_bitmap y mutex_hash_index internamente)
•	Intentos de limpieza de directorios logical_blocks y tag; responde LISTO_OK.
•	Observaciones:
o	DELETE hace la copia de block_numbers bajo mutex_metadata para evitar races con saves/loads y para garantizar que la lista de bloques a operar esté consistente.
o	Luego con blocks_copy fuera del mutex hace los unlink y liberaciones; liberar_bloque_fisico protegió al bitmap con mutex_bitmap.
8.	HANDSHAKE_STORAGE (handle_handshake_storage)
•	Recibe nombre del worker, llama agregar_worker(id, socket) que hace:
o	Crea t_worker, duplica key socket como string, pthread_mutex_lock(&mutex_workers) y dictionary_put(diccionario_workers, strdup(key_socket), w), total_workers++ y unlock.
•	Envía al worker HANDSHAKE_WORKER con block_size (información sobre el FS).
•	Observaciones:
o	No toca metadata/bitmap/bloques.
Puntos críticos de sincronización / race conditions / garantías
•	mutex_metadata: serializa todas las operaciones de load/save y protege modificaciones en meta en memoria. Está usado en:
o	t_file_tag_metadata_save (lock), t_file_tag_metadata_load (lock), write_block_file_tag (lock al inicio), partes de handlers (cuando hacen load+modificar+save).
•	mutex_bitmap: protege la estructura bitarray y msync del bitmap; usado por reservar_bloque_fisico, liberar_bloque_fisico, asignar/desasignar funciones.
•	mutexes_bloques_fisicos[]: array de mutexes uno por cada bloque físico; usado para leer/escribir/hashear/copiar bloques y evitar deadlocks se bloquean en orden (first/second).
•	mutex_hash_index: protege acceso al archivo blocks_hash_index.config (lectura/escritura).
•	Orden de locks: para evitar deadlocks el código ordena según el número de bloque físico cuando necesita bloquear dos mutexes de bloque, y evita holding prolongado de mutex_metadata durante operaciones lentas (aunque write_block_file_tag sí mantiene mutex_metadata durante operaciones de CoW — esto es deliberado para mantener consistencia de la meta en memoria).
•	Persistencia:
o	Metadata: tmp+rename + fflush+fsync → evita metadatas parciales.
o	Bitmap: msync(bitmap->bitarray, bitmap->size, MS_SYNC) después de set/clean.
o	Bloques físicos: se escribe en archivos blockNNNN.dat y se hace fflush (al menos) antes de link/unlink.
Caminos de error y limpieza
•	En la mayoría de las funciones hay comprobaciones de malloc, fopen, ftruncate, write, rename. Si algo falla:
o	Se loguea el error y se limpian los recursos (free, fclose, unlink tmp si corresponde).
o	Los handlers traducen códigos internos a errores reconocidos por el protocolo (ERROR_FILE_INEXISTENTE, ERROR_ESCRITURA_NO_PERMITIDA, ERROR_FUERA_DE_LIMITE, ERROR_ESPACIO_INSUFICIENTE).
•	Se intenta continuar cuando es seguro (por ejemplo si unlink de un logical block falla se loguea y se sigue) para no dejar la Storage en dead state; sin embargo se reportan warnings.
Ejemplo conciso integrando handlers + funciones (TRUNCATE agrandar + WRITE CoW)
•	Estado inicial:
o	meta->block_numbers = [0], bitmap: bit0=1
•	TRUNCATE -> agrandar a 3 bloques:
o	agrandar_file_tag: realloc meta->block_numbers => [0,0,0], crea hardlinks logical_blocks/000001.dat y 000002.dat → link count block0000.dat aumento.
o	t_file_tag_metadata_save -> escribe metadata.cfg con BLOCKS=[0,0,0] (tmp+rename).
•	WRITE al bloque lógico 1:
o	handle_write_block carga meta, llama write_block_file_tag.
o	write detecta links>1 => CoW -> reservar_bloque_fisico marca bit k=5 por ej. y msync.
o	Crea block0005.dat con copia+modificación, meta->block_numbers[1]=5, unlink logical_blocks/000001.dat y link a block0005.dat.
o	write devuelve 0; handler llama t_file_tag_metadata_save para persistir BLOCKS=[0,5,0].
•	Beneficio: solo el bloque modificado se duplica (CoW), los otros siguen compartiendo block0000.dat.
Recomendaciones / mejoras posibles
•	Reducir tiempo de hold del mutex_metadata:
o	muchas fases de formateo (con malloc/snprint) ocurren dentro del lock de metadata; sería posible construir el texto fuera del lock y luego adquirir mutex_metadata solo para la escritura/final rename.
o	actualmente se mantiene lock durante fsync/rename en t_file_tag_metadata_save → penaliza concurrencia.
•	Gestionar tamaño exacto para blocks_str antes de snprintf (o usar dynamic string con realloc) en lugar de heurística cap = block_count*12 para evitar abortos si números crecen.
•	Considerar O_TMPFILE / renameat2 o fsync del directorio (para mayores garantías en crash) si se requieren garantías más fuertes.
•	Revisar ordering global de locks cuando combine mutex_metadata + mutexes_bloques_fisicos para evitar inversion de orden improbable.


Visión general del filesystem
•	Punto de montaje: hay un directorio raíz configurado (punto_montaje, p. ej. /mnt/storage) donde se crea toda la estructura del FS.
•	Estructura en disco (principales rutas):
o	/<punto_montaje>/physical_blocks/blockNNNN.dat — archivos que contienen datos de cada bloque físico.
o	/<punto_montaje>/bitmap.bin — bitmap persistente (mapeado en memoria) que indica bloques libres/ocupados.
o	/<punto_montaje>/blocks_hash_index.config — índice (hash MD5 -> blockNNNN) usado para deduplicación.
o	/<punto_montaje>/files/<file>/<tag>/logical_blocks/<######.dat> — archivos “lógicos” por File:Tag (hard links hacia archivos en physical_blocks).
o	/<punto_montaje>/files/<file>/<tag>/metadata.cfg — metadata del File:Tag (TAMAÑO, ESTADO, BLOCKS).
•	Estructura en memoria principal: t_storage_fs contiene fs_size, block_size, block_count, puntero a t_bitarray (bitmap) y rutas.
Metadata (qué es y cómo se guarda)
•	Tipo en memoria: t_file_tag_metadata con campos:
o	file_name, tag_name (buffers estáticos)
o	size: tamaño en bytes del File:Tag
o	state: WORK_IN_PROGRESS o COMMITED
o	block_numbers: array dinámico de int → índice de bloque físico para cada bloque lógico
o	block_count: cantidad de bloques lógicos
•	Formato en disco (metadata.cfg):
o	Texto con 3 claves, ejemplo: TAMAÑO=3072 ESTADO=WORK_IN_PROGRESS BLOCKS=[0,5,0]
o	BLOCKS es un array con un entero por bloque lógico (cada número es el índice del bloque físico al que apunta).
•	Guardado seguro: la función t_file_tag_metadata_save escribe en path+".tmp", fflush+fsync, luego rename(tmp→path) — patrón atómico para evitar metadata parcial.
Bloques lógicos vs bloques físicos
•	Bloque físico: unidad real de almacenamiento (archivo blockNNNN.dat, tamaño block_size).
•	Bloque lógico: índice dentro de un File:Tag (p. ej. bloque lógico 0, 1, 2...). Cada bloque lógico está mapeado a un bloque físico mediante meta->block_numbers.
•	Hard links: los archivos lógicos (files/.../logical_blocks/000000.dat) son hard links hacia los archivos en physical_blocks. Es decir, el mismo inode (blockNNNN.dat) puede tener múltiples enlaces lógicos desde distintos File:Tag o posiciones lógicas.
•	Block 0: hay un bloque físico especial inicial (block0000.dat) con contenido inicial (ceros). Cuando se amplía un file sin asignar bloques reales, los nuevos bloques lógicos apuntan a block 0 (optimización para no reservar inmediatamente bloques físicos nuevos).
Bitmap y reserva/ liberación de bloques físicos
•	El bitmap (t_bitarray) marca para cada índice de bloque físico si está ocupado (1) o libre (0).
•	Está mapeado (mmap) y se hace msync tras cambios para persistirlo.
•	Reservar bloque físico:
o	recorrer el bitmap buscando un bit 0, setearlo a 1 y msync; devuelve índice reservado.
•	Liberar bloque físico:
o	limpiar el bit correspondiente (set → 0) y msync.
•	mutex_bitmap protege las operaciones sobre el bitmap.
Copy-On-Write (CoW) y escritura
•	Cuando se escribe en un bloque lógico:
o	Se mira el bloque físico actual al que apunta (meta->block_numbers[idx]) y se obtiene su link count (número de hardlinks al inode).
o	Si link count <= 1 (no compartido): escritura directa en el archivo physical_blocks/blockNNNN.dat (se abre en r+b y se sobrescribe).
o	Si link count > 1 (bloque compartido): se hace CoW:
a.	reservar un bloque físico libre (bitmap),
b.	crear un nuevo archivo blockNNNN_new.dat copiando el contenido del viejo,
c.	aplicar la escritura sobre la copia,
d.	actualizar meta->block_numbers[idx] = nro_bloque_nuevo,
e.	actualizar el hardlink lógico: unlink(logical_path) y link(path_nuevo, logical_path),
f.	si el bloque antiguo quedó sin referencias, liberar su bit en el bitmap y eliminar su entrada del índice de hashes.
•	Esto garantiza que escribir en un bloque compartido no corrompa otros tags/files que compartían ese bloque.
Deduplicación (COMMIT)
•	Al hacer COMMIT de un File:Tag, se calcula el hash (MD5) de cada bloque físico (se usa mmap + crypto_md5) y se busca en blocks_hash_index.config si ya existe un bloque con ese hash.
•	Si hay un bloque distinto con el mismo hash:
o	Se reasigna el hardlink lógico al bloque existente (unlink + link),
o	Se actualiza meta->block_numbers para apuntar al bloque existente,
o	Se libera el bloque viejo si quedó sin referencias y se elimina su entrada del índice de hashes.
•	Si el hash no existe, se agrega al índice: hash=blockNNNN.
•	Finaliza marcando meta->state = COMMITED y guardando metadata.
Operaciones principales (resumen del flujo)
•	CREATE:
o	crear directorios files/<file>/<tag>/logical_blocks,
o	crear metadata con size=0, state=WIP, BLOCKS=[] y guardarla.
•	TRUNCATE:
o	validar múltiplos de block_size (0 permitido),
o	cargar metadata,
o	si agrandar → realloc block_numbers, inicializar nuevos índices a 0 y crear hardlinks lógicos a block0000.dat (agrandar_file_tag o asignar_bloques_logicos),
o	si achicar → unlink de los hardlinks lógicos sobrantes; para cada bloque físico “eliminado”, si quedó sin referencias liberar bitmap y eliminar hash_index; ajustar meta->block_count y guardar metadata.
•	WRITE_BLOCK:
o	cargar metadata, validar permisos y rango,
o	llamar a write_block_file_tag: decide escritura directa o CoW, actualiza archivo físico y/o metadata,
o	si metadata fue modificada (CoW) guardar metadata.cfg (tmp+rename).
•	READ_BLOCK:
o	cargar metadata, validar índice,
o	abrir el block físico correspondiente y leer block_size bytes, enviarlos al requester.
•	TAG (clonar file:tag):
o	copiar metadata (size, block_numbers) a nuevo destino con state = WORK_IN_PROGRESS,
o	guardar metadata destino,
o	crear hardlinks lógicos en el nuevo directorio apuntando a los mismos bloques físicos (sin duplicar contenido).
•	DELETE:
o	cargar metadata, copiar lista de bloques,
o	eliminar metadata en disco (remove(path_meta)),
o	por cada bloque lógico unlink del hardlink lógico; si el bloque físico quedó sin referencias, liberar el bloque en bitmap y remover su hash del índice,
o	limpiar directorios.
•	HANDSHAKE:
o	registrar worker en diccionario (id + socket) y enviar block_size.
Sincronización y concurrencia
•	mutex_metadata: protege load/save y modificaciones en la metadata en memoria. t_file_tag_metadata_load/save y write_block_file_tag usan este mutex en puntos clave.
•	mutex_bitmap: protege operaciones sobre bitmap (set/clean + msync).
•	mutex_hash_index: protege acceso (lectura/escritura) al archivo blocks_hash_index.config.
•	mutexes_bloques_fisicos[]: array de mutexes, uno por bloque físico, para proteger lecturas/escrituras/hasheo de cada archivo blockNNNN.dat. Cuando se bloquean dos mutexes de bloque simultáneamente (p. ej. CoW entre bloque viejo y nuevo) el código adquiere los mutexes en orden numérico para evitar deadlocks.
•	Patrones importantes:
o	Guardado de metadata con tmp+rename para atomicidad.
o	msync del bitmap para persistencia inmediata de la asignación/liberación.
o	La persistencia de bloques físicos se hace con fwrite/fflush; en CoW se crea un archivo nuevo antes de cambiar hardlinks.
Detalles importantes / decisiones de diseño
•	Hard links permiten que varios File:Tag apunten al mismo bloque físico sin duplicación hasta que se escriba (share-on-read, copy-on-write on write).
•	El bloque 0 se usa como “singleton” al agrandar sin reservar: nuevos bloques lógicos inicialmente apuntan a block0000.dat; se reserva un bloque físico real (y se actualiza el mapping) sólo cuando se escribe sobre dicho bloque y es compartido (CoW).
•	La deduplicación (COMMIT) busca bloques con el mismo contenido (md5) y reasigna enlaces lógicos para ahorrar espacio.
•	Metadata es el mapa lógico → físico (block_numbers), por lo tanto es la “fuente de la verdad” sobre qué bloque físico contiene cada bloque lógico.
•	El índice de hashes (blocks_hash_index.config) mantiene mapping hash → blockNNNN usado para acelerar deduplicación.
Ejemplo breve (flujo típico)
•	Estado inicial: myfile:BASE size=1024, BLOCKS=[0], bit0 = 1.
•	TRUNCATE a 3072:
o	agranda block_count a 3, BLOCKS=[0,0,0], crea hardlinks lógicos 000001.dat y 000002.dat apuntando a block0000.dat.
•	WRITE en bloque lógico 1:
o	detecta block 0 compartido → reservar bloque físico libre (p. ej. 5), crear block0005.dat, copiar+escribir, actualizar BLOCKS → [0,5,0], actualizar hardlink lógico 000001.dat para que apunte a block0005.dat.
•	COMMIT:
o	hashea cada bloque; si encuentra bloques con mismo hash apunta ambos lógicos al mismo físico, libera los físicos sobrantes y actualiza índice.
Posibles puntos de mejora / riesgos
•	t_file_tag_metadata_save mantiene mutex_metadata mientras prepara y escribe el archivo (incluyendo fsync/rename), lo que puede incrementar latencia; mover parte del formateo fuera del lock reduciría bloqueo.
•	El cálculo heurístico del tamaño del buffer para BLOCKS podría provocar fallos si los números tienen muchos dígitos; el código detecta y aborta en caso de overflow, pero podría ser más robusto con un buffer dinámico que crezca con realloc.
•	Para mayores garantías de crash-consistency podría requerirse fsync del directorio donde se renombra el archivo (no solo del contenido) — actualmente se usa rename y fsync del FILE pero no del directorio.


Supuestos del ejemplo
•	punto_montaje = /mnt/storage
•	block_size = 1024
•	file = "myfile", tag = "BASE"
•	Estado final que ilustraré: meta->block_numbers = [0, 5, 0], size = 3072
•	bitmap marcado en 0 y 5 (ocupados), resto libres
•	hardlinks: logical_blocks/*.dat son hard links a archivos en physical_blocks
DIAGRAMA (vista simplificada)
/mnt/storage ├─ physical_blocks/ │ ├─ block0000.dat (inode A) ← contenido inicial (ceros) │ ├─ block0001.dat (inode free) │ ├─ block0002.dat (inode free) │ ├─ block0003.dat (inode free) │ ├─ block0004.dat (inode free) │ ├─ block0005.dat (inode B) ← copia modificada (resultado de CoW) │ └─ ... ├─ bitmap.bin (mapeado en memoria) └─ files/ └─ myfile/ └─ BASE/ ├─ logical_blocks/ │ ├─ 000000.dat -> hard link to ../physical_blocks/block0000.dat (inode A) │ ├─ 000001.dat -> hard link to ../physical_blocks/block0005.dat (inode B) │ └─ 000002.dat -> hard link to ../physical_blocks/block0000.dat (inode A) └─ metadata.cfg (contenido:) TAMAÑO=3072 ESTADO=WORK_IN_PROGRESS BLOCKS=[0,5,0]
Bitmap (visual)
•	Representación conceptual (índices):
índice: 0 1 2 3 4 5 6 7 ... bitmap: 1 0 0 0 0 1 0 0 ... (1 = ocupado, 0 = libre)
Link counts (st_nlink) aproximados
•	block0000.dat (inode A): st_nlink = 1 (physical_blocks) + 2 (000000.dat y 000002.dat) = 3
•	block0005.dat (inode B): st_nlink = 1 (physical_blocks) + 1 (000001.dat) = 2
Explicación del diagrama y cómo se llegó ahí (pasos resumidos)
1.	Estado inicial (antes de agrandar/escribir)
o	metadata.cfg tenía BLOCKS=[0], size=1024
o	logical_blocks/000000.dat → hard link a block0000.dat (inode A)
o	bitmap: bit0 = 1, demás 0
2.	TRUNCATE a 3072 (agrandar a 3 bloques)
o	Se amplía meta->block_count a 3 y se crean las entradas lógicas nuevas apuntando al bloque físico 0: meta.block_numbers = [0,0,0]
o	Se crean hard links: logical_blocks/000001.dat → hard link a block0000.dat logical_blocks/000002.dat → hard link a block0000.dat
o	Resultado intermedio: block0000.dat tiene st_nlink aumentado (multiples logical links).
o	metadata.cfg guarda BLOCKS=[0,0,0]
3.	WRITE en bloque lógico 1 (índice 1)
o	Antes de escribir: meta->block_numbers[1] == 0 (apunta al físico 0, compartido)
o	Se comprueba link count de block0000.dat: > 1 → hay compartición.
o	Se hace CoW: a) reservar un bloque físico libre (ej. índice 5): bitmap marca bit5 = 1 (msync). b) crear/cargar block0005.dat copiando contenido de block0000.dat. c) aplicar la escritura sobre block0005.dat. d) actualizar meta->block_numbers[1] = 5. e) actualizar hard link lógico:
	unlink logical_blocks/000001.dat (quita link al inode A)
	link physical_blocks/block0005.dat → logical_blocks/000001.dat (nuevo link al inode B) f) si el bloque viejo quedara sin referencias, se liberaría el bit correspondiente; en este caso sigue referenciado (000000 + 000002), por eso no se libera block0000.
o	metadata.cfg se actualiza a BLOCKS=[0,5,0] (save via tmp+rename).
Qué representa cada cosa y por qué es útil
•	physical_blocks/blockNNNN.dat
o	Contiene los datos reales por bloque físico (tamaño block_size). Es la unidad real de almacenamiento.
•	files/.../logical_blocks/XXXXXX.dat (hard links)
o	Son los “vistas lógicas” dentro de cada File:Tag. No contienen una copia del contenido si apuntan al mismo físico; son hard links al physical_blocks.
o	Ventaja: permite sharing eficiente y CoW cuando se escribe.
•	metadata.cfg
o	Es el mapping lógico → físico: cada entrada en BLOCKS es el índice de bloque físico que almacena el contenido del bloque lógico correspondiente.
o	Es la “fuente de la verdad” para lecturas/escrituras.
•	bitmap.bin
o	Estructura (mmap) que indica qué bloques físicos están libres/ocupados. Se usa para reservar nuevos bloques físicos y para liberar cuando dejan de estar referenciados.
o	Se msync tras cambios para persistir.
Comportamiento en lectura y escritura (cómo usa el diagrama)
•	READ_BLOCK (nro lógico X)
o	metadata.cfg → meta->block_numbers[X] → obtiene nro_bloque_fisico
o	lee physical_blocks/blockNNNN.dat y envía su contenido
o	bloqueo por mutexes_bloques_fisicos[nro] protege acceso concurrente
•	WRITE_BLOCK (nro lógico X)
o	si bloque físico apuntado está compartido (st_nlink > 1) → CoW (reservar nuevo físico, copiar, escribir, cambiar hardlink y actualizar metadata)
o	si no está compartido → escritura directa sobre el archivo physical_blocks/blockNNNN.dat
Garantías y atomicidad
•	Guardado metadata: escribe a path.tmp, fsync del archivo y luego rename(tmp→metadata.cfg) → evita metadatas parciales.
•	Bitmap: modificación con msync del área mmap, para persistir la asignación.
•	Hard links / unlink: las operaciones se coordinan con mutexes para evitar races; CoW asegura que la escritura no afecte a otros consumidores que compartían el bloque.
Extras: cómo actúa la deduplicación (COMMIT)
•	Durante COMMIT se calcula MD5 de cada physical block y se busca si ya existe otro bloque con ese MD5 en blocks_hash_index.config.
•	Si existe un bloque distinto con ese hash, se reasigna el hardlink lógico para apuntar al bloque existente (ahorrando espacio). Luego se libera el bloque antiguo si queda sin referencias.


CoW en este Storage (pasos concretos que hace el código)
1.	El handler / write verifica a qué bloque físico apunta el bloque lógico (meta->block_numbers[idx]).
2.	Obtiene el número de hardlinks del archivo físico (st_nlink).
o	Si st_nlink <= 1 → no hay otra referencia: se escribe directamente en el bloque físico (sin CoW).
o	Si st_nlink > 1 → el bloque está compartido → usar CoW.
3.	CoW:
o	reservar_bloque_fisico(fs) → marca un bit en el bitmap y devuelve un índice libre.
o	crear un nuevo archivo physical_blocks/blockNNNN.dat y copiar ahí el contenido del bloque viejo.
o	aplicar la escritura sobre esa copia.
o	actualizar meta->block_numbers[idx] = nro_bloque_nuevo (en memoria).
o	actualizar el hardlink lógico: unlink(logical_path) y link(path_nuevo, logical_path) (ahora el logical block apunta al nuevo bloque físico).
o	comprobar si el bloque viejo quedó sin referencias → si es así, liberar su bit en el bitmap y eliminar su entrada del índice de hashes.
4.	Guardar la metadata en disco (t_file_tag_metadata_save) si se modificó el mapping.
Ventajas de CoW
•	Eficiencia: evita copias hasta que son necesarias.
•	Seguridad/aislamiento: escrituras no afectan a otros que comparten la misma versión.
•	Facilita snapshots y deduplicación (puedes apuntar varios logical blocks al mismo físico).
Desventajas y riesgos
•	Sobrehead en escrituras compartidas: CoW hace una copia completa del bloque (lectura + escritura), así que escribir en un bloque muy compartido implica coste extra.
•	Fragmentación: CoW puede dispersar bloques lógicos en muchos bloques físicos nuevos.
•	Complejidad: requiere coordinación (locks) para mantener consistencia del bitmap, hardlinks y metadata.
•	Condiciones de carrera si no se sincroniza correctamente: p. ej. dos writes concurrentes podrían reservar bloques distintos y causar inconsistencias si no hay locking correcto.
•	Coste de I/O y uso de espacio temporal hasta que se libere el bloque viejo.
Consideraciones prácticas / puntos de atención (en este código)
•	El código usa st_nlink para detectar compartición; esto es simple y efectivo para hard links, pero:
o	Debes asegurarte de que el conteo sea correcto en sistemas de archivos remotos o con comportamientos especiales.
•	Locks:
o	mutexes_bloques_fisicos[] para proteger lectura/escritura/hasheo de cada blockNNNN.dat.
o	mutex_bitmap para proteger reserva/liberación de bits.
o	mutex_metadata para proteger la estructura meta->block_numbers en memoria y su persistencia.
o	El orden y alcance de locks reduce riesgo de deadlocks (p. ej. bloqueo ordenado de dos mutexes).
•	Persistencia:
o	Reservar bloque: marca el bit y hace msync del bitmap.
o	Metadata: se guarda con tmp+rename y fsync del archivo para evitar metadatas parciales.
o	Bloques: en CoW se escribe el nuevo archivo usando fwrite/fflush antes de cambiar los hardlinks.
Errores típicos al depurar CoW
•	Bloques reservados que nunca se liberan (leaks): ocurre si falla un paso tras reservar y no se libera el bit.
•	Metadata desincronizada: si la metadata no se guarda tras actualizar block_numbers, un crash puede dejar inconsistencias.
•	Races entre dos writers concurrentes: ambos pueden decidir hacer CoW y terminar con dos bloques distintos, si la lógica de locking/persistencia no coordina.
•	Problemas con link counts en FS que no soportan hard links igual que ext4 (ej. algunas soluciones en red).
Alternativas a CoW
•	Clonar siempre (copia eager) — seguro pero costoso en espacio/tiempo.
•	Versionado a nivel de bloque con copy-on-write pero usando estructuras que permitan más control (B+trees en ZFS/Btrfs).
•	Deduplicación previa al commit (lo hace este repo en COMMIT: tras guardar, intenta deduplicar por hash).
Resumen sencillo
•	CoW = compartir hasta que alguien escribe; la primera escritura crea una copia y el escritor trabaja sobre esa copia.
•	En el Storage: CoW protege a otros usuarios que comparten el mismo bloque físico (hard links), reservando un nuevo bloque físico, copiando, escribiendo y actualizando metadata y hardlinks.
•	Es eficiente en lectura/duplicación y seguro, pero añade overhead a escrituras compartidas y exige correcto manejo de locks y persistencia.



Mapeo entre consigna y el repo
•	query_control/ → Módulo Query Control
o	Responsabilidad según la consigna: leer archivo de query y prioridad, conectar al Master, enviar nombre del archivo_query + prioridad y esperar mensajes (lecturas o finalización). Debe registrar los logs obligatorios (conexión exitosa, envío de Query, lecturas recibidas, finalización).
o	Qué buscar en esa carpeta:
	Makefile + bin/query ejecutable.
	Un main.c (o similar) que parsea args: [archivo_config] [archivo_query] [prioridad].
	Uso de so-commons-library para logs y config (config_create, log_create, log_level_from_string).
	Código de sockets para conectar al Master y manejo de mensajes entrantes.
•	master/ → Módulo Master
o	Responsabilidad según la consigna: aceptar conexiones de Query Control y Workers, asignar id autoincremental a cada Query, mantener estados READY/EXEC/EXIT, reenviar lecturas desde Workers a su Query Control, realizar planificación (FIFO y Prioridades con desalojo + aging), manejar conexiones/desconexiones y logs obligatorios.
o	Qué buscar:
	Makefile + bin/master.
	Código de servidor TCP en el puerto definido por PUERTO_ESCUCHA en config.
	Estructuras de datos para cola READY, lista de EXEC y control de workers.
	Implementación de algoritmos de planificación (FIFO y PRIORIDADES), manejo de aging (timer/intervalos), y lógica de desalojo (solicitar PC al Worker, almacenar contexto).
	Logs obligatorios (conexión Query Control, conexión Worker, envío/desalojo, cambios de prioridad, finalizaciones, etc.).
•	worker/ → Módulo Worker
o	Responsabilidad según la consigna: conectarse a Storage y luego a Master (enviando su ID), ejecutar a la vez una sola Query asignada por el Master, interpretar instrucciones del archivo de Query (CREATE, TRUNCATE, WRITE, READ, TAG, COMMIT, FLUSH, DELETE, END), tener Memoria Interna (malloc único) con paginación por demanda, pedir bloques faltantes a Storage, aplicar reemplazo (LRU o CLOCK-M), respetar RETARDO_MEMORIA, atender desalojo (persistir páginas modificadas) y logs obligatorios de memoria y ejecución.
o	Qué buscar:
	Makefile + bin/worker.
	Código que hace handshake con Storage (obtener BLOCK_SIZE) y luego se conecta al Master.
	Query Interpreter: parser de líneas del archivo de query y ejecución instrucción por instrucción respetando PC (Program Counter).
	Módulo de Memoria Interna: malloc(TAM_MEMORIA) y tablas de páginas por File:Tag, implementación de LRU y/o CLOCK-M, manejo de fallos de página (fetch desde Storage), RETARDO_MEMORIA entre accesos.
	Logs obligatorios: recepción de Query, FETCH/PC, asignación/liberación de marcos, memoria-miss/add, reemplazos, lecturas/escrituras físicas, desalojos.
•	storage/ → Módulo Storage
o	Responsabilidad según la consigna: servidor multihilo que implementa FS montado en PUNTO_MONTAJE, mantener superblock.config, bitmap.bin (bitarray real), blocks_hash_index.config (mapa hash MD5 -> blockNNNN), directorio physical_blocks con archivos blockNNNN.dat, y directorio files con cada File/Tag (metadata.config + logical_blocks con hard links a physical blocks). Operaciones: CREATE, TRUNCATE, TAG, COMMIT (deduplicación por MD5), WRITE block, READ block, DELETE tag, manejo de errores y retardos RETARDO_OPERACION/RETARDO_ACCESO_BLOQUE y logs obligatorios.
o	Qué buscar:
	Makefile + bin/storage.
	Función de inicialización que lee FRESH_START y crea/formatéa estructura (superblock.config, bitmap.bin, physical_blocks, files, crear initial_file/BASE).
	Implementación de bitmap como bitarray binario (no usar texto simple que no representen bits).
	Uso de crypto_md5() (so-commons) para calcular hash del contenido de un bloque y actualizar blocks_hash_index.config.
	Operaciones de lectura/escritura de bloque con bloqueo/concorrencia (servidor multihilo) y logs obligatorios por cada acción (reservado/liberado, bloque lógico leído/escrito, hard link agregado/eliminado, deduplicaciones).
Cómo fluye el sistema (topología de ejecución)
1.	Inicializas storage (./bin/storage config_storage) — monta/maneja el FS y escucha conexiones de Workers.
2.	Inicias master (./bin/master config_master) — escucha Query Controls y Workers.
3.	Inicias uno o más workers (./bin/worker config_worker <ID>) — se conectan al Storage (handshake para BLOCK_SIZE) y luego al Master (registran ID).
4.	Inicias query control(s) (./bin/query config_query archivo_query prioridad) — se conecta al Master, envía path y prioridad y queda a la espera.
5.	Master recibe Query Control, asigna query_id y la coloca en READY; según algoritmo la envía a un Worker libre -> Worker pasa a ejecutar la Query.
6.	Worker abre el archivo de Query (desde PATH_QUERIES), interpreta instrucciones manteniendo PC, manipula Memoria Interna: para páginas faltantes pide bloques a Storage, realiza READs y devuelve lecturas al Master (que las reenvía al Query Control) y al final notifica END.
7.	Si llega una Query de mayor prioridad cuando todos los Workers ocupados y ALGORITMO=PRIORIDADES, Master solicita desalojo de la Query de menor prioridad a su Worker correspondiente; Worker responde con PC y persiste páginas modificadas (FLUSH implícito antes de desalojo) y Master pone la Query desalojada en READY con contexto para reanudar.


Visión general del repo (lo observado)
•	Carpetas principales: master/, worker/, storage/, query_control/, utils/
•	README.md con instrucciones generales (dependencias, compilación por módulo y uso de so-commons-library).
•	Cada módulo se compila por separado mediante su Makefile y entrega ejecutable en bin/.
Ahora, módulo por módulo:
1.	Query Control
•	Propósito funcional
o	Enviar al Master la solicitud de ejecución de una Query (ruta/archivo) con una prioridad.
o	Quedarse a la espera de mensajes del Master: lecturas realizadas por el Worker o aviso de finalización.
•	Componentes esperados en la carpeta
o	Ejecutable bin/query (o main.c + Makefile).
o	Código para: parsear args (archivo_config, archivo_query, prioridad), leer config, crear logger.
o	Cliente TCP que conecta al Master y maneja reconexión/recepción de mensajes.
•	Interacciones y protocolo
o	Al iniciar: handshake con Master (IP/PUERTO desde config).
o	Envía: nombre/ruta del archivo_query + prioridad.
o	Recibe: mensajes de lectura (contenido) y mensaje de fin (incluyendo motivo si hubo error).
•	Logs mínimos (deben aparecer)
o	Conexión exitosa al Master.
o	Envío de la Query (archivo y prioridad).
o	Cada lectura recibida: “Lectura realizada: File File:Tag, contenido: <CONTENIDO>”
o	Finalización de la Query con motivo.
•	Qué comprobar en el repo
o	Existencia de parser de config y uso de log_level_from_string / funciones de so-commons.
o	Implementación del socket/client connect y manejo de mensajes entrantes.
2.	Master
•	Propósito funcional
o	Gestión central de Queries: recibir de Query Controls, asignar ID autoincremental, mantener estados (READY, EXEC, EXIT), planificar envío a Workers, reenviar lecturas recibidas desde Workers al Query Control correspondiente, manejar desalojo y aging.
•	Componentes esperados
o	Servidor TCP (escucha en PUERTO_ESCUCHA) que acepta Query Controls y Workers.
o	Estructuras de datos: lista/cola de READY, lista de EXEC, tabla de Workers conectados, mapa id_query → conexión QueryControl/estado.
o	Implementación de dos algoritmos de planificación: FIFO y PRIORIDADES (con desalojo y aging).
o	Mecanismo de timers para aging (TIEMPO_AGING).
•	Interacciones y protocolos
o	Recibe conexión de QueryControl: asigna query_id, pone en READY.
o	Recibe conexión de Worker: marca Worker disponible y le asigna una Query según algoritmo.
o	Si PRIORIDADES y llega Query más importante: envía solicitud de desalojo al Worker con menor prioridad; el Worker retorna PC para reanudar luego.
o	Reenvía mensajes de lectura desde Worker a Query Control.
o	Maneja desconexiones: si QueryControl se desconecta cancela Query; si Worker se desconecta finaliza la Query en ejecución con error y notifica.
•	Logs mínimos (deben aparecer)
o	Conexión de Query Control (con path/prioridad y id).
o	Conexión/desconexión de Worker (y cantidad total).
o	Envío de Query a Worker.
o	Desalojo de Query en Worker (motivo).
o	Cambio de prioridad (aging).
o	Finalización de Query en Worker y envío de lecturas al Query Control.
•	Qué comprobar en el repo
o	Código del servidor (accept loop), estructuras de colas/colas priorizadas.
o	Implementación de desalojo (mensaje al Worker solicitando PC) y re-planificación con PC.
o	Uso del campo TIEMPO_AGING y ALGORITMO_PLANIFICACION en el config parser.
3.	Worker
•	Propósito funcional
o	Ejecutar una sola Query a la vez enviada por el Master; interpretar instrucciones de Query (CREATE, TRUNCATE, WRITE, READ, TAG, COMMIT, FLUSH, DELETE, END).
o	Mantener Memoria Interna con paginación por demanda, pedir páginas faltantes al Storage, aplicar algoritmo de reemplazo (LRU o CLOCK-M), respetar RETARDO_MEMORIA y manejar desalojos.
•	Componentes esperados
o	Cliente TCP que primero hace handshake con Storage (obtiene BLOCK_SIZE) y posteriormente se conecta al Master registrando su ID.
o	Query Interpreter: abrir archivo de Query desde PATH_QUERIES, parsear línea por línea, mantener Program Counter (PC).
o	Memoria Interna: un único malloc(TAM_MEMORIA), tablas de páginas por File:Tag, mecanismos de page-fault y fetch de bloques desde Storage.
o	Implementación de algoritmos de reemplazo: LRU y/o CLOCK-M (seleccionado por ALGORITMO_REEMPLAZO).
o	Manejo de RETARDO_MEMORIA (espera por cada acceso).
o	Manejo de desalojo: menerima petición del Master, flush de páginas modificadas al Storage y retorno del PC.
•	Interacciones y protocolos
o	Handshake Storage ↔ Worker para obtener tamaño de bloque.
o	Master ↔ Worker: asignación de Query, solicitud de desalojo, recepción de PC para reanudar.
o	Worker ↔ Storage: operaciones de lectura/escritura de bloques lógicos, TAG, CREATE, TRUNCATE, COMMIT, DELETE, etc.
o	Worker → Master → QueryControl: envía lecturas efectuadas.
•	Logs mínimos (deben aparecer)
o	Recepción de Query y path.
o	FETCH de instrucción con Program Counter.
o	Instrucción realizada.
o	Desalojo solicitado por Master.
o	Lectura/Escritura física en Memoria (dirección física y valor).
o	Asignación y liberación de marcos, memoria-miss/add, reemplazos de páginas.
•	Qué comprobar en el repo
o	Implementación del malloc único y estructura de tablas de páginas.
o	Funciones de fetch y write que contactan Storage cuando faltan páginas.
o	Parser e interpreter de instrucciones y respeto de delays RETARDO_MEMORIA.
o	Mecanismo de serialización de contexto (PC y páginas sucias) en desalojo.
4.	Storage
•	Propósito funcional
o	Implementar el File System que atiende peticiones de Workers: mantener superblock.config, bitmap.bin (bitarray real), blocks_hash_index.config (MD5->bloque físico), physical_blocks (archivos blockNNNN.dat) y files (File/Tag con metadata.config + logical_blocks como hard links).
o	Operaciones: CREATE, TRUNCATE, TAG, COMMIT (con deduplicación mediante MD5), WRITE bloque (copy-on-write si bloque referenciado), READ bloque, DELETE tag, manejo de errores (file/tag inexistente, permiso de escritura, espacio insuficiente).
•	Componentes esperados
o	Servidor multihilo que atiende conexiones de Workers (PUERTO_ESCUCHA).
o	Inicialización con FRESH_START: crear estructura de FS, superblock y primer initial_file/BASE con bloque 0 lleno de '0'.
o	Implementación del bitmap como bitarray persistido (bitmap.bin) — obligatorio como bitarray (no texto plano que no represente bits).
o	blocks_hash_index.config actualizado con crypto_md5() para deduplicar bloques.
o	Operaciones de bloqueo/concorrencia y retardos: RETARDO_OPERACION por petición y RETARDO_ACCESO_BLOQUE por cada bloque leído/escrito.
•	Interacciones y protocolos
o	Workers conectan y realizan solicitudes de operación (lectura de bloque lógico, escritura de bloque lógico, crear tag, truncar, commit, delete).
o	Para escritura: si bloque físico referenciado por varios logical_blocks, se debe buscar nuevo bloque físico (copy-on-write).
o	Para commit: buscar por hash MD5 en blocks_hash_index y reapuntar o agregar hash.
•	Logs mínimos (deben aparecer)
o	Conexión/desconexión de Worker.
o	File creado, truncado, tag creado, commit, tag eliminado.
o	Bloque lógico leído/escrito, bloque físico reservado/liberado, hard link agregado/eliminado, deduplicación de bloques.
•	Qué comprobar en el repo
o	Código de inicialización usando FRESH_START y creación del initial_file/BASE.
o	Implementación de bitmap como bitarray en archivo binario.
o	Uso de crypto_md5() (so-commons) y manejo persistente de blocks_hash_index.config.
o	Creación de hard links para logical_blocks que apunten a physical_blocks/blockNNNN.dat.
5.	utils (código compartido)
•	Propósito funcional
o	Contener utilidades compartidas entre módulos: serialización de mensajes, estructuras de protocolo, funciones comunes de logging/config, manejo de sockets, funciones de ayuda para File:Tag (parseo) o manejo de listas/colas si no se usa directamente so-commons.
•	Qué comprobar en el repo
o	Implementaciones de serializadores/deserializadores de mensajes usados entre Master/Worker/QueryControl/Storage.
o	Funciones auxiliares reutilizadas en varios módulos (p. ej. parseo de paths, creación de estructuras, wrappers de sockets).
Comportamiento global y flujo (resumen operativo)
1.	Lanzás storage (monta FS, escucha).
2.	Lanzás master (escucha conexiones).
3.	Lanzás workers (se conectan a storage y luego a master).
4.	Lanzás query_controls (envían path/prioridad al master).
5.	Master planifica y asigna queries a workers (FIFO o PRIORIDADES con desalojo + aging).
6.	Worker interpreta instrucciones, usa Memoria Interna paginada y consulta a Storage para bloques faltantes; envía lecturas al Master, que las reenvía al Query Control.
7.	Commit / Flush / Desalojo / Errores se manejan según la consigna y los logs deben reflejar cada operación obligatoria.



handle_create (líneas 40–131)
•	41: declaración de la función handle_create(int socket_cliente, t_log* logger).
•	42: usleep(retardo_operacion * 1000);
o	Introduce un retardo configurable (en ms) antes de procesar la operación.
•	43: log_info(logger, "CREATE");
o	Logea la recepción de la operación CREATE.
•	45: t_list* valores = recibir_paquete(socket_cliente);
o	Recibe el paquete proveniente del cliente; devuelve una lista con los elementos enviados.
•	46–51: if (!valores) { ... send error and return; }
o	Verifica recepción; en caso de fallo registra error, envía un código de error (WTF_ERROR) y retorna.
•	53–59: if (list_size(valores) != 3) { ... }
o	Valida que el paquete tenga exactamente 3 elementos; si no, libera memoria, envía error y retorna.
•	61–64: extrae los 3 elementos: nombre_archivo, nombre_tag y pointer a query_id; extrae valor query_id.
o	Convierte/obtiene los tipos esperados.
•	66: crear_directorio_file_tag(nombre_archivo, nombre_tag);
o	Crea (si no existe) la estructura de directorios para ese file:tag.
•	70: char* path = path_metadata(nombre_archivo, nombre_tag);
o	Construye la ruta al metadata.cfg del file:tag.
•	71–77: if (!path) { ... }
o	Manejo de error por falta de memoria/creación de path; log y respuesta de error.
•	80–89: FILE* fchk = fopen(path, "r"); if (fchk) { ... }
o	Si ya existe metadata (archivo abierto con éxito), se rechaza la creación con ERROR_FILE_PREEXISTENTE.
•	92: t_file_tag_metadata* meta = t_file_tag_metadata_create(nombre_archivo, nombre_tag);
o	Construye en memoria la estructura metadata inicial (probablemente con size=0, state=WIP, blocks=NULL).
•	93–100: if (!meta) { ... }
o	Si falló la creación en memoria, respuesta de error y limpieza.
•	103: t_file_tag_metadata_save(path, meta);
o	Guarda en disco la metadata inicial (crea metadata.cfg con campos TAMAÑO, ESTADO, BLOCKS).
•	106: t_config* cfg = config_create(path);
o	Intenta cargar el archivo metadata.cfg con la biblioteca commons/config para verificar su validez.
•	107–118: Validación explícita de que las propiedades TAMAÑO, ESTADO y BLOCKS existan. Si no, error y limpieza.
•	119: config_destroy(cfg);
o	Libera el objeto config.
•	121: log_info(logger, "##%d File Creado %s:%s", query_id, nombre_archivo, nombre_tag);
o	Log de éxito.
•	123: t_file_tag_metadata_destroy(meta);
o	Libera la metadata en memoria.
•	124: free(path);
o	Libera el string path.
•	126–127: int resultado_ok = LISTO_OK; send(socket_cliente, &resultado_ok, sizeof(int), 0);
o	Envía al cliente el resultado de éxito.
•	129: list_destroy_and_destroy_elements(valores, free);
o	Libera los elementos recibidos en el paquete.
•	130: log_info(logger, "termine el create");
o	Log final indicando que terminó el CREATE.
•	131: fin de la función.
Comentarios: El handler hace validaciones robustas, evita sobrescribir metadata existente y persiste la metadata inicial. Respeta retardo_operacion y hace logging detallado.
________________________________________
handle_truncate (líneas 134–221)
•	135: declaración handle_truncate(...).
•	136: usleep(retardo_operacion * 1000);
o	Retardo antes de ejecutar.
•	138: t_list* valores = recibir_paquete(socket_cliente);
o	Recepción del paquete esperado.
•	139–144: Validación básica: paquete no NULL y tamaño == 4; si falla, log, limpieza y envía un int 0 indicando error (aquí usan 0 como error simple).
•	146–149: Obtiene nombre_archivo, nombre_tag, nuevo_tamanio y query_id desde la lista.
•	151: log_info indicando intento de TRUNCATE con datos.
•	153–167: Validación de que nuevo_tamanio sea múltiplo de block_size (excepto que 0 se permite). Si no es múltiplo, log de error, cleanup y envía WTF_ERROR.
•	169: char* path_meta = path_metadata(...);
o	Construye la ruta al metadata.
•	170–175: Si no pudo construir path (NULL), enviar error (0), limpiar y retornar.
•	178–184: Si path_meta no existe en FS (access(...) == -1), log error, limpiar y responder ERROR_FILE_INEXISTENTE.
•	186–187: t_file_tag_metadata* meta = t_file_tag_metadata_create(...); t_file_tag_metadata_load(path_meta, meta);
o	Crea la estructura meta y carga desde disco su contenido.
•	190–197: Si el estado es COMMITED, rechaza la operación (no se permiten escrituras/truncados). Log, limpieza y responde ERROR_ESCRITURA_NO_PERMITIDA.
•	199–205: Decide si agrandar o achicar: compara nuevo_tamanio con meta->size y llama a agrandar_file_tag o achicar_file_tag según corresponda; si igual, no hace nada.
o	Estas funciones (no mostradas aquí) encapsulan la lógica de asignación/liberación de bloques.
•	208: meta->size = nuevo_tamanio; // Actualiza el campo de tamaño
•	209: t_file_tag_metadata_save(path_meta, meta); // Persiste metadata actualizada
•	211: log_info de éxito indicando truncado.
•	214–216: limpieza: destruir meta, free path_meta, destruir lista valores.
•	219–220: enviar respuesta LISTO_OK al cliente.
•	221: fin de función.
Comentarios: Maneja validaciones primordiales (multiplo de bloque, existencia, estado COMMITED) y delega la lógica específica de reallocación de bloques a funciones auxiliares.
________________________________________
handle_tag (Clonar File:Tag) (líneas 224–349)
•	225: declaración handle_tag.
•	226: usleep(retardo_operacion * 1000);
•	227: log_info(logger, "TAG");
•	229: t_list* valores = recibir_paquete(socket_cliente);
•	230–237: Si valores == NULL, log error y responder WTF_ERROR.
•	239–246: Valida que se reciban 5 items (src_file, src_tag, dst_file, dst_tag, query_id); si no, error y respuesta WTF_ERROR.
•	248–253: Extrae src_file, src_tag, dst_file, dst_tag y query_id.
•	255: log_info describiendo la operación TAG origen->destino.
•	258–260: Construcción de paths con path_metadata para origen y destino.
•	260–267: Si alguno de los paths es NULL, log error, limpieza y respuesta WTF_ERROR.
•	271–284: Chequeo de existencia del destino: intenta fopen(path_dst,"r"); si existe, cierra y responde ERROR_FILE_PREEXISTENTE (evita pisar).
•	287–297: Carga metadata de origen: crea meta_src y carga su contenido; si falla crear meta_src, responde con error.
•	300: crear_directorio_file_tag(dst_file, dst_tag);
o	Crea los directorios del destino (no rompe si existen).
•	301–311: Crea meta_dst en memoria; si malloc falla, limpieza y error.
•	313–317: Clona campos relevantes: size, state=WIP (se fuerza WORK_IN_PROGRESS), block_count = block_count del source.
•	319–328: Si el origen tiene bloques, aloca memoria en meta_dst->block_numbers y copia la lista; si malloc falla, deja la lista vacía.
•	331: t_file_tag_metadata_save(path_dst, meta_dst);
o	Persiste metadata destino.
•	334–336: sincronizar_hardlinks_tag(dst_file, dst_tag, meta_dst);
o	Esta línea (marcada “CORRECCIÓN CLAVE”) crea los hard links físicos en el nuevo directorio logical_blocks. Es la parte que hace que el tag clonado comparta físicamente los bloques (no duplica bytes).
•	338: log_info de éxito de creación del tag.
•	341–345: limpieza: destruir meta_src, meta_dst, free paths y liberar lista valores recibidos.
•	347–348: enviar LISTO_OK al cliente.
•	349: fin de función.
Comentarios: TAG clona metadata y, muy importante, crea hard links a los bloques físicos (sin copiado) — esto permite compartir bloques y soporta deduplicación/optimización. El handler hace chequeo de no sobrescribir destino.
________________________________________
handle_commit (líneas 351–442)
•	352: declaración handle_commit.
•	353: usleep(retardo_operacion * 1000);
•	354: log_info(logger, "commit time");
•	355: t_list* valores = recibir_paquete(socket_cliente);
•	356–361: Valida que haya 3 elementos; si no, log y responde WTF_ERROR.
•	363–365: Extrae nombre_archivo, nombre_tag y query_id.
•	367: char* path_meta = path_metadata(...);
•	368: t_file_tag_metadata* meta = t_file_tag_metadata_create(...);
•	371: (comentado) pthread_mutex_lock(&mutex_metadata);
o	El código comenta un lock global de metadata; aquí se usa t_file_tag_metadata_load que internamente puede usar mutex_metadata según el comentario posterior.
•	372: t_file_tag_metadata_load(path_meta, meta);
o	Carga metadata desde disco.
•	374–376: Si meta->state == COMMITED, log que ya estaba commited y no hace nada.
•	377–425: Si no estaba commited, realiza la lógica de deduplicación:
o	378–385: para cada bloque lógico del archivo:
	Obtiene nro_bloque_actual.
	Bloquea el mutex del bloque físico correspondiente (pthread_mutex_lock(&mutexes_bloques_fisicos[nro_bloque_actual])) antes de leer/hashear el bloque.
	Calcula hash del bloque con calcular_hash_bloque (usa logger).
	Desbloquea el mutex del bloque.
o	386–387: si no se obtuvo hash, saltea.
o	389: buscar_nro_bloque_por_hash(hash_actual, logger) — busca si ya existe un bloque con ese hash en índice (usa mutex_hash_index internamente).
o	391–419: si encontró un bloque existente distinto (nro_bloque_existente != -1 y distinto de actual):
	Log de deduplicación.
	Construye path_logico (archivo logical block del file), path_fisico_nuevo (bloque que ya existe), path_fisico_viejo (bloque actual).
	Realiza una “transacción” simple: unlink(path_logico) y link(path_fisico_nuevo, path_logico) — así el logical block ahora apunta al bloque físico existente.
	Actualiza meta->block_numbers[i] = nro_bloque_existente.
	Si el viejo bloque físico quedó sin links (obtener_link_count(path_fisico_viejo) <= 1), libera el bloque físico (liberar_bloque_fisico, que internamente ajusta bitmap con mutex_bitmap) y remueve su entrada del índice de hashes (con mutex_hash_index).
	Libera los strings temporales.
o	421–423: else if (nro_bloque_existente == -1) → no existe hash en índice: agregar_hash_a_indice(hash_actual, nro_bloque_actual, logger).
o	424: free(hash_actual);
•	426: fin del loop de deduplicación.
•	428: meta->state = COMMITED; // marca como confirmado
•	430: t_file_tag_metadata_save(path_meta, meta); // persiste cambios (se comenta que mutex_metadata es externo)
•	431: log_info indicando Commit exitoso.
•	434: (comentado) pthread_mutex_unlock(&mutex_metadata);
•	436–438: limpieza: destruir meta, free path_meta y destruir lista valores.
•	440–441: enviar LISTO_OK al cliente.
•	442: fin de función.
Comentarios: Commit hace deduplicación por contenido (hash) bajo protección de mutexes de bloque y mutex de índice de hashes. Actualiza metadata y libera bloques físicos no referenciados. Es la operación clave para consolidar writes y ahorrar espacio.
________________________________________
handle_pagina_bloque / READ_BLOCK (líneas 445–541)
•	446: void handle_pagina_bloque(int socket_cliente, t_log* logger) {
•	447: usleep(retardo_operacion * 1000); // Retardo general
•	449: t_list* valores = recibir_paquete(socket_cliente);
•	450: op_code error_code_respuesta = WTF_ERROR; // variable local (no siempre usada)
•	452–458: Validación del paquete: debe existir y tener 4 elementos; si no, log y envía WTF_ERROR.
•	460–463: Extrae query_id, nombre_archivo, nombre_tag y nro_bloque_logico.
•	465: log_info indicando READ_BLOCK solicitado.
•	467: char* path_meta = path_metadata(nombre_archivo, nombre_tag);
•	469–476: Si path_meta no existe (access(...) == -1), envía ERROR_FILE_INEXISTENTE y limpia.
•	478–480: Crea meta en memoria y carga metadata desde path_meta; luego libera path_meta.
•	482–488: Si nro_bloque_logico fuera de rango ( <0 o >= block_count), error ERROR_FUERA_DE_LIMITE y retorna.
•	491: int nro_bloque_fisico = meta->block_numbers[nro_bloque_logico];
•	492: t_file_tag_metadata_destroy(meta);
o	Ya obtuvo el número de bloque físico; libera metadata.
•	495: pthread_mutex_lock(&mutexes_bloques_fisicos[nro_bloque_fisico]);
o	Bloquea mutex específico del bloque físico para sincronizar acceso concurrente.
•	498–500: Construye path_bloque_fisico (ruta al archivo físico block%04d.dat).
•	501: usleep(retardo_aceso_bloque * 1000); // retardo de acceso a bloque
•	503–510: fopen(path_bloque_fisico, "rb"); si fopen falla, log error, limpiar y enviar WTF_ERROR (nota: falta unlock en este caso — ver abajo).
o	IMPORTANTE: cuando fopen falla, el mutex todavía está bloqueado: el código actual NO hace pthread_mutex_unlock antes de responder en el error; eso puede provocar deadlock si no se desbloquea. (Es un punto crítico a revisar).
•	512: char* buffer_bloque = malloc(block_size);
•	513: size_t bytes_leidos = fread(buffer_bloque, 1, block_size, fp);
•	514: fclose(fp);
•	516: pthread_mutex_unlock(&mutexes_bloques_fisicos[nro_bloque_fisico]);
o	Desbloquea el mutex del bloque físico después de la lectura.
•	518–525: Si bytes_leidos != block_size (lectura incompleta), log error, free(buffer) y envia WTF_ERROR.
•	527: log_info indicando lectura exitosa.
•	530–533: Arma un paquete con código LISTO_OK y agrega el buffer de tamaño block_size al paquete para enviarlo al worker.
•	535–537: enviar_paquete(paquete_respuesta, socket_cliente); si falla, log de error (no cambia respuesta).
•	539: free(buffer_bloque);
•	540: list_destroy_and_destroy_elements(valores, free);
•	541: fin de función.
Comentarios y observaciones críticas:
•	El bloqueo por mutex por bloque físico es correcto para serializar accessos concurrentes.
•	Hay un bug potencial: si fopen falla (líneas 503–510), no se desbloquea pthread_mutex_unlock antes de retornar — eso puede dejar el mutex permanentemente bloqueado. Hay que desbloquearlo antes de send/error return.
•	El handler envía el contenido del bloque binario completo (block_size) al cliente.
________________________________________
handle_write_block (líneas 544–634)
•	544: void handle_write_block(...)
•	545: usleep(retardo_operacion * 1000);
•	547: int resultado = WTF_ERROR; // valor por defecto a devolver
•	548–549: t_file_tag_metadata* meta = NULL; char* path_meta = NULL; // inicializaciones
•	551: t_list* valores = recibir_paquete(socket_cliente);
•	552: comentario: se espera file, tag, nro_bloque_logico, contenido, query_id
•	553–557: si paquete inválido -> cleanup y enviar resultado (WTF_ERROR) y return.
•	559–563: extrae nombre_archivo, nombre_tag, nro_bloque_logico, contenido (puntero), query_id.
•	565: path_meta = path_metadata(...); // construye path metadata
•	566–571: si path_meta es NULL -> log error, limpiar, enviar error y return.
•	574–580: si path_meta no existe en disco -> enviar ERROR_FILE_INEXISTENTE y return.
•	584–586: crea meta y carga metadata desde disco.
•	588–596: valida que meta->state != COMMITED; si está COMMITED, respuesta ERROR_ESCRITURA_NO_PERMITIDA.
•	599–607: valida índice de bloque lógico: si fuera de rango, responde ERROR_FUERA_DE_LIMITE.
•	610: int rc = write_block_file_tag(nombre_archivo, nombre_tag, nro_bloque_logico, block_size, contenido, meta, query_id);
o	Llama a una función auxiliar que ejecuta la escritura: puede ser escritura directa si el bloque ya es exclusivo o Copy-on-Write si el bloque está deduplicado/shared. Esa función se encarga del locking por bloque, del manejo del bitmap, de crear/actualizar hardlinks y de loguear el éxito.
•	612–627: interpreta el rc devuelto por write_block_file_tag:
o	rc == 0 → éxito: guarda metadata (si write creó o cambió algo), resultado = LISTO_OK.
o	rc == ERROR_ESPACIO_INSUFICIENTE → resultado específico.
o	rc == ERROR_FUERA_DE_LIMITE → resultado específico.
o	rc == ERROR_ESCRITURA_NO_PERMITIDA → resultado específico.
o	cualquier otro rc → resultado WTF_ERROR y log de error genérico.
•	629–631: limpieza: destruir meta, free(path_meta), destruir lista valores.
•	633: send(socket_cliente, &resultado, sizeof(int), 0);
o	Envía el resultado al cliente (códigos definidos más arriba).
•	634: fin de función.
Comentarios:
•	La lógica compleja del write (CoW, asignación de nuevo bloque físico, actualización de hardlinks, hashing, etc.) está delegada a write_block_file_tag; este handler valida y maneja el flujo general y la persistencia de metadata si hubo cambios.
•	Buen manejo de errores y códigos específicos.
________________________________________
handle_delete (líneas 637–861)
•	638: void handle_delete(...)
•	639: usleep(retardo_operacion * 1000);
•	640: log_info(logger, "DELETE");
•	642: t_list* args = recibir_paquete(socket_cliente);
•	643–649: Validación paquete; si inválido, log y enviar WTF_ERROR.
•	652–654: extrae nombre_archivo, nombre_tag y query_id.
•	656: log_info indicando operación DELETE.
•	658–665: Protege un tag especial initial_file:BASE — si se intenta borrar, rechaza con WTF_ERROR.
•	667–669: inicializa dir_tag y path_meta a NULL.
•	670: dir_tag = string_from_format("%s/files/%s/%s", punto_montaje, nombre_archivo, nombre_tag);
o	Path al directorio del tag.
•	671–676: si dir_tag NULL (sin memoria), error y return.
•	679: path_meta = path_metadata(...);
•	680–686: si path_meta NULL, error y return.
•	689–697: si path_meta no existe en disco, responde ERROR_FILE_INEXISTENTE.
•	699–707: crea meta en memoria y valida malloc; si falla, error y return.
•	712: t_file_tag_metadata_load(path_meta, meta);
o	Carga metadata. Comentario: usa mutex_metadata internamente.
•	715–723: prepara copia local de la lista de bloques (blocks_copy). Bloquea mutex_metadata antes de acceder a los datos y eliminar la metadata en disco:
o	718: pthread_mutex_lock(&mutex_metadata);
o	719–723: si blocks_count>0, aloca blocks_copy y memcpy; si malloc falla, desbloquea mutex, limpia y retorna error.
•	736–740: intenta remove(path_meta): elimina metadata en disco (si falla, hace warning pero continúa).
•	741: pthread_mutex_unlock(&mutex_metadata);
•	744: t_file_tag_metadata_destroy(meta);
o	libera estructura meta porque ya tiene copia blocks_copy con los bloques.
•	747–792: si blocks_copy existe y blocks_count>0, iterar por cada bloque lógico y:
o	749: int nro_bloque_fisico = blocks_copy[i];
o	751–753: construye path_bloque_logico y path_bloque_fisico.
o	754–759: chequeo de memoria para strings temporales.
o	762–786: si unlink(path_bloque_logico) == 0 (hard link lógico eliminado):
	log_info indicando eliminación.
	if (obtener_link_count(path_bloque_fisico) <= 1) { liberar_bloque_fisico(fs, nro_bloque_fisico); actualizar índice de hashes (blocks_hash_index_remove_entry_for_block) } else log_debug que todavía referenciado.
o	Si unlink falla, log warning.
o	libera strings temporales.
•	791: free(blocks_copy);
•	795–820: intenta eliminar dir_logical (dir_tag/logical_blocks) de forma recursiva si quedan archivos residuales:
o	abre directorio, itera sobre entradas y unlink cada archivo; luego closedir y rmdir el dir_logical.
o	logs y manejo de errores si no puede eliminar.
•	822–851: intenta rmdir(dir_tag) (el directorio del tag); si falla, intenta limpiar recursivamente su contenido (abre d2 y elimina contenidos) y vuelve a rmdir; logs de advertencia si no pudo eliminar.
•	854–857: limpia dir_tag, path_meta y lista args.
•	859–860: envía LISTO_OK al cliente.
•	861: fin de función.
Comentarios:
•	La función borra metadata, elimina hard links lógicos y libera bloques físicos cuando ya no tienen más referencias. Actualiza índice de hashes al liberar bloques.
•	Protege con mutex_metadata al leer y hacer visible la eliminación de metadata.
•	Realiza limpieza recursiva para evitar dejar residuos en disco; maneja errores con logs.
•	Buena robustez general.
________________________________________
handle_handshake_storage (líneas 863–881)
•	864: void handle_handshake_storage(int socket_cliente, t_log* logger) {
•	865: (usleep comentado) // no aplica el retardo o quedó comentado.
•	867: log_info(logger, "Recibí HANDSHAKE_STORAGE de un Worker");
•	868: char* id = recibir_nombre(socket_cliente, logger);
o	Recibe un nombre/ID enviado por el worker (string).
•	870: agregar_worker(id, socket_cliente);
o	Registra el worker en diccionario_workers (función definida en otro lugar).
•	872–881: Prepara un paquete de respuesta:
o	872: t_paquete* paquete = malloc(sizeof(t_paquete));
o	873: paquete->codigo_operacion = HANDSHAKE_WORKER; // código para handshake
o	874: crear_buffer(paquete);
o	876: cargar_int_buffer(paquete->buffer, block_size); // envía block_size al worker
o	878: enviar_paquete(paquete, socket_cliente);
o	880: eliminar_paquete(paquete);
•	881: fin de función.
Comentarios: El handshake sirve para registrar workers y enviarles configuración inicial (block_size). agregar_worker debe gestionar el diccionario y posiblemente inicializar estructuras por worker.
________________________________________
Observaciones generales y puntos críticos detectados
•	Bloqueos y concurrencia:
o	El código usa mutex_metadata, mutex_hash_index, mutex_bitmap y mutexes_bloques_fisicos[] para sincronización. En general están bien aplicados (bloque por bloque para accesos físicos, mutex global para metadata/índice).
o	Sin embargo, detecté un posible bug en handle_pagina_bloque: si fopen falla tras haber tomado el mutex del bloque físico (línea ~503), el código no hace pthread_mutex_unlock antes de retornar con error. Eso puede producir deadlocks posteriores en accesos a ese bloque. Recomendación: asegurarse de desbloquear antes de cualquier return/error tras bloquear un mutex.
•	Manejo de errores:
o	Los handlers suelen responder con códigos específicos (LISTO_OK, ERROR_FILE_INEXISTENTE, ERROR_FUERA_DE_LIMITE, ERROR_ESCRITURA_NO_PERMITIDA, ERROR_ESPACIO_INSUFICIENTE, WTF_ERROR...). Ese mapeo permite al cliente interpretar la causa.
•	Delegación de responsabilidad:
o	Las operaciones complejas (asignación/liberación de bloques, CoW, hashing, índice de hashes, sync hardlinks) están delegadas a funciones auxiliares (agregar_hash_a_indice, write_block_file_tag, liberar_bloque_fisico, calcular_hash_bloque, etc.). El handler valida, orquesta y persiste metadata.
•	Logs: El código registra mucha información útil (info, error, warning, debug) lo cual es muy valioso para depuración.


1.	handle_create(int socket_cliente, t_log* logger)
•	Recibe:
o	Parámetros formales: socket_cliente, logger.
o	Por la red (recibir_paquete): una lista con 3 items:
a.	nombre_archivo (char*)
b.	nombre_tag (char*)
c.	query_id (int*)
•	Qué hace y cómo (paso a paso):
i.	Simula latencia: usleep(retardo_operacion * 1000).
ii.	Llama recibir_paquete(socket_cliente) y valida que la lista exista y tenga exactamente 3 elementos. Si falla, envía error.
iii.	Llama crear_directorio_file_tag(nombre_archivo, nombre_tag) para asegurarse de que la jerarquía de directorios exista (no falla si ya existe).
iv.	Construye path a metadata: path_metadata(nombre_archivo, nombre_tag). Si falla la asignación, responde error.
v.	Comprueba si el archivo metadata ya existe con fopen(path, "r"). Si existe responde ERROR_FILE_PREEXISTENTE para no pisar.
vi.	Crea en memoria metadata inicial: t_file_tag_metadata_create(nombre_archivo, nombre_tag).
vii.	Guarda la metadata en disco con t_file_tag_metadata_save(path, meta).
viii.	Valida con commons/config (config_create / config_has_property) que el metadata.cfg tenga las claves TAMAÑO, ESTADO y BLOCKS; si algo falta, limpia y responde error.
ix.	Envía LISTO_OK por socket y libera memoria/listas.
•	Qué devuelve / qué responde al caller:
o	En éxito: envía un int LISTO_OK (send(socket, &resultado_ok, sizeof(int), 0)).
o	En fallos: envía códigos de error (WTF_ERROR o ERROR_FILE_PREEXISTENTE).
•	Qué manda o llama a otros módulos / efectos colaterales:
o	crear_directorio_file_tag (módulo de FS/paths).
o	path_metadata (generación de path).
o	fopen/remove I/O de libc.
o	t_file_tag_metadata_create / t_file_tag_metadata_save / t_file_tag_metadata_destroy (módulo de metadata).
o	commons/config (config_create/config_has_property) para validar el archivo guardado.
o	recibir_paquete / enviar_paquete (protocolo de red).
o	Logger: log_info/log_error.
•	Observaciones de sincronización:
o	No usa mutex explícito aquí; asume que la creación de metadata es segura en el contexto (o manejada por llamadas internas).
2.	handle_truncate(int socket_cliente, t_log* logger)
•	Recibe:
o	Formal: socket_cliente, logger.
o	Por la red: lista con 4 items:
a.	nombre_archivo (char*)
b.	nombre_tag (char*)
c.	nuevo_tamanio (int*)
d.	query_id (int*)
•	Qué hace y cómo:
i.	usleep(retardo_operacion * 1000)
ii.	Valida paquete y tamaño (4 items). Si inválido, responde error.
iii.	Comprueba que nuevo_tamanio sea múltiplo de block_size (acepta 0). Si no lo es, responde WTF_ERROR.
iv.	path_meta = path_metadata(...). Verifica existencia con access(path_meta, F_OK). Si no existe responde ERROR_FILE_INEXISTENTE.
v.	Crea y carga metadata en memoria: t_file_tag_metadata_create + t_file_tag_metadata_load.
vi.	Si meta->state == COMMITED, responde ERROR_ESCRITURA_NO_PERMITIDA (no se permite truncar confirmado).
vii.	Decide:
	Si nuevo_tamanio > meta->size → llama agrandar_file_tag(meta, nuevo_tamanio, query_id, logger).
	Si nuevo_tamanio < meta->size → llama achicar_file_tag(meta, nuevo_tamanio, query_id, logger).
	Si igual, no hace nada. (Estas funciones manejan la asignación/liberación de bloques, creación/eliminación de hardlinks y actualización de block_numbers.)
viii.	Actualiza meta->size = nuevo_tamanio y guarda con t_file_tag_metadata_save(path_meta, meta).
ix.	Envía LISTO_OK y libera recursos.
•	Qué devuelve:
o	En éxito: LISTO_OK.
o	En fallos: WTF_ERROR, ERROR_FILE_INEXISTENTE o ERROR_ESCRITURA_NO_PERMITIDA (según caso).
•	Qué manda/llama a otros módulos:
o	path_metadata, t_file_tag_metadata_create/load/save/destroy (módulo metadata).
o	agrandar_file_tag / achicar_file_tag (módulo que implementa expansión/contracción de File:Tag).
o	recibir_paquete / enviar_paquete.
o	Logger.
•	Sincronización:
o	No se ve lock global en este handler (el código tiene comentado un lock en otros puntos). Se asume que agrandar/achicar realizan locking necesario (p. ej. mutex_bitmap, mutexes_bloques_fisicos, mutex_metadata) internamente.
3.	handle_tag(int socket_cliente, t_log* logger)
•	Recibe:
o	Formal: socket_cliente, logger.
o	Por la red: lista con 5 items:
a.	src_file (char*)
b.	src_tag (char*)
c.	dst_file (char*)
d.	dst_tag (char*)
e.	query_id (int*)
•	Qué hace y cómo:
i.	usleep(retardo_operacion * 1000)
ii.	Valida paquete (5 items).
iii.	path_src = path_metadata(src_file, src_tag); path_dst = path_metadata(dst_file, dst_tag).
iv.	Si path_dst ya existe (fopen en modo "r") → responde ERROR_FILE_PREEXISTENTE.
v.	Carga metadata origen: meta_src = t_file_tag_metadata_create + t_file_tag_metadata_load.
vi.	Crea directorio destino: crear_directorio_file_tag(dst_file, dst_tag).
vii.	Crea meta_dst en memoria (t_file_tag_metadata_create). Copia campos:
	size = meta_src->size
	state = WORK_IN_PROGRESS (WIP)
	block_count = meta_src->block_count
	block_numbers: intenta malloc y memcpy; si falla deja block_count=0 y block_numbers=NULL.
viii.	Guarda metadata destino en disco: t_file_tag_metadata_save(path_dst, meta_dst).
ix.	Llama sincronizar_hardlinks_tag(dst_file, dst_tag, meta_dst) — función clave que crea los hard links físicos dentro del directorio logical_blocks del destino apuntando a los mismos physical_blocks que usa el origen. Esto es lo que hace que el tag destino comparta datos sin copiar.
10.	Envía LISTO_OK.
•	Qué devuelve:
o	LISTO_OK en éxito.
o	ERROR_FILE_PREEXISTENTE o WTF_ERROR en fallos.
•	Qué manda/llama a otros módulos:
o	path_metadata, crear_directorio_file_tag.
o	t_file_tag_metadata_create/load/save/destroy.
o	sincronizar_hardlinks_tag (módulo que crea hard links lógicos apuntando a physical_blocks).
o	recibir_paquete / enviar_paquete.
•	Sincronización:
o	El proceso de crear hardlinks debe ser consistente; la función sincronizar_hardlinks_tag realiza la creación de links físicos. Si otra operación modifica los bloques simultáneamente, la función llamada debería usar mutexes por bloque físico cuando sea necesario.
4.	handle_commit(int socket_cliente, t_log* logger)
•	Recibe:
o	Formal: socket_cliente, logger.
o	Por la red: lista con 3 items:
a.	nombre_archivo (char*)
b.	nombre_tag (char*)
c.	query_id (int*)
•	Qué hace y cómo (detallado — deduplicación):
i.	usleep(retardo_operacion * 1000)
ii.	recibir_paquete y validación.
iii.	path_meta = path_metadata(...); meta = t_file_tag_metadata_create(...); t_file_tag_metadata_load(path_meta, meta).
iv.	Si meta->state == COMMITED → no hace deduplicación, sólo informa.
v.	Si no, para cada bloque lógico i (0..meta->block_count-1): a. nro_bloque_actual = meta->block_numbers[i]. b. pthread_mutex_lock(&mutexes_bloques_fisicos[nro_bloque_actual]) — protege lectura/hasheo del bloque físico. c. hash_actual = calcular_hash_bloque(nro_bloque_actual, logger). d. pthread_mutex_unlock(&mutexes_bloques_fisicos[nro_bloque_actual]). e. buscar_nro_bloque_por_hash(hash_actual, logger): busca en el índice de hashes otro bloque con el mismo contenido. (La función indicada ya usa mutex_hash_index internamente, según comentario.) f. Si se encontró un bloque distinto (deduplicación):
	Construye path_logico (logical_blocks/%06u.dat).
	Construye path_fisico_nuevo (physical_blocks/block%04d.dat) y path_fisico_viejo.
	Hace unlink(path_logico) y link(path_fisico_nuevo, path_logico) — reasigna el hard link lógico al bloque físico existente.
	Actualiza meta->block_numbers[i] = nro_bloque_existente.
	Si obtener_link_count(path_fisico_viejo) <= 1 (ya no referenciado) → liberar_bloque_fisico(fs, nro_bloque_actual) (libera bit en bitmap; esta función hace locking sobre mutex_bitmap internamente) y blocks_hash_index_remove_entry_for_block(punto_montaje, nro_bloque_actual, logger) protegida con mutex_hash_index. g. Si no se encontró (nro_bloque_existente == -1) → agregar_hash_a_indice(hash_actual, nro_bloque_actual, logger) (inserta en índice de hashes). h. free(hash_actual).
vi.	Después de procesar todos los bloques: meta->state = COMMITED y t_file_tag_metadata_save(path_meta, meta).
vii.	Envía LISTO_OK y limpia.
•	Qué devuelve:
o	LISTO_OK en éxito; en fallos de recepción/interna devuelve WTF_ERROR (en el flujo mostrado no se envían otros códigos en commit).
•	Qué manda/llama a otros módulos:
o	t_file_tag_metadata_create/load/save/destroy.
o	calcular_hash_bloque (módulo de hashing/IO).
o	buscar_nro_bloque_por_hash, agregar_hash_a_indice, blocks_hash_index_remove_entry_for_block (módulo índice de hashes).
o	unlink/link (syscalls de libc para hard links).
o	liberar_bloque_fisico (módulo de gestión de bloques y bitmap).
o	obtener_link_count (consulta de número de enlaces a archivo).
o	recibir_paquete / enviar_paquete.
•	Sincronización:
o	Usa pthread_mutex_lock/unlock sobre mutexes_bloques_fisicos[n] para proteger lectura/hasheo de cada physical block.
o	Usa pthread_mutex_lock(&mutex_hash_index) al eliminar entradas del índice; las funciones de búsqueda/añadir aparentemente manejan sus propios locks.
o	Hay un comentario de que se podría (o debería) bloquear mutex_metadata para toda la operación, pero el lock global está comentado en el código — por tanto la operación es parcialmente protegida a nivel de bloque/índice, no completamente serializada a nivel metadata.
5.	handle_pagina_bloque(int socket_cliente, t_log* logger) (READ_BLOCK)
•	Recibe:
o	Formal: socket_cliente, logger.
o	Por la red: lista con 4 items:
a.	query_id (int*)
b.	nombre_archivo (char*)
c.	nombre_tag (char*)
d.	nro_bloque_logico (int*)
•	Qué hace y cómo:
i.	usleep(retardo_operacion * 1000).
ii.	recibir_paquete y validación (4 items).
iii.	path_meta = path_metadata(...) y valida existencia con access(). Si no existe → ERROR_FILE_INEXISTENTE.
iv.	Carga metadata: t_file_tag_metadata_create + t_file_tag_metadata_load.
v.	Comprueba que nro_bloque_logico esté en rango [0, meta->block_count). Si no → ERROR_FUERA_DE_LIMITE.
vi.	Determina nro_bloque_fisico = meta->block_numbers[nro_bloque_logico] y destruye meta.
vii.	pthread_mutex_lock(&mutexes_bloques_fisicos[nro_bloque_fisico]) — bloqueo por bloque físico.
viii.	Construye path al fichero físico: "%s/physical_blocks/block%04d.dat".
ix.	usleep(retardo_aceso_bloque * 1000) — retardo de acceso a bloque.
10.	fopen(path_bloque_fisico, "rb"), malloc(block_size) y fread(block_size) bytes. Si fopen/fread falla → error y respuesta WTF_ERROR.
11.	fclose, pthread_mutex_unlock(&mutexes_bloques_fisicos[nro_bloque_fisico]).
12.	Construye t_paquete con codigo LISTO_OK, agrega buffer con crear_buffer/agregar_a_paquete y enviar_paquete(socket_cliente).
13.	Libera buffer y lista recibida.
•	Qué devuelve:
o	Envía paquete con codigo_operacion = LISTO_OK y buffer con block_size bytes (el contenido del bloque) mediante enviar_paquete.
o	En errores envía int con código (ERROR_FILE_INEXISTENTE, ERROR_FUERA_DE_LIMITE o WTF_ERROR).
•	Qué manda/llama a otros módulos:
o	path_metadata, t_file_tag_metadata_create/load/destroy.
o	pthread_mutexes (mutexes_bloques_fisicos).
o	I/O: fopen/fread/fclose.
o	crear_buffer/agregar_a_paquete/enviar_paquete (módulo protocolo).
o	recibir_paquete / enviar_paquete.
•	Sincronización:
o	Usa mutex por bloque físico para garantizar que la lectura se haga de forma consistente frente a escrituras/CoW.
6.	handle_write_block(int socket_cliente, t_log* logger)
•	Recibe:
o	Formal: socket_cliente, logger.
o	Por la red: lista con 5 items:
a.	nombre_archivo (char*)
b.	nombre_tag (char*)
c.	nro_bloque_logico (int*)
d.	contenido (char*, con tamaño block_size)
e.	query_id (int*)
•	Qué hace y cómo:
i.	usleep(retardo_operacion * 1000)
ii.	recibir_paquete y validación (5 items); si no válido envía WTF_ERROR.
iii.	path_meta = path_metadata(...). Si falta, responde error.
iv.	Verifica existencia con access(path_meta, F_OK). Si no existe responde ERROR_FILE_INEXISTENTE.
v.	Crea y carga metadata: meta = t_file_tag_metadata_create + t_file_tag_metadata_load.
vi.	Si meta->state == COMMITED → ERROR_ESCRITURA_NO_PERMITIDA.
vii.	Verifica que nro_bloque_logico esté en rango; si no → ERROR_FUERA_DE_LIMITE.
viii.	Llama write_block_file_tag(nombre_archivo, nombre_tag, nro_bloque_logico, block_size, contenido, meta, query_id).
	Esta llamada es la que realiza el trabajo real: puede escribir directamente en el physical block si está exclusivo, o hacer Copy-On-Write (crear nuevo physical block, escribir ahí, actualizar meta->block_numbers y crear hard link lógico). También debe encargarse de asignación de nuevo bloque físico (bitmap), actualización del índice de hashes, bloqueo por bloque físico, etc.
ix.	Interpreta el código rc devuelto por write_block_file_tag:
	rc == 0 → guarda metadata (t_file_tag_metadata_save) y responde LISTO_OK.
	rc == ERROR_ESPACIO_INSUFICIENTE → responde ese código.
	rc == ERROR_FUERA_DE_LIMITE / ERROR_ESCRITURA_NO_PERMITIDA → responde el respectivo código.
	si rc otro → responde WTF_ERROR.
10.	Libera meta/path_meta/lista.
•	Qué devuelve:
o	Envía un int por send con el código resultante (LISTO_OK o un ERROR_* o WTF_ERROR).
•	Qué manda/llama a otros módulos:
o	path_metadata, t_file_tag_metadata_create/load/save/destroy.
o	write_block_file_tag (módulo que implementa la lógica de escritura y CoW).
o	recibir_paquete / enviar_paquete.
o	Logger.
•	Sincronización:
o	El handler confía en que write_block_file_tag haga lock de mutexes pertinentes (mutexes_bloques_fisicos, mutex_bitmap, mutex_hash_index, etc.) ya que la operación de escribir implica modificar physical blocks, bitmap e índice de hashes.
7.	handle_delete(int socket_cliente, t_log* logger)
•	Recibe:
o	Formal: socket_cliente, logger.
o	Por la red: lista con al menos 3 items:
a.	nombre_archivo (char*)
b.	nombre_tag (char*)
c.	query_id (int*)
•	Qué hace y cómo:
i.	usleep(retardo_operacion * 1000)
ii.	recibir_paquete y validación (>=3).
iii.	Rechaza explícitamente si intento borrar initial_file:BASE (protegido).
iv.	Construye dir_tag = "%s/files/%s/%s" y path_meta = path_metadata(...).
v.	Si path_meta no existe → ERROR_FILE_INEXISTENTE.
vi.	meta = t_file_tag_metadata_create + t_file_tag_metadata_load(path_meta, meta).
vii.	Bajo pthread_mutex_lock(&mutex_metadata):
	blocks_count = meta->block_count.
	Si blocks_count > 0, malloc blocks_copy y memcpy(meta->block_numbers, ...) para procesarlos fuera del lock.
	Intenta remove(path_meta) (elimina metadata del disco); si falla sólo loggea y continúa.
	pthread_mutex_unlock(&mutex_metadata).
viii.	t_file_tag_metadata_destroy(meta).
ix.	Para cada bloque en blocks_copy (i = 0..blocks_count-1):
	nro_bloque_fisico = blocks_copy[i].
	path_bloque_logico = "%s/logical_blocks/%06u.dat" (en dir_tag).
	path_bloque_fisico = "%s/physical_blocks/block%04d.dat".
	unlink(path_bloque_logico): si tiene éxito → log; si falla → warning.
	Si unlink tuvo éxito y obtener_link_count(path_bloque_fisico) <= 1: • liberar_bloque_fisico(fs, nro_bloque_fisico) (hace locking internamente sobre mutex_bitmap). • pthread_mutex_lock(&mutex_hash_index); blocks_hash_index_remove_entry_for_block(punto_montaje, nro_bloque_fisico, logger); pthread_mutex_unlock(&mutex_hash_index).
	Free paths.
10.	Free blocks_copy.
11.	Intenta eliminar el subdirectorio logical_blocks (abre dir, borra residuales con unlink, rmdir).
12.	Intenta rmdir(dir_tag) y, si falla, intenta limpiar recursivamente contenido y volver a rmdir; si finalmente no puede, loggea warning.
13.	Envía LISTO_OK.
•	Qué devuelve:
o	LISTO_OK en éxito.
o	ERROR_FILE_INEXISTENTE, WTF_ERROR en fallos.
•	Qué manda/llama a otros módulos:
o	t_file_tag_metadata_create/load/destroy.
o	liberar_bloque_fisico (módulo de gestión de bloques/bitmap).
o	blocks_hash_index_remove_entry_for_block (módulo índice de hashes).
o	obtener_link_count (consulta del filesystem).
o	I/O dir ops: opendir/readdir/unlink/rmdir.
o	recibir_paquete / enviar_paquete.
•	Sincronización:
o	Copia block_numbers bajo mutex_metadata para evitar races con otra modificación de metadata.
o	liberar_bloque_fisico hace locking sobre mutex_bitmap; actualización del índice usa mutex_hash_index.
o	El borrado de hardlinks lógicos se hace fuera del lock global.
8.	handle_handshake_storage(int socket_cliente, t_log* logger)
•	Recibe:
o	Formal: socket_cliente, logger.
o	Por la red: primero espera un nombre/id (recibir_nombre(socket_cliente, logger)), que identifica al Worker.
•	Qué hace y cómo:
i.	log_info de handshake recibido.
ii.	id = recibir_nombre(socket_cliente, logger).
iii.	agregar_worker(id, socket_cliente) — registra el worker en la estructura interna de storage (tabla de workers conectados).
iv.	Construye un t_paquete con codigo_operacion = HANDSHAKE_WORKER.
v.	crear_buffer(paquete) y cargar_int_buffer(paquete->buffer, block_size) — empaqueta el block_size del storage para que el worker lo conozca.
vi.	enviar_paquete(paquete, socket_cliente) y eliminar_paquete(paquete).
•	Qué devuelve:
o	Envía un paquete con codigo_operacion HANDSHAKE_WORKER y en su buffer envía el block_size (int).
•	Qué manda/llama a otros módulos:
o	recibir_nombre, agregar_worker (módulo de gestión de conexiones/workers).
o	crear_buffer/cargar_int_buffer/enviar_paquete/eliminar_paquete (módulo de protocolo).
•	Sincronización:
o	Registro de worker debería manejar concurrencia interna en agregar_worker; el handler no maneja locks explícitos aquí.



1.	Firma (header) int write_block_file_tag(char* file, char* tag, int bloque_idx, int tamanio_escritura, char* buffer, t_file_tag_metadata* meta, int query_id);
Qué recibe
•	file (char*): nombre del archivo lógico.
•	tag (char*): nombre del tag del file.
•	bloque_idx (int): índice del bloque lógico dentro del file (0-based).
•	tamanio_escritura (int): tamaño de los datos a escribir (se espera block_size).
•	buffer (char*): puntero a los datos a escribir (block_size bytes).
•	meta (t_file_tag_metadata*): metadata del file:tag ya cargada en memoria (contiene block_numbers, block_count, state, size).
•	query_id (int): id de la request para logs.
Qué hace (pasos exactos observables en la implementación)
•	Validaciones iniciales (líneas visibles):
o	Si alguno de file, tag, meta o buffer es NULL → retorna WTF_ERROR.
o	Si tamanio_escritura != block_size → log_error y retorna WTF_ERROR.
•	Bloqueo y validaciones bajo mutex_metadata:
o	pthread_mutex_lock(&mutex_metadata);
o	Verifica que bloque_idx esté dentro del rango: si fuera de rango libera mutex y retorna ERROR_FUERA_DE_LIMITE.
o	Verifica estado de meta: si meta->state == COMMITED libera mutex y retorna ERROR_ESCRITURA_NO_PERMITIDA.
•	Obtiene nro_bloque_fisico_actual = meta->block_numbers[bloque_idx] y construye path_fisico_actual = "%s/physical_blocks/block%04d.dat" (con punto_montaje).
•	(El código continúa después de la porción vista; a partir de cómo se usa en handlers y por convención del proyecto, los siguientes comportamientos son los que se esperan e implementan en la misma función):
o	Determina el conteo de enlaces (link count) del physical block actual para decidir si puede escribir "in-place" o si debe hacer CoW (copy-on-write).
o	Si el bloque físico es exclusivo (link_count <= 1):
	Abrir el archivo físico (r+b o mmap) y escribir el buffer directamente.
	Actualizar índice de hashes para ese bloque (remover/actualizar entrada y/o agregar el nuevo hash).
	Hacer fsync/ensure persistence según política.
o	Si el bloque está compartido (link_count > 1) → realizar CoW:
	Reservar un nuevo bloque físico libre en el bitmap (bajo mutex_bitmap).
	Crear el archivo physical_blocks/blockNNNN.dat nuevo y escribir ahí el buffer.
	Reemplazar el hard link lógico: unlink(path_logico) y link(path_fisico_nuevo, path_logico).
	Actualizar meta->block_numbers[bloque_idx] = nro_bloque_nuevo.
	Agregar hash del nuevo bloque al índice (agregar_hash_a_indice).
	Comprobar si el bloque antiguo quedó sin referencias; si quedó sin refs llamar a liberar_bloque_fisico para liberarlo y eliminar su entrada en el índice (blocks_hash_index_remove_entry_for_block).
o	Manejo de errores: si no hay bloques libres, devolver ERROR_ESPACIO_INSUFICIENTE; otros fallos devuelven WTF_ERROR o códigos adecuados.
•	Al finalizar la función (antes de retornar), se libera mutex_metadata o los mutexes usados.
Qué devuelve
•	0 → éxito (escritura realizada / metadata actualizada en memoria).
•	ERROR_ESPACIO_INSUFICIENTE → si no hay bloques disponibles para CoW.
•	ERROR_FUERA_DE_LIMITE → índice inválido.
•	ERROR_ESCRITURA_NO_PERMITIDA → si meta->state == COMMITED.
•	WTF_ERROR → error genérico (argumentos nulos, tamaño incorrecto u otros fallos).
Qué llama / a qué módulos les manda datos (efectos colaterales)
•	mutex_metadata (bloqueo global de metadata al inicio).
•	mutexes_bloques_fisicos[n] (potencialmente) para proteger lectura/escritura de cada bloque físico.
•	mutex_bitmap y funciones de bitmap/allocator para reservar bloques libres.
•	syscalls de I/O: fopen/fwrite/fsync/mmap/munmap/unlink/link.
•	agregar_hash_a_indice / blocks_hash_index_remove_entry_for_block para mantener el índice de hashes.
•	liberar_bloque_fisico para liberar el bloque físico si queda sin referencias.
•	No guarda metadata en disco: el caller (handle_write_block) guarda la metadata cuando write_block_file_tag devuelve 0 (t_file_tag_metadata_save).
Observación concreta tomada de la implementación visible
•	La función realiza validaciones estrictas y toma mutex_metadata antes de validar índice y estado (esto garantiza coherencia sobre la metadata durante la decisión de CoW/Escritura).
2.	Firma (header) void agrandar_file_tag(t_file_tag_metadata* meta, int nuevo_tamanio, int query_id, t_log* logger);
Qué recibe
•	meta (t_file_tag_metadata*): metadata en memoria para el file:tag a agrandar.
•	nuevo_tamanio (int): nuevo tamaño en bytes (múltiplo de block_size).
•	query_id (int) y logger para logs.
Qué hace (paso a paso esperado / inferido por uso en handlers)
•	Calcula cuantos bloques adicionales se necesitan: bloques_necesarios = nuevo_tamanio / block_size; bloques_a_agregar = bloques_necesarios - meta->block_count.
•	Para cada nuevo bloque:
o	Reservar un bloque físico libre del bitmap (bajo mutex_bitmap).
o	Crear archivo physical_blocks/block%04d.dat correspondiente y llenarlo (probablemente con ceros).
o	Crear el hard link lógico en logical_blocks/%06u.dat apuntando a ese archivo físico (link).
o	Añadir el número de bloque a meta->block_numbers (realloc si hace falta) y aumentar meta->block_count.
o	Calcular hash del bloque recién creado y agregarlo al índice con agregar_hash_a_indice (si la política lo requiere).
•	Manejo de error: si no hay bloques libres, debe devolver o señalar ERROR_ESPACIO_INSUFICIENTE; el caller (handle_truncate) luego actualiza meta->size y guarda la metadata.
•	No guarda metadata en disco: el caller guarda la metadata al finalizar.
Qué devuelve
•	En el diseño del proyecto normalmente devuelve void, pero internamente podría loguear errores y dejar meta en un estado consistente parcial; handlers no siempre chequean retorno, por eso es importante que la función deje meta coherente o tome rollback.
Qué llama / a qué módulos les manda datos
•	mutex_bitmap / bitmap (reserva de bloques).
•	crear/interactuar con archivos physical_blocks (I/O).
•	link/unlink para crear hard links en logical_blocks.
•	agregar_hash_a_indice / calcular_hash_bloque.
3.	Firma (header) void achicar_file_tag(t_file_tag_metadata* meta, int nuevo_tamanio, int query_id, t_log* logger);
Qué recibe
•	meta (t_file_tag_metadata*): metadata cargada del file:tag.
•	nuevo_tamanio (int): nuevo tamaño en bytes (múltiplo de block_size).
•	query_id, logger.
Qué hace (paso a paso inferido)
•	Calcula cuantos bloques quitar: bloques_a_quitar = meta->block_count - (nuevo_tamanio / block_size).
•	Para cada bloque a remover (de final hacia atrás):
o	Construir path_logico del bloque a eliminar y path_fisico correspondiente.
o	unlink(path_logico) para eliminar el hard link lógico.
o	Si después de unlink el conteo de links del archivo físico es <= 1 => llamar liberar_bloque_fisico(fs, nro_bloque_fisico) y blocks_hash_index_remove_entry_for_block para eliminarlo del índice.
o	Reducir meta->block_count y (posiblemente) realloc para shrink block_numbers.
•	No guarda metadata en disco: caller (handle_truncate) actualiza meta->size y guarda metadata.
Qué devuelve
•	Normalmente void; registra errores en logger si hay problemas de unlink o I/O.
Qué llama / a qué módulos les manda datos
•	unlink/stat (obtener_link_count) para decidir liberación.
•	liberar_bloque_fisico para liberar physical blocks.
•	blocks_hash_index_remove_entry_for_block para actualizar índice.
•	mutexes usados dentro de liberar_bloque_fisico y del índice (mutex_bitmap y mutex_hash_index).
4.	Firma (header) int crear_logical_block(t_storage_fs* fs, char* logical_path, int bloque_fisico); int eliminar_logical_block(char* logical_path);
Breve (relevante para sincronizar_hardlinks_tag)
•	crear_logical_block: crea el hard link logical_path apuntando al archivo physical_blocks/block%04d.dat (probablemente usa link()).
•	eliminar_logical_block: hace unlink(logical_path).
•	Devuelven 0 en éxito o código de error en fallo.
5.	sincronizar_hardlinks_tag(dst_file, dst_tag, meta_dst)
•	Firma exacta no aparece en el header (se usa en handlers), pero su comportamiento es: Qué recibe
•	dst_file (char*), dst_tag (char*), meta_dst (t_file_tag_metadata*). Qué hace (paso a paso)
•	Para cada índice i en meta_dst->block_count:
o	Construir path_fisico = "%s/physical_blocks/block%04d.dat" con meta_dst->block_numbers[i].
o	Construir path_logico_dest = "%s/files/%s/%s/logical_blocks/%06u.dat".
o	Intentar link(path_fisico, path_logico_dest). Si existe basura en path_logico_dest, unlink antes y volver a link.
•	Maneja errores con logs. Su propósito es que el tag destino comparta (por hard links) los mismos archivos físicos que el origen (clon barato). Qué devuelve
•	Normalmente void o código de estado (handlers no verifica retorno).
6.	Índice de hashes — firmas (header)
•	char* calcular_hash_bloque(int nro_bloque_fisico, t_log* logger);
•	int buscar_nro_bloque_por_hash(const char* hash, t_log* logger);
•	void agregar_hash_a_indice(const char* hash, int nro_bloque_fisico, t_log* logger);
•	int blocks_hash_index_remove_entry_for_block(const char* punto_montaje, int nro_bloque, t_log* logger);
Qué recibe
•	calcular_hash_bloque: nro de bloque físico, retorna string con hash (malloc) o NULL en error.
•	buscar_nro_bloque_por_hash: recibe hash (cadena) y devuelve nro_bloque existente o -1 si no existe.
•	agregar_hash_a_indice: añade la mapping hash->nro_bloque (usado en commit si el hash no existía).
•	blocks_hash_index_remove_entry_for_block: elimina entradas relacionadas con un bloque físico que se libera.
Qué hacen (pasos/internamente)
•	calcular_hash_bloque:
o	Abrir archivo physical_blocks/blockNNNN.dat (idealmente bajo mutex del bloque), leer block_size bytes y calcular hash (SHA1/MD5), devolver hex string.
•	buscar_nro_bloque_por_hash:
o	Consultar estructura en memoria (hash table) protegida por mutex_hash_index; devolver bloque canónico si existe.
•	agregar_hash_a_indice:
o	Insertar la mapping hash->nro_bloque en la tabla, bajo mutex_hash_index.
•	blocks_hash_index_remove_entry_for_block:
o	Remover del índice cualquier mapping que apunte a nro_bloque; usado cuando se libera un bloque físico.
Qué devuelven
•	buscar: nro_bloque encontrado o -1.
•	agregar/remove: 0 éxito o !=0 error.
Qué llaman / a qué módulos les manda datos
•	calcular_hash_bloque usa I/O y funciones de hashing.
•	Las operaciones del índice usan mutex_hash_index para sincronización.
7.	Firma (header) void liberar_bloque_fisico(t_storage_fs* fs, int bloque);
Qué recibe
•	fs: estructura del filesystem (contiene bitmap, rutas, contadores).
•	bloque: número de bloque físico a liberar.
Qué hace (paso a paso inferido)
•	pthread_mutex_lock(&mutex_bitmap).
•	Marcar el bit correspondiente en el bitmap como libre.
•	pthread_mutex_unlock(&mutex_bitmap).
•	Borrar el archivo physical_blocks/block%04d.dat (unlink).
•	Actualizar contadores en fs (liberar contadores/recursos).
•	Llamar blocks_hash_index_remove_entry_for_block para limpiar el índice si no fue eliminado antes.
•	Loggear la operación.
Qué devuelve
•	Normalmente void o un código de estado; handlers asumen que opera correctamente (los logs registran fallos).
8.	read_block_file_tag (implementación observada) Firma void read_block_file_tag(char* file, char* tag, int base_direccion, int tamanio_lectura, char* buffer, t_file_tag_metadata* meta, int block_size);
Qué recibe
•	file, tag: identificación.
•	base_direccion: offset (en bytes) desde el inicio del file lógico.
•	tamanio_lectura: cantidad de bytes a leer.
•	buffer: puntero donde escribir.
•	meta: metadata cargada.
•	block_size: tamaño de bloque.
Qué hace (implementado en storage.c — pasos exactos)
•	pthread_mutex_lock(&mutex_metadata);
•	Calcula bloque_idx = base_direccion / block_size y offset = base_direccion % block_size.
•	block_nro = meta->block_numbers[bloque_idx].
•	Construye block_path = "%s/fs/physical_blocks/block%04d.dat" (la ruta concreta en el repo incluye /fs en el snippet).
•	fopen(block_path, "rb"); si f != NULL:
o	fseek(f, offset, SEEK_SET);
o	fread(buffer, 1, tamanio_lectura, f);
o	fclose(f);
•	pthread_mutex_unlock(&mutex_metadata); Observaciones
•	read_block_file_tag toma mutex_metadata para proteger la lectura de metadata + acceso a physical block con consistencia.
•	No arma paquete ni comunica con worker (esta es función de bajo nivel; los handlers llaman a funciones de red).
9.	obtener_link_count(const char* path)
•	Firma y propósito
o	Devuelve el número de hard links de un fichero (usa stat() y st_nlink).
•	Uso
o	Se usa en commit y delete para decidir si un physical block quedó sin referencias y puede liberarse.


// Estados de un File:Tag
typedef enum {
    WORK_IN_PROGRESS,
    COMMITED
} t_file_tag_state;

// Estructura principal del FileSystem
typedef struct {
    uint32_t fs_size;               // Tamaño total del FS (bytes)
    uint32_t block_size;            // Tamaño de cada bloque físico (bytes)
    uint32_t block_count;           // Cantidad total de bloques físicos
    t_bitarray* bitmap;             // Bitarray de bloques físicos
    char bitmap_path[MAX_BLOCK_PATH];
    char superblock_path[MAX_BLOCK_PATH];
    char hash_index_path[MAX_BLOCK_PATH];
    char physical_blocks_dir[MAX_BLOCK_PATH];
    char files_dir[MAX_BLOCK_PATH];
} t_storage_fs;

// Metadata de un File:Tag
typedef struct {
    char file_name[MAX_FILE_NAME];
    char tag_name[MAX_TAG_NAME];
    uint32_t size;                  // Tamaño del File:Tag (bytes)
    t_file_tag_state state;         // Estado (WORK_IN_PROGRESS/COMMITED)
    int* block_numbers;             // Array con índices de bloques físicos
    uint32_t block_count;           // Cantidad de bloques asignados
} t_file_tag_metadata;

typedef struct{
    char* id;
    int socket;
} t_worker;


Resumen general
•	handlers.c contiene los handlers del servidor Storage para las operaciones del protocolo: CREATE, TRUNCATE, TAG, COMMIT, READ_BLOCK (PAGINA/BLOQUE), WRITE_BLOCK, DELETE y el HANDSHAKE_STORAGE.
•	Las operaciones manipulan metadata (t_file_tag_metadata), hardlinks (bloques lógicos), y el bitmap / bloques físicos mediante llamadas a las funciones definidas en storage.c.
•	Se usan varios mutexes globales declarados extern: mutex_bitmap, mutex_hash_index, mutex_metadata y mutexes_bloques_fisicos[].
•	Hay varias comprobaciones y logs, pero también hay inconsistencias y puntos frágiles que explico abajo.
Explicación por handler (qué hace, parámetros, flujo)
1.	handle_create(int socket_cliente, t_log* logger)
•	Qué recibe: paquete con [nombre_archivo, nombre_tag, query_id] (3 items).
•	Flujo:
o	crea directorios necesarios (crear_directorio_file_tag),
o	arma path metadata y comprueba que no exista,
o	crea una t_file_tag_metadata inicial (size 0, WIP, BLOCKS=[]),
o	guarda metadata en disco (t_file_tag_metadata_save) y verifica que el archivo tenga las claves esperadas (TAMAÑO, ESTADO, BLOCKS),
o	responde LISTO_OK o error.
•	Bloqueos: no toma mutex_metadata para crear el archivo; asume que crear metadata es atómico y que no hay race con otros handlers para el mismo File:Tag.
•	Riesgos observados:
o	No se valida retorno de crear_directorio_file_tag (aunque esa función intenta crear dirs y retorna nada — revisar).
o	Si dos CREATE concurrentes para el mismo archivo/ tag llegan, ambos pueden pasar la comprobación "no existe" y competir para crear metadata — sería mejor proteger con mutex_metadata o algún mecanismo de creación atómica (mkdir + O_EXCL para metadata tmp + rename).
o	Usa list_destroy_and_destroy_elements(valores, free) asumiendo que cada elemento fue heap-allocated por el protocolo — debe ser consistente con quien arme el paquete.
2.	handle_truncate(int socket_cliente, t_log* logger)
•	Qué recibe: [file, tag, nuevo_tamanio, query_id]
•	Flujo:
o	valida que nuevo_tamanio sea múltiplo de block_size (0 permitido),
o	comprueba existencia del metadata, carga metadata,
o	rechaza si COMMITED,
o	decide agrandar o achicar: llama agrandar_file_tag o achicar_file_tag,
o	actualiza meta->size, guarda metadata y responde LISTO_OK.
•	Bloqueos: NO toma mutex_metadata alrededor de la operación completa (aunque agrandar/achicar usan mutex_bitmap internamente). Esto puede ser una fuente de races cuando otros hilos leen/escriben metadata.
•	Riesgos:
o	race entre TRUNCATE y WRITE/COMMIT: ejemplo, TRUNCATE puede leer meta y luego agrandar/achicar mientras otro hilo está haciendo WRITE sobre la misma metadata.
o	No valida retorno de t_file_tag_metadata_load, ni comprueba integridad de meta->block_numbers antes de usarlas.
o	Si meta->block_count cambia por otra operación concurrente, podría producirse acceso fuera de rango.
3.	handle_tag(int socket_cliente, t_log* logger) — clonado (TAG)
•	Qué recibe: [src_file, src_tag, dst_file, dst_tag, query_id]
•	Flujo:
o	arma paths source/destination, comprueba destino no existe,
o	carga metadata origen, crea metadata destino en memoria copiando campos (size, block_count, block_numbers),
o	guarda metadata destino en disco, y crea hardlinks lógicos en el directorio del tag destino (sincronizar hardlinks con sincronizar_hardlinks_tag).
•	Bloqueos: no se toma mutex_metadata globalmente alrededor de clonación; la sincronización con otros threads depende de que otros handlers no modifiquen el origen/destino simultáneamente.
•	Riesgos:
o	Si la metadata origen cambia (ej. un WRITE o COMMIT en curso), la clonación puede copiar una versión intermedia (inconsistencia).
o	No se validan índices de bloques antes de usarlos en sincronizar_hardlinks_tag; si la metadata fuente está corrupta puede causar acceso invalid al array o crear hardlinks con números fuera de rango.
o	No se maneja explícitamente errores de link (solo logea), pero sigue adelante (esto está bien en parte, pero conviene reportar error al worker si falla algo crítico).
o	No hay chequeo de recursos (malloc return) consistente (algún chequeo está presente, otros no).
4.	handle_commit(int socket_cliente, t_log* logger)
•	Qué recibe: [file, tag, query_id]
•	Flujo:
o	carga metadata,
o	si no estaba COMMITED: itera todos los bloques lógicos para deduplicar:
	toma mutex del bloque físico, calcula hash (calcular_hash_bloque), desbloquea mutex,
	busca si ya existe ese hash en índice; si existe y es distinto del bloque actual, realiza re-link (unlink(link) + link nuevo), actualiza metadata en memoria, libera bloque viejo si quedó sin referencias y elimina su entrada del índice de hashes; si no existía, agrega hash al índice.
o	marca meta->state = COMMITED y salva metadata.
•	BLOQUEOS / PROBLEMA CRÍTICO:
o	En el código las líneas que bloqueaban mutex_metadata al inicio y lo desbloqueaban al final están comentadas (líneas con //pthread_mutex_lock(&mutex_metadata); y //pthread_mutex_unlock...). Eso significa que el commit NO protege la metadata contra concurrencia durante toda la operación, aunque modifica meta->block_numbers y meta->state y guarda metadata al final. Esto puede causar condiciones de carrera graves (por ejemplo concurrente WRITE puede modificar meta mientras commit lo itera).
o	Además, cuando se hace pthread_mutex_lock(&mutexes_bloques_fisicos[nro_bloque_actual]) NO se valida que nro_bloque_actual sea un índice válido (0 <= nro < cant_bloques). Si meta está corrupta o cambiado concurrentemente, acceder a mutexes_bloques_fisicos[nro] puede producir un segfault.
•	Otros riesgos:
o	Se calcula hash y luego se hace buscar/actualizar índice de hashes; blocks_hash_index_remove_entry_for_block no toma mutex internamente (lo hacen llamadores), en commit lo llaman dentro de pthread_mutex_lock(&mutex_hash_index) — OK.
o	No todo error intermedio detiene la operación, por lo que la metadata puede quedar parcialmente actualizada si ocurre un fallo.
•	Recomendación crítica: restablecer la protección con mutex_metadata para cubrir la carga de metadata, la iteración, las actualizaciones de meta->block_numbers y la escritura de metadata final. También validar índices antes de usar el array y el acceso a mutexes_bloques_fisicos.
5.	handle_pagina_bloque (READ_BLOCK)
•	Qué recibe: [query_id, file, tag, nro_bloque_logico]
•	Flujo:
o	verifica existencia del File:Tag, carga metadata, valida rango de bloque lógico,
o	obtiene nro_bloque_fisico = meta->block_numbers[nro_bloque_logico],
o	toma pthread_mutex_lock(&mutexes_bloques_fisicos[nro_bloque_fisico]), abre archivo físico y lee block_size bytes a buffer,
o	unlock mutex y envía bloque en paquete LISTO_OK.
•	Bloqueos: usa mutex por bloque físico para evitar lecturas/escrituras concurrentes al mismo .dat.
•	Riesgos:
o	No valida que nro_bloque_fisico esté dentro del rango [0, cant_bloques). Si la metadata está corrupta puede indexar fuera y segfault al acceder a mutexes_bloques_fisicos[nro].
o	En caso de fopen fallido tras lock, la función retorna sin desbloquear mutexes_bloques_fisicos (mirar: se hace fopen y si fp==NULL, envía error y RETURN sin pthread_mutex_unlock). Revisa líneas 503-510: después de fopen fallido se hace send error y return, pero NO pthread_mutex_unlock — eso deja el mutex bloqueado PERMANENTEMENTE → deadlock para futuros accesos a ese bloque. (Es un bug crítico.)
	Confirmación: la llamada a pthread_mutex_lock está en línea 495, fopen en 503; si fopen falla, el handler hace "list_destroy_and_destroy_elements(valores, free); int err = WTF_ERROR; send(...); return;" sin pthread_mutex_unlock. Esto deja el mutex bloqueado.
o	Incluso si fopen falla rara vez, ese bug puede causar deadlocks globales.
•	Corrección inmediata: garantizar unlock en todos los caminos (usar goto/out label o estructura que asegure unlock antes de return).
6.	handle_write_block(int socket_cliente, t_log* logger)
•	Qué recibe: [file, tag, nro_bloque_logico, contenido (char*), query_id]
•	Flujo:
o	valida existencia y carga metadata,
o	valida no COMMITED y que el bloque lógico está asignado,
o	llama write_block_file_tag(...) que implementa escritura directa o CoW; write_block_file_tag toma y libera mutex_metadata internamente y mutexes_bloques_fisicos donde corresponde,
o	si rc==0 guarda metadata en disco (t_file_tag_metadata_save), responde con códigos adecuados.
•	Riesgos:
o	write_block_file_tag espera buffer de tamaño block_size; aquí el handler envía block_size como argumento. Si el protocolo manda menos/más bytes, podría haber inconsistencias—aun así hay validación en write_block_file_tag.
o	Se asume que write_block_file_tag actualiza meta en memoria; pero si write falla parcialmente, handler todavía intenta t_file_tag_metadata_save únicamente si rc==0 — correcto.
o	No valida índices antes de llamar write (pero write hace validaciones).
•	En general handler_write_block se ve aceptable; mayor riesgo son condiciones fuera de write_block_file_tag.
7.	handle_delete(int socket_cliente, t_log* logger)
•	Qué recibe: [file, tag, query_id]
•	Flujo:
o	bloquea special-case initial_file:BASE (no se puede borrar),
o	arma dir_tag y path_meta, verifica existencia,
o	carga metadata, toma mutex_metadata y copia en memoria blocks (blocks_copy),
o	elimina el archivo metadata en disco (remove), desbloquea mutex_metadata,
o	itera blocks_copy y para cada bloque lógico elimina hardlink lógico y, si el bloque físico quedó sin enlaces, limpia bitmap y elimina del índice de hashes; al final intenta remover directorio logical_blocks y el directorio del tag, con limpieza recursiva si es necesario.
•	Bloqueos: toma mutex_metadata al copiar la lista de bloques y al eliminar metadata en disco.
•	Riesgos:
o	Antes de tomar mutex_metadata ya hizo t_file_tag_metadata_load(path_meta, meta) sin bloqueo; si otro hilo estaba modificando meta, aquí puede haber inconsistencia, aunque luego copia bajo mutex (pero meta pudo cambiar entre load y lock). Mejor tomar mutex_metadata antes de la carga o usar una función de carga que lo haga bajo mutex.
o	En la iteración sobre blocks_copy al construir path_bloque_logico usa dir_tag (p. ej "%s/files/%s/%s") y concatena "/logical_blocks/%06u.dat" — OK.
o	En el bucle, si unlink del hard link lógico se ejecuta y se detecta obtener_link_count(path_bloque_fisico) <=1 se llama liberar_bloque_fisico(fs, nro_bloque_fisico) — esa función usa mutex_bitmap interno, lo cual es correcto.
o	No valida que nro_bloque_fisico esté en rango antes de usarlo para construir path_bloque_fisico ni para liberar_bloque_fisico (liberar_bloque_fisico usará bitarray_clean_bit con índice fuera de rango provocando corrupción o segfault). Se debería validar el valor (0 <= nro < cant_bloques).
•	Otros puntos: en caso de fallos parciales en eliminar archivos, el handler intenta una limpieza heurística; es razonable.
8.	handle_handshake_storage(int socket_cliente, t_log* logger)
•	Flujo:
o	recibe id del worker, llama agregar_worker(id, socket), responde con paquete HANDSHAKE_WORKER que contiene block_size.
•	Riesgos: no valida retorno de recibir_nombre; no valida agregar_worker retorno.
Problemas / bugs concretos y críticos detectados
1.	mutex_metadata comentado en COMMIT (condición de carrera grave)
•	Las llamadas a pthread_mutex_lock(&mutex_metadata) y pthread_mutex_unlock(&mutex_metadata) en handle_commit están comentadas. COMMIT itera y modifica la metadata (meta->block_numbers y meta->state) y guarda meta en disco; debe protegerse con mutex_metadata. Sin esa protección concurrente, pueden ocurrir:
o	Escrituras concurrentes (WRITE_BLOCK) que hagan CoW y actualicen meta mientras commit la itera → lectura de índices inválidos, actualización inconsistente, o acceso fuera de rango.
o	Truncates concurrentes que modifiquen block_count mientras commit procesa → índice fuera de rango.
•	Solución: volver a habilitar la protección con mutex_metadata y verificar que no haya deadlocks con los locks por bloque físico (ordenar locks consistentemente).
2.	Mutex bloqueado en READ_BLOCK si fopen falla
•	En handle_pagina_bloque se toma pthread_mutex_lock(&mutexes_bloques_fisicos[nro_bloque_fisico]); si fopen falla, la función retorna sin desbloquear el mutex. Resultado: el mutex queda permanentemente bloqueado y provoca deadlock para futuras operaciones sobre ese bloque.
•	Corrección: Asegurar pthread_mutex_unlock en todos los caminos (usar etiqueta de salida que haga unlock si se tomó).
3.	Índices de bloque no validados antes de usar mutexes_bloques_fisicos[]
•	En varios lugares (COMMIT, READ, COPY, etc.) se indexa mutexes_bloques_fisicos[nro] sin validar 0 <= nro < cant_bloques. Si metadata está corrupta o ha sido cambiada concurrentemente esto puede causar leer/lock fuera de rango → segfault.
•	Corrección: validar el índice y en caso inválido logear error y omitir/abort operación sobre ese bloque.
4.	Duplicados / inconsistencia de declarations externas
•	En la cabecera hay 'extern bool fresh_start;' duplicado (líneas 22 y 25).
•	Hay "extern t_bitarray* bitmap;" en handlers.c aunque en storage.c el bitmap se asigna como fs->bitmap; si el resto del código usa fs->bitmap (no una variable global bitmap) la declaración extern bitmap es redundante o incorrecta — revisar para evitar inconsistencias. (En storage.c se hace fs->bitmap = bm; pero no se setea variable global bitmap.)
5.	Manejo de errores y returns inconsistentes
•	Hay varios lugares donde el código hace returns sin liberar recursos o sin desbloquear mutexes; riesgo de leaks / deadlocks.
6.	Falta de validación de malloc / retorno de funciones
•	En muchos sitios no se comprueba si malloc/string_from_format devolvieron NULL (algunos sí lo hacen). Debería centralizarse la verificación.
7.	blocks_hash_index_remove_entry_for_block tiene política de locking mixta
•	Algunas llamadas a esa función toman mutex_hash_index externamente antes de llamar; otras funciones confían en que la función lo haga internamente. En handlers.c los llamadores (commit, delete, write) generalmente toman pthread_mutex_lock(&mutex_hash_index) antes de llamar, lo cual está bien; pero la función misma no toma el mutex. Es importante documentar esta convención o hacer que la función tome el mutex internamente para evitar confusiones.
Pequeñas observaciones y patrones mejorables
•	Mensajes y checks inconsistentes (ej. handle_pagina_bloque tiene log que dice "esperaba 3 items" pero valida 4).
•	Validar y unificar uso de tamaños de buffers en snprintf.
•	Verificar inyección de paths (si file/tag vienen con '..' o caracteres no permitidos puede escaparse del punto_montaje).
•	Mejorar la atomicidad en operaciones que crean metadata: crear archivo temp + rename para evitar TOCTOU en CREATE.
•	Comprobar y unificar política de bloqueo: por ejemplo, si se necesita bloquear metadata y luego bloquear bloques físicos, definir orden global de locks para evitar deadlocks (p. ej. siempre mutex_metadata antes de mutexes_bloques_fisicos o viceversa — pero debe ser consistente).
Sugerencias de correcciones concretas (priorizadas)
1.	Corregir desbloqueo en handle_pagina_bloque:
•	Asegurar pthread_mutex_unlock(&mutexes_bloques_fisicos[nro]) en todos los caminos tras tomar el lock (incluso si fopen falla). Patch mínimo.
2.	Proteger COMMIT con mutex_metadata:
•	Volver a habilitar pthread_mutex_lock(&mutex_metadata) al inicio de handle_commit y pthread_mutex_unlock al final. Adicionalmente:
o	Mientras se mantiene mutex_metadata, evitar lock inverso de mutexes_bloques_fisicos en un orden que cause deadlock. Por ejemplo establecer el orden: primero mutexes_bloques_fisicos (por número ascendente) y luego mutex_metadata, o preferir tomar mutex_metadata y luego, para cada bloque, solo tomar el mutex del bloque temporalmente para calcular hash — pero hay que definir un orden global. El camino menos intrusivo: tomar mutex_metadata al inicio para proteger meta, y al iterar por bloques tomar y soltar el mutex por bloque físico solo alrededor de la lectura/hasheado (como ya hace) — el riesgo es una inversión de orden si otro thread toma primero el mutex del bloque y luego mutex_metadata; por eso hay que revisar callers que tomen ambos mutexes (si existen). En el código actual, write_block_file_tag toma mutex_metadata primero, luego mutexes_bloques_fisicos — por tanto tomar mutex_metadata primero en commit mantiene consistencia y evita deadlock (buena práctica: mantener siempre mismo orden: mutex_metadata antes de mutexes_bloques_fisicos cuando sea necesario tomar ambos).
•	Adicional: validar índices antes de usar mutexes_bloques_fisicos[nro].
3.	Agregar validación de índices de bloque antes de usarlos
•	En COMMIT, READ, DELETE, COPY, etc. validar 0 <= nro < cant_bloques; si invalido logear y saltar el bloque.
4.	Eliminar duplicados y arreglar extern bitmap
•	Quitar la declaración extern t_bitarray* bitmap si no se usa, o asegurarse de que existe y se inicializa en setear_o_usar_fs (actualmente sólo fs->bitmap se inicializa). Unificar a una sola referencia (recomiendo usar siempre fs->bitmap y eliminar extern bitmap).
5.	Mejorar manejo de errores y comprobaciones de malloc
•	Añadir cheques sistemáticos y respuestas apropiadas.
6.	Policy para blocks_hash_index_remove_entry_for_block
•	O bien hacer que la función tome mutex_hash_index internamente, o documentar que todos los llamadores deben tomarlo; por seguridad, preferir que la función lo haga internamente (más robusto).



Resumen rápido
•	main.c inicializa el logger y la configuración, lee parámetros del archivo de configuración del storage, carga estructuras globales, inicializa el sistema de archivos llamando a setear_o_usar_fs() y luego arranca el servidor con iniciar_servidor_storage().
•	Tiene 3 funciones/elementos principales: main, cargar_estructuras y leer_configuraciones. Declara variables globales usadas en storage (punto_montaje, logger, fresh_start, block_size, fs_size, retardos y mutexes).
Variables globales importantes
•	punto_montaje (char*)
•	logger (t_log*)
•	fresh_start (bool)
•	block_size, fs_size, retardo_operacion, retardo_aceso_bloque (int)
•	mutex_bitmap, mutex_hash_index, mutex_metadata, mutexes_bloques_fisicos (mutexes)
•	diccionario_workers y mutex_workers
Funciones / flujo
1.	int main(int argc, char* argv[])
•	Valida argumentos (espera 1 argumento: archivo de config).
•	Construye ruta de config con DEFAULT_CONFIG_DIR "./configsSTORAGE/ConfigStorage" + config_path + ".cfg".
o	Atención: la concatenación no incluye separador; produce "./configsSTORAGE/ConfigStorage<nombre>.cfg". Puede ser intencional pero normalmente se espera un slash o separador distinto.
•	Inicia logger: iniciar_logger("Storage.log", "STORAGE", LOG_LEVEL_INFO).
•	Lee config con leer_config(ruta_completa, logger). Si falla sale con EXIT_FAILURE.
•	Obtiene puerto_escucha con config_get_int_value(config, "PUERTO_ESCUCHA").
•	Llama leer_configuraciones(config) para extraer FRESH_START, PUNTO_MONTAJE y retardos; además abre superblock.cfg para obtener FS_SIZE y BLOCK_SIZE.
•	Llama cargar_estructuras() que a su vez llama setear_o_usar_fs() para crear/montar el FS.
•	Finalmente inicia el servidor: iniciar_servidor_storage(logger, puerto_escucha). (Esta función entra en un loop aceptando clientes y creando hilos, por lo que main normalmente no continúa después.)
2.	void cargar_estructuras()
•	Crea diccionario_workers = dictionary_create().
•	Llama setear_o_usar_fs() y guarda resultado en resultado.
•	Si resultado != 0 hace log_error y "return resultado;" (nota: la función está declarada void — bug, ver más abajo).
3.	void leer_configuraciones(t_config* config)
•	Lee FRESH_START con config_get_string_value(config, "FRESH_START"), compara con "TRUE" y setea fresh_start.
•	Setea punto_montaje = config_get_string_value(config, "PUNTO_MONTAJE") y retardos con config_get_int_value.
•	Construye path_super = "%s/superblock.cfg" usando punto_montaje y carga ese config (t_config* super = leer_config(path_super, logger)).
•	Si no puede abrir super, hace log_error y "return EXIT_FAILURE;" (nota: la función está declarada void — bug, ver más abajo).
•	Extrae fs_size y block_size del super.
Problemas / bugs detectados y riesgos
1.	Firmas de funciones inconsistente con returns
•	cargar_estructuras() está declarada como void pero en su cuerpo hace "return resultado;" (línea ~91). Eso no compila correctamente en C (warning/error) o produce comportamiento indefinido según el compilador. Debe devolver int o no retornar nada.
•	leer_configuraciones() está declarada como void pero hace "return EXIT_FAILURE;" cuando no puede abrir el superblock (línea ~114). Mismo problema: la firma debe permitir retornar un código o la función debe manejar el error internamente sin return.
•	Solución: cambiar ambas firmas a int y propagar/chequear los códigos de error en main, o manejar localmente y no retornar.
2.	Construcción de ruta de configuración potencialmente incorrecta
•	DEFAULT_CONFIG_DIR = "./configsSTORAGE/ConfigStorage"
•	snprintf(ruta_completa, "%s%s.cfg", DEFAULT_CONFIG_DIR, config_path);
o	Resultado: "./configsSTORAGE/ConfigStorage<config_path>.cfg"
o	Normalmente se esperaría "./configsSTORAGE/ConfigStorage/<config_path>.cfg" o "./configsSTORAGE/ConfigStorage<algo>...". Revisar intención y agregar separador si hace falta. Riesgo: archivo no encontrado si el path esperado difiere.
3.	Manejo de recursos/configs
•	main no destruye el t_config* config ni libera el t_config* super después de leerlos. Esto produce leaks leves, y además pointers devueltos por config_get_string_value apuntan a datos internos de t_config; si luego se destruye el config, esos punteros quedarían invalid; en este código no llaman config_destroy, así que los punteros quedan válidos, pero es mala práctica. Recomendación:
o	Si necesitás conservar valores más allá de la vida del t_config, duplicar strings (string_duplicate) y luego destruir el config.
o	Liberar (config_destroy) cuando ya no se necesite.
4.	Error handling inconsistente
•	leer_configuraciones intenta abrir superblock.cfg y en error hace return EXIT_FAILURE (en void). Además no hace config_destroy(super) en el éxito. Hay inconsistencias en cómo se propagan errores a main (cargar_estructuras retorna resultado pero main no recibe retorno).
•	Mejor unificar: que leer_configuraciones devuelva int (0 ok, <0 error) y que main detecte y salga limpiamente.
5.	Potenciales problemas con tamaños de buffers
•	ruta_completa y path_super usan buffers de 256 bytes; si punto_montaje o DEFAULT_CONFIG_DIR son muy largos puede haber truncamiento. Probablemente no crítico, pero tener en cuenta.
6.	Robustez / logs
•	No se valida retorno de iniciar_logger ni leer_config aparte del chequeo null en config.
•	No hay manejo de señales, shutdown ordenado o liberación de mutexes en salida.
Sugerencias prácticas (correcciones mínimas y mejoras)
1.	Arreglar firmas y flujo de errores
•	Cambiar:
o	int cargar_estructuras(void) — que devuelva 0 o código de error.
o	int leer_configuraciones(t_config* config) — que devuelva 0 o error.
•	En main, chequear los retornos y salir si hay error (liberar recursos, log y return EXIT_FAILURE).
2.	Corregir la construcción de la ruta de configuración si se quería un separador
•	Ejemplo: snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s.cfg", DEFAULT_CONFIG_DIR, config_path);
3.	Manejo de memoria / config_destroy
•	Duplicar (strdup) las strings necesarias (punto_montaje, algoritmo si se va a usar) y luego llamar config_destroy(config) y config_destroy(super) cuando ya no se necesiten.
•	Alternativa: mantener t_config* en memoria mientras dure ejecución, pero documentarlo.
4.	Verificación y logs
•	Verificar y loggear errores de setear_o_usar_fs en main (ahora cargar_estructuras intenta hacerlo pero no propaga bien el error).
•	Agregar checks a iniciar_logger o reusar logger en caso de fallo.
5.	Revisión de rutas y convenciones
•	Revisar y unificar cómo se componen los nombres de archivo de config en todo el repo (master tenia "./configsMASTER/ConfigMaster%s.cfg"; storage usa otro formato). Consistencia evita errores por ruta mal formada.
6.	Opcional: agregar limpieza ordenada y manejo de señales
•	Capturar SIGINT/SIGTERM para cerrar sockets y destruir mutexes y logs ordenadamente.
Puntos concretos a corregir (patch recomendado)
•	Cambiar firmas:
o	int cargar_estructuras(void) { ... return resultado; }
o	int leer_configuraciones(t_config* config) { ... return 0/EXIT_FAILURE; }
•	En main, tras leer config:
o	if (leer_configuraciones(config) != 0) { log_error(...); return EXIT_FAILURE; }
o	if (cargar_estructuras() != 0) { log_error(...); return EXIT_FAILURE; }
•	Añadir config_destroy(config) y config_destroy(super) cuando corresponda (o duplicar strings antes de destruir).
•	Revisar la concatenación de rutas y agregar separadores si necesario.



Funciones y explicación
1.	int mkdir_si_no_existe(char* dir)
•	Qué hace: intenta crear el directorio dir con permisos 0777; si ya existe, no es error.
•	Retorno: 0 si OK, -1 si error distinto de EEXIST.
•	Uso: helper simple para crear directorios.
2.	int setear_o_usar_fs()
•	Qué hace: inicializa/usa el FS en punto_montaje. Calcula cantidad de bloques (fs_size / block_size), crea/ inicializa mutexes por bloque físico, crea estructuras en disco (physical_blocks, files, bitmap, initial_file/BASE, metadata.cfg, índice de hashes) si fresh_start es true; si no, valida y mapea el bitmap existente y comprueba bloques.
•	Bloqueos: no usa mutexes de bitmap explícitamente aquí (pero usa msync/bitarray).
•	Retorno: 0 en éxito, varios códigos negativos en errores específicos (-98, -99, -2, -1).
•	Efectos secundarios: crea/borra carpetas y archivos, mapea bitmap con mmap, inicializa fs global, aloca mutexes por bloque.
•	Notas de implementación:
o	Inicializa mutexes fisicos con pthread_mutex_init.
o	Reserva y escribe block0000 con ceros en caso de fresh_start.
o	Crea hard link inicial del logical block 000000.dat apuntando a block0000.
o	Calcula y escribe MD5 de block0 en blocks_hash_index.config.
•	Riesgos observados:
o	Hay dos declaraciones extern bool fresh_start; duplicadas (líneas 24 y 27): es redundante, aunque no crítico.
o	mutexes_bloques_fisicos se malloca pero no se libera en esta función (esto es lógico si viven mientras el proceso está activo).
o	Se usan string_from_format para paths y luego se liberan; en general parecen bien manejados.
o	Cuando crea bitmap usa mmap y bitarray_create_with_mode con el area mapeada; esta área no se munmapea al final del branch fresh_start (los menús hacen munmap en errores, pero en éxito el mmap queda vivo mientras fs->bitmap apunte a él: esperado).
3.	int reservar_bloque_fisico(t_storage_fs* fs)
•	Qué hace: recorre el bitmap buscando el primer bit libre, lo marca y msync, devuelve el número de bloque reservado.
•	Bloqueo: pthread_mutex_lock(&mutex_bitmap) para proteger el bitmap.
•	Retorno: índice de bloque reservado (>=0) o -1 si no hay espacio.
4.	void liberar_bloque_fisico(t_storage_fs* fs, int bloque)
•	Qué hace: limpia el bit en el bitmap y hace msync.
•	Bloqueo: usa mutex_bitmap.
5.	int crear_logical_block(t_storage_fs* fs, char* logical_path, int bloque_fisico)
•	Qué hace: crea un hard link que apunta al physical block bloque_fisico. Elimina el logical_path anterior y hace link(physical, logical).
•	Retorno: valor de link() (0 éxito, -1 error).
•	Nota: usa snprintf para construir path físico con fs->physical_blocks_dir.
6.	int eliminar_logical_block(char* logical_path)
•	Qué hace: unlink(logical_path). Retorna resultado del unlink.
7.	int actualizar_metadata_config(char* path_metadata, int nuevo_tamanio, char* blocks_str)
•	Qué hace: usa config_create/ config_set_value/config_save para actualizar TAMAÑO y BLOCKS en metadata.cfg.
•	Retorno: 0 éxito, -1 si no pudo abrir el config.
8.	char* path_metadata(char* file, char* tag)
•	Qué hace: helper que construye y devuelve el path absoluto al metadata.cfg de un File:Tag.
•	Retorna string mallocado por string_from_format, quien debe liberarse por el llamador si corresponde.
9.	void crear_directorio_file_tag(char* file, char* tag)
•	Qué hace: crea directorios necesarios para files/<file>/<tag>/logical_blocks usando mkdir_si_no_existe; libera strings.
•	Nota: hay un comentario "// <-- faltaba" en la línea donde crea dir_logical: indica que se agregó mkdir para logical_blocks.
10.	void asignar_bloques_logicos(t_file_tag_metadata* meta, int nuevo_tamanio, int block_size, t_bitarray* bitmap, int total_blocks)
•	Qué hace: amplía (en memoria) meta->block_numbers para tener suficientes bloques lógicos para cubrir nuevo_tamanio; marca el bloque físico 0 en bitmap si era necesario; rellena los nuevos bloques con referencia al físico 0 (es decir, los nuevos lógicos apuntan a físico 0). No hace hardlinks aquí — solo ajusta la metadata en memoria y el bitmap.
•	Bloqueo: toma mutex_bitmap.
•	Notas:
o	Calcula nuevos_bloques = ceil(nuevo_tamanio / block_size).
o	Realloc para meta->block_numbers.
o	msync del bitmap al final.
11.	void desasignar_bloques_logicos(t_file_tag_metadata* meta, int nuevo_tamanio, t_bitarray* bitmap, int block_size)
•	Qué hace: libera en el bitmap los bloques físicos que dejan de usarse cuando se achica la metadata; ajusta meta->block_count y realoca array (si queda 0 libera el array).
•	Bloqueo: mutex_bitmap.
•	Nota: no elimina hardlinks del filesystem: solo limpia bitmap y actualiza estructura en memoria. (En el flujo general, el caller suele borrar hardlinks en disco cuando achica).
12.	void copiar_bloques_file_tag(... meta_src, meta_dst, block_size)
•	Qué hace: copia (byte a byte) el contenido de cada bloque físico de meta_src a la ubicación física correspondiente de meta_dst; maneja locking por bloque físico (dos mutexes por cada copia para origen y destino en orden para evitar deadlock).
•	Parámetros: src_file/src_tag/dst_file/dst_tag (strings para logging), meta_src/meta_dst (metadata), block_size.
•	Comportamiento:
o	Valida índices de bloques (se fija en cant_bloques para no usar mutex fuera de rango).
o	Si origen==destino salta.
o	Bloquea mutexes en orden (first, second).
o	Abre archivos src (rb) y dst (wb), malloc buffer block_size, lee, escribe, libera.
o	Aplica usleep(retardo_aceso_bloque) antes de lectura y escritura (simula latencia).
•	Errores: registra logs y continúa (no aborta toda operación si falla un bloque).
•	Notas de seguridad: valida rangos antes de usar mutexes; libera recursos en todos los caminos.
13.	void sincronizar_hardlinks_tag(char* dst_file, char* dst_tag, t_file_tag_metadata* meta_dst)
•	Qué hace: para cada bloque declarado en meta_dst hace unlink del logical (por si existe basura) y crea un hard link desde el physical correspondiente al logical en files/.../logical_blocks/<index>.dat.
•	Efectos: crea los hardlinks en disco que representan los bloques lógicos del tag destino.
•	Notas: si link falla, lo registra.
14.	void read_block_file_tag(char* file, char* tag, int base_direccion, int tamanio_lectura, char* buffer, t_file_tag_metadata* meta, int block_size)
•	Qué hace: lee datos desde un bloque lógico: calcula bloque_idx y offset dentro del bloque (base_direccion), obtiene el número de bloque físico desde meta, arma path del bloque (OBSERVA un posible bug, ver abajo), abre y lee desde offset a buffer.
•	Bloqueo: toma mutex_metadata al inicio y lo libera al final.
•	Retorno: void; si no pudo abrir archivo no hace nada (no llena buffer).
•	Posible bug de path: construye block_path con "%s/fs/physical_blocks/..." (línea 643), añadiendo "/fs" entre punto_montaje y physical_blocks. En setear_o_usar_fs y en otras funciones los bloque físicos están en "%s/physical_blocks/...". Esa "/fs" extra parece errónea y puede generar que fopen falle y la lectura no ocurra.
15.	int write_block_file_tag(..., int bloque_idx, int tamanio_escritura, char* buffer, t_file_tag_metadata* meta, int query_id)
•	Función más compleja: escribe un bloque lógico con gestión CoW.
•	Validaciones iniciales:
o	Verifica punteros no nulos.
o	Verifica tamanio_escritura == block_size (si no → WTF_ERROR).
o	Toma mutex_metadata.
o	Verifica índice fuera de rango contra meta->block_count.
o	Rechaza escritura si meta->state == COMMITED.
•	Flujo:
o	Obtiene nro_bloque_fisico_actual = meta->block_numbers[bloque_idx], arma path y obtiene link count (obtener_link_count).
o	CASO 1 (links <= 1): escritura directa en el bloque físico:
	bloquea mutexes_bloques_fisicos[nro_fisico_actual], abre archivo con "r+b", escribe buffer completo (seek a 0), flush, cierra, desbloquea mutex y mutex_metadata, retorna 0.
o	CASO 2 (links > 1): Copy-On-Write:
	reserva un nuevo bloque físico con reservar_bloque_fisico(fs). Si no hay espacio, devuelve ERROR_ESPACIO_INSUFICIENTE.
	bloquea ambos mutexes (actual y nuevo) en orden (first, second) para evitar deadlock.
	abre src (rb) y dst (w+b), copia todo el bloque en memoria (malloc block_size), aplica memcpy del buffer sobre copy_buf (esto escribe todo el bloque; ojo: el código copia buffer por tamanio_escritura que igual a block_size en validación previa), escribe dst, fflush.
	actualiza meta->block_numbers[bloque_idx] = nro_bloque_nuevo.
	actualiza hardlink lógico: unlink viejo logical path y link(path_fisico_str, path_logico).
	si el bloque viejo quedó con links <=1 tras operación (verifica con obtener_link_count(path_fisico_actual)), libera el bloque viejo en bitmap y remueve entrada del hash index para ese bloque.
	desbloquea mutexes y mutex_metadata y retorna 0.
•	Bloqueos y sincronización: mutex_metadata cubre operaciones a meta; mutexes_bloques_fisicos protegen acceso a cada archivo físico; mutex_bitmap se usa indirectamente al reservar/liberar.
•	Notas y riesgos:
o	write exige que tamanio_escritura == block_size; en algunos casos se podrían escribir menos bytes si es una escritura parcial, pero aquí se fuerza escritura full-block.
o	Se llama reservar_bloque_fisico(fs) donde fs es la variable global; write usa fs global (declarada al tope). Si fs no fue inicializada, reservar falla. Pero setear_o_usar_fs inicializa fs.
o	Actualización del índice de hashes: cuando se libera el bloque viejo se llama blocks_hash_index_remove_entry_for_block(punto_montaje, nro_bloque_fisico_actual, logger) protegido por mutex_hash_index.
o	Hay orden de desbloqueo correcto y manejo de errores intentando liberar bloque en casos de fallo.
o	Cuando actualiza hardlink lógico arma path con punto_montaje/files/... y link de path_fisico_str que es "%s/physical_blocks/block%04d.dat" — consistente.
16.	void liberar_bloques_file_tag(t_file_tag_metadata* meta, t_bitarray* bitmap)
•	Qué hace: recorre meta->block_numbers y limpia los bits en bitmap si están set; msync y desbloquea.
•	Bloqueo: mutex_bitmap.
•	Nota: no elimina hardlinks en disco (solo modifica bitmap).
17.	long obtener_link_count(const char* path)
•	Qué hace: stat(path) y devuelve st_nlink; si stat falla devuelve 0.
•	Uso: para decidir si un bloque físico puede liberarse.
18.	void agrandar_file_tag(t_file_tag_metadata* meta, int nuevo_tamanio, int query_id, t_log* logger)
•	Qué hace: similar a asignar_bloques_logicos pero además crea hard links en el filesystem que apuntan al block0000 (bloque físico 0).
•	Flujo:
o	realloc meta->block_numbers
o	path_bloque_fisico_0 = "%s/physical_blocks/block0000.dat"
o	por cada nuevo índice: meta->block_numbers[i] = 0; crea hard link desde block0000 a files/.../logical_blocks/%06u.dat
o	actualiza meta->block_count.
•	Notas: logea errores si link falla, pero continúa.
19.	void achicar_file_tag(t_file_tag_metadata* meta, int nuevo_tamanio, int query_id, t_log* logger)
•	Qué hace: reduce la cantidad de bloques del tag: elimina hardlinks lógicos que sobran y, si el bloque físico queda sin referencias, libera el bloque en bitmap y remueve su entrada del índice de hashes.
•	Flujo:
o	Calcula bloques_nuevos = ceil(nuevo_tamanio / block_size); especial caso nuevo_tamanio==0 => bloques_nuevos=0.
o	Para cada i en [bloques_nuevos, bloques_actuales):
	arma path lógico y física, intenta unlink(path_bloque_logico)
	si unlink exitoso y obtener_link_count(path_bloque_fisico) <=1:
	llamar liberar_bloque_fisico(fs, nro) (que usa mutex_bitmap)
	llamar blocks_hash_index_remove_entry_for_block bajo mutex_hash_index
o	Al final, actualiza meta->block_count y realoca meta->block_numbers (o lo libera si queda 0).
•	Notas:
o	logs más detallados y inclui strerror(errno) en warning si unlink falla (buen cambio).
o	Eliminar hardlinks y liberar bitmap están sincronizados correctamente.
20.	char* calcular_hash_bloque(int nro_bloque_fisico, t_log* logger)
•	Qué hace: abre el archivo blockNNNN.dat, mmapea su contenido (PROT_READ, MAP_PRIVATE), llama crypto_md5 sobre el contenido y devuelve el string mallocado con MD5; cierra y munmap.
•	Retorno: md5 mallocado o NULL si fallo.
•	Uso: para COMMIT y mantener índice de hashes.
21.	int buscar_nro_bloque_por_hash(const char* hash, t_log* logger)
•	Qué hace: abre blocks_hash_index.config (ruta "%s/blocks_hash_index.config"), lee propiedad con config_has_property/config_get_string_value y parsea "block%04d" para devolver numero de bloque. Protegido por mutex_hash_index.
•	Retorno: bloque o -1 si no existe/errores.
22.	void agregar_hash_a_indice(const char* hash, int nro_bloque_fisico, t_log* logger)
•	Qué hace: abre el archivo blocks_hash_index.config en modo "a", escribe "hash=blockNNNN\n". Protegido con mutex_hash_index.
23.	int blocks_hash_index_remove_entry_for_block(const char* punto_montaje, int nro_bloque, t_log* logger)
•	Qué hace: elimina entradas en blocks_hash_index.config que terminen en "=blockNNNN" para el bloque indicado:
o	Abre el archivo original, crea tmp, copia líneas que no terminen en el suffix target, y renombra tmp sobre original si se removió alguna entrada.
o	Maneja trims y vacíos, usa getline para leer.
o	Retorna 0 si ok (o incluso si no existía el archivo, no es error), -1 en errores.
•	Bloqueo: no usa mutex_hash_index en la función; muchos llamadores usan pthread_mutex_lock(&mutex_hash_index) antes de llamar (ver write y achicar). Aquí la función no asume que el mutex ya está tomado; callers que no tomen el mutex corren riesgo de race condition. (En varios sitios anteriores sí se toma el mutex externamente, pero aquí no se toma dentro de la función).
•	Notas: maneja sólidamente la escritura a tmp, fflush/fsync y renombrado; borra tmp si no se removió nada.
Problemas / bugs / observaciones importantes detectadas
•	Ruta en read_block_file_tag: usa snprintf(block_path, "%s/fs/physical_blocks/block%04d.dat", punto_montaje, block_nro); esto introduce "/fs" extra y probablemente causa fopen fallido. En otras funciones el path es "%s/physical_blocks/...". Es muy probable que esta sea la causa de lecturas que no funcionan.
•	Duplicado de extern bool fresh_start; (no crítico pero desordenado).
•	Inclusiones duplicadas: #include <commons/string.h> aparece dos veces.
•	blocks_hash_index_remove_entry_for_block no toma mutex_hash_index internamente. Muchos llamadores sí toman mutex_hash_index externamente (por ejemplo en write y achicar llaman al lock alrededor de la función), pero hay sitios como agregar_hash_a_indice y buscar_nro_bloque_por_hash que toman mutex_hash_index internamente. Es mejor unificar la política: o la función lo hace internamente o se documenta que el caller debe hacerlo. Actualmente está mezclado.
•	Path sizes: algunos buffers usan 256/512; cuidado si punto_montaje es muy largo (riesgo mínimo pero presente).
•	libre/realloc y manejo de metadatas: en desasignar_bloques_logicos hay un realloc a tamaño (nuevos_bloques ? nuevos_bloques : 1) — si nuevo=0 se hace realloc de tamaño 1, pero luego liberan en caso nuevo==0; es algo extraño pero manejado.
•	Reserva de mutexes por bloque: se malloca mutexes_bloques_fisicos en setear_o_usar_fs y se inicializan, pero no hay función de cleanup (no es necesariamente bug si el proceso vive toda la vida).
•	En write_block_file_tag la validación fuerza tamanio_escritura == block_size; si hay operaciones parciales (escrituras no alineadas) el diseño actual no las soporta. read_block_file_tag sí permite leer tamaños arbitrarios pero la escritura exige bloque completo.
•	Algunos retornos y errores usan constantes como WTF_ERROR, ERROR_FUERA_DE_LIMITE etc. Asumo que están definidas en otros headers. Asegurate que callers interpretan esos códigos correctamente.
•	En copiar_bloques_file_tag se valida índices de mutexes usando int max_mutexes = cant_bloques; si cant_bloques no fue inicializado correctamente puede ser problema; por lo general setear_o_usar_fs pone cant_bloques.
Sugerencias y próximos pasos prácticos
•	Corregir la ruta en read_block_file_tag (quitar "/fs/") y probar lecturas.
•	Estandarizar el manejo del mutex_hash_index: o la función blocks_hash_index_remove_entry_for_block toma el mutex internamente, o documentar y asegurarse que todos los llamadores lo hagan.
•	Revisar casos de escritura parcial si el sistema de protocolos necesita soportarlas (o documentar que solo se escriben bloques completos).
•	Añadir tests o logs adicionales para confirmar que reservar_bloque_fisico/liberar_bloque_fisico actúan correctamente en condiciones de concurrencia.
•	(Opcional) Agregar función de cleanup para liberar mutexes y memoria en shutdown.



Resumen rápido
•	threads_st.c implementa el bucle servidor que acepta conexiones y crea un hilo por cliente, y la función que atiende cada cliente en ese hilo.
•	No hace trabajo de negocio en sí (los handlers están en otro módulo); su responsabilidad es aceptar conexiones, lanzar hilos, recibir operaciones y despachar a los handlers correspondientes.
Contenido y explicación de funciones
1.	void iniciar_servidor_storage(t_log* logger, int puerto)
•	Qué hace:
o	Llama a iniciar_servidor(puerto, logger) para obtener un socket servidor.
o	En un while(1) acepta clientes llamando a esperar_cliente(socket_servidor, logger).
o	Para cada cliente aceptado crea un t_args_server* (malloc), llena socket_cliente y logger, crea un hilo (pthread_create) que ejecuta atender_cliente pasando el puntero args, y lo detacha (pthread_detach).
•	Parámetros:
o	logger: puntero al logger para logs.
o	puerto: puerto en que escuchar.
•	Efectos:
o	Lanza un hilo nuevo por cliente; los hilos son detach (no se pueden joinear).
•	Aspectos importantes / riesgos detectados:
o	No hay comprobación del retorno de malloc para args. Si malloc devuelve NULL el programa crashará.
o	No se comprueba el valor de retorno de pthread_create ni de pthread_detach; fallos podrían dejar al cliente sin servicio o generar comportamiento indefinido.
o	No hay límite o pool de hilos: el servidor crea hilos ilimitados, lo que puede agotar recursos bajo carga (mejor usar pool o semáforo para limitar concurrencia).
o	No se maneja ningún mecanismo de “shutdown” limpio del servidor; while(1) infinito sin forma de salir.
o	No hay manejo específico de SIGPIPE/errores al escribir a sockets cerrados (esto suele manejarse en socket_utils o en flags de send).
2.	void* atender_cliente(void* args)
•	Qué hace:
o	Recibe el puntero args (t_args_server*), extrae socket_cliente y logger, libera el struct args y entra en un bucle while(1).
o	En el loop, llama recibir_operacion(socket_cliente, logger) para obtener un op-code (cod_op).
o	Con un switch despacha a diferentes handlers según el op-code:
	OP_CREATE -> handle_create
	OP_TRUNCATE -> handle_truncate
	OP_TAG -> handle_tag
	OP_COMMIT -> handle_commit
	OP_PEDIR_PAGINA -> handle_pagina_bloque
	OP_WRITE_BLOCK -> handle_write_block
	OP_DELETE -> handle_delete
	HANDSHAKE_STORAGE -> handle_handshake_storage
	cod_op == -1 -> interpreta cierre/error: llama quitar_worker_por_socket(socket_cliente), liberar_conexion(socket_cliente) y retorna NULL (termina el hilo)
	default -> log warning para código no manejado
•	Parámetros:
o	args: puntero a t_args_server con socket_cliente y logger.
•	Retorno:
o	devuelve NULL al terminar (hilo termina).
•	Aspectos importantes / riesgos detectados:
o	Free temprano: se hace free(datos) justo después de extraer socket/logger; esto es correcto siempre que el hilo copie lo necesario (lo hace). No hay race allí porque el main thread no usa ese puntero luego.
o	Si recibir_operacion bloquea indefinidamente, el hilo queda bloqueado (comportamiento esperado). Depende del protocolo y del timeout que implemente recibir_operacion.
o	Cuando recibir_operacion devuelve -1 se asume que hay que quitar al worker por socket y liberar la conexión; esto está bien, pero depende de que quitar_worker_por_socket y liberar_conexion manejen correctamente concurrentes.
o	No se hace close del socket en rutas donde el loop termina por otras razones; la única ruta explícita de cierre es cuando recibes -1. Si un handler decide terminar la conexión, depende del handler hacerlo.
o	No se captura ni se gestionan excepciones internas de handlers; si un handler falla de forma inesperada (por ejemplo longjmp o segfault) el hilo muere. Eso es normal pero conviene tener robustez.
Cuestiones de concurrencia y recursos
•	Hilos Detached:
o	Al crear hilos detached no se puede joinearlos ni obtener su retorno; es aceptable en servidores de conexión corta, pero dificulta control y shutdown ordenado.
•	Límite de hilos:
o	Actualmente no hay límite: bajo alta carga se pueden agotar fd/threads/memoria. Recomiendo implementar un semáforo o pool de threads para limitar la concurrencia máxima.
•	Errores no verificados:
o	Falta comprobar malloc, pthread_create y pthread_detach. Añadir comprobaciones y fallbacks robustos.
•	Manejo de SIGPIPE / writes a sockets cerrados:
o	Si un handler hace send() a un socket que el cliente cerró, puede generar SIGPIPE en ciertos entornos y terminar el proceso. Es habitual ignorar SIGPIPE o usar MSG_NOSIGNAL en send/write.
o	Probablemente socket_utils tiene manejo; si no, conviene agregarlo.
•	Coordinación con storage.c:
o	Los handlers invocan funciones de storage que usan mutexes (mutex_bitmap, mutex_metadata, etc.). threads_st.c no hace locking adicional, lo correcto es que la protección esté en storage/handlers (que parece ser el caso).
Posibles mejoras / hardening sugerido
•	Validar retornos:
o	Comprobar resultado de malloc en iniciar_servidor_storage y reaccionar (log + close socket + continuar).
o	Comprobar resultado de pthread_create y pthread_detach; si pthread_create falla, liberar recursos y rechazar cliente.
•	Limitar concurrencia:
o	Añadir un semáforo/contador global que limite el número de hilos concurrentes. Ej: sem_t pool_sem inicializado a N; antes de crear hilo sem_wait y al terminar el hilo sem_post.
o	Alternativa: usar un thread pool con una cola de conexiones para evitar crear/destroy de hilos constantemente.
•	Manejo de SIGPIPE:
o	Asegurarse que el proceso ignora SIGPIPE (signal(SIGPIPE, SIG_IGN)) o que los envíos usan flags para no generar la señal.
•	Limpieza y shutdown:
o	Proveer mecanismo para detener el servidor limpiamente (cerrar socket_servidor, esperar fin de hilos o usar contador de hilos y señal).
•	Manejar recursos en handlers:
o	Revisar que los handlers siempre liberen recursos si detectan cierre del cliente (para evitar leaks).
•	Logs y métricas:
o	Llevar conteo de conexiones, hilos actuales, rechazos por falta de recursos.
Errores/bugs concretos detectados en este archivo
•	Falta verificación de errores de malloc/pthread_create/pthread_detach.
•	Posible problema de agotamiento de recursos por no limitar hilos (riesgo alto en carga).
•	No hay control de shutdown (no es estrictamente un bug pero limita manejo en producción).
Sugerencias de cambios mínimos (patchs recomendados)
•	Añadir comprobación de malloc, e.g. si args==NULL: log_error y close(socket_cliente).
•	Comprobar pthread_create: si falla, log_error, close(socket_cliente), free(args).
•	Opcional: usar semáforo para limitar hilos; ejemplo simple: antes de pthread_create sem_wait(&sem_pool); dentro del hilo al final sem_post(&sem_pool).
•	Ignorar SIGPIPE en inicialización del servidor (signal(SIGPIPE, SIG_IGN)) para evitar que un write a socket cerrado mate el proceso.
•	Registrar y retornar códigos de error coherentes si no se pudo aceptar cliente.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     Archivo de Configuración
Campo
Tipo
Descripción
PUERTO_ESCUCHA
Numérico
Puerto al cual otros módulos se deberán conectar con la memoria
TAM_MEMORIA
Numérico
Tamaño expresado en bytes del espacio de usuario de la memoria.
TAM_PAGINA
Numérico
Tamaño de las páginas en bytes.
ENTRADAS_POR_TABLA
Numérico
Cantidad de entradas de cada tabla de páginas.
CANTIDAD_NIVELES
Numérico
Cantidad de niveles de tablas de páginas
RETARDO_MEMORIA
Numérico
Tiempo en milisegundos que deberá esperarse antes de responder una petición de memoria
PATH_SWAPFILE
String
Path donde se encuentra el archivo de swapfile.bin
RETARDO_SWAP
Numérico
Tiempo en milisegundos que deberá esperarse antes de responder una petición de SWAP
LOG_LEVEL
String
Nivel de detalle máximo a mostrar.
Compatible con log_level_from_string()
DUMP_PATH
String
Path donde se almacenarán los archivos de DUMP
PATH_INSTRUCCIONES
String
Path donde se encuentran los scripts de instrucciones

Ejemplo de Archivo de Configuración
PUERTO_ESCUCHA=8002
TAM_MEMORIA=4096
TAM_PAGINA=64
ENTRADAS_POR_TABLA=4
CANTIDAD_NIVELES=3
RETARDO_MEMORIA=1500
PATH_SWAPFILE=/home/utnso/swapfile.bin
RETARDO_SWAP=15000
LOG_LEVEL=TRACE
DUMP_PATH=/home/utnso/dump_files/
PATH_INSTRUCCIONES=/home/utnso/scripts/

Descripción de las entregas
Debido al orden en que se enseñan los temas de la materia en clase, los checkpoints están diseñados para que se pueda realizar el trabajo práctico de manera iterativa incremental tomando en cuenta los conceptos aprendidos hasta el momento de cada checkpoint.
Check de Control Obligatorio 1: Conexión inicial
Fecha: 19/04/2025
Objetivos:
Familiarizarse con Linux y su consola, el entorno de desarrollo y el repositorio.
Aprender a utilizar las Commons, principalmente las funciones para listas, archivos de configuración y logs.
Definir el Protocolo de Comunicación.
Todos los módulos están creados y son capaces de establecer conexiones entre sí.

Check de Control Obligatorio 2: Ejecución Básica  
Fecha: 25/05/2025
Objetivos:
Módulo Kernel:
Planificación de corto y largo plazo con FIFO
Administración de IO

Módulo CPU:
Interpretación de instrucciones y ejecución de instrucciones

Módulo Memoria:
Devolver lista de instrucciones
Devolver un valor fijo de espacio libre (mock)

Módulo IO: Completo 

Carga de trabajo estimada:
Módulo Kernel: 40%
Módulo CPU: 30%
Módulo Memoria: 15%
Módulo IO: 15%

Check de Control Obligatorio 3: CPU Completa y Memoria
Fecha: 21/06/2025
Objetivos:
Módulo Kernel:
Planificación de corto y largo plazo completa.

Módulo CPU: Completo

Módulo Memoria:
Esquema de Memoria principal completo
Administración de espacio libre en memoria principal

Carga de trabajo estimada:
Módulo Kernel: 30%
Módulo CPU: 30%
Módulo Memoria: 40%

Entregas Finales
Fechas: 12/07/2025, 19/07/2025, 02/08/2025
Objetivos:
Finalizar el desarrollo de todos los procesos.
Probar de manera intensiva el TP en un entorno distribuido.
Todos los componentes del TP ejecutan los requerimientos de forma integral.



