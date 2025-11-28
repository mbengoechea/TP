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
•	Registrar y retornar códigos de error coherentes si no se pudo aceptar cliente.

