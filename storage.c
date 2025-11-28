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

