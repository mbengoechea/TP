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

