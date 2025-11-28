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

