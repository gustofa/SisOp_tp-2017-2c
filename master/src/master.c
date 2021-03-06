/*
 ============================================================================
 Name        : master.c
 Author      : Carlos Flores
 Version     :
 Copyright   : GitHub @Charlos
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include "master.h"
#include <fcntl.h>
pedido_master * pedido;
int job_id;
pthread_mutex_t lock, lock_transf;

int main(int argc, char ** argv) {

	if (argc != 5) {
		printf(
				"ERROR: Cantidad de parametros invalida. Deben ser 4: transformador/ruta \nreductor/ruta \nArchivo de origen/ruta \narchivo resultado/ruta");
		exit(0);
	}
	crear_logger(argv[0], &logger, true, LOG_LEVEL_TRACE);
	struct timeval tiempo_inicio, tiempo_finalizacion;
	inicializar_estadisticas();
	gettimeofday(&tiempo_inicio, NULL);

	master_config = crear_config();
	pedido = crear_pedido_yama(argv);
	transformador_file = read_file(pedido->ruta_trans);

	yama_socket = connect_to_socket(master_config->ip_yama,
			master_config->port_yama);

	if (pthread_mutex_init(&lock, NULL) != 0)
	{
		printf("\n mutex init failed\n");
		return 1;
	}
	if (pthread_mutex_init(&lock_transf, NULL) != 0)
	{
		printf("\n mutex init failed\n");
		return 1;
	}

	// Enviar Pedido a YAMA
	t_yama_planificacion_resp * respuesta_solicitud = yama_nueva_solicitud(yama_socket, pedido->ruta_orige, logger);
	job_id = respuesta_solicitud->job_id;
	// RECV LOOP
	while(respuesta_solicitud->exec_code != SERVIDOR_DESCONECTADO && respuesta_solicitud->etapa != FINALIZADO_OK && respuesta_solicitud->etapa != FINALIZADO_ERROR) {
		atender_solicitud(respuesta_solicitud);

		respuesta_solicitud = yama_resp_planificacion(yama_socket, logger);
	}
	if(respuesta_solicitud->exec_code == SERVIDOR_DESCONECTADO) {
		log_trace(logger, "El servidor se desconecto. Abortando ejecucion");
		printf("El servidor se desconecto. Abortando ejecucion");

	} else if(respuesta_solicitud->etapa == FINALIZADO_ERROR) {
		log_trace(logger, "La ejecucion del job [%d] termino con error", job_id);
		printf("La ejecucion del job [%d] termino con error", job_id);

	} else if(respuesta_solicitud->etapa == FINALIZADO_OK){

		log_trace(logger, "Job %d - Termino ejecucion satisfactoriamente", job_id);
	} else {
		printf("Error no contemplado");
	}
	gettimeofday(&tiempo_finalizacion, NULL);
	metricas->tiempo_total = ((tiempo_finalizacion.tv_sec*1e6 + tiempo_finalizacion.tv_usec) - (tiempo_inicio.tv_sec*1e6 + tiempo_inicio.tv_usec)) / 1000.0;
	imprimir_estadisticas();
	liberar_estructuras();

	return EXIT_SUCCESS;
}
master_cfg * crear_config() {
	t_config * conf = malloc(sizeof(t_config));
	conf = config_create("./master.cfg");

	master_cfg * mcfg = malloc(sizeof(master_cfg));
	mcfg->ip_yama = string_duplicate(config_get_string_value(conf, "IP_YAMA"));
	mcfg->port_yama = string_duplicate(config_get_string_value(conf, "PORT_YAMA"));

	config_destroy(conf);
	return mcfg;
}
pedido_master * crear_pedido_yama(char ** argv) {
	pedido_master * pedido = malloc(sizeof(pedido_master));
	pedido->ruta_trans = argv[1];
	pedido->ruta_reduc = argv[2];
	pedido->ruta_orige = argv[3];
	pedido->ruta_resul = argv[4];

	return pedido;
}
void atender_respuesta_transform(respuesta_yama_transform * respuesta) {

	struct timeval tiempo_inicio, tiempo_fin;
	uint32_t dif_tiempo;
	t_estadisticas * est_transformacion = metricas->metricas_transformacion;
	ip_port_combo * combo = split_ipport(respuesta->ip_port);

	gettimeofday(&tiempo_inicio, NULL);

	log_trace(logger, "Job: %d - Se creo hilo para transformacion", job_id);

	pthread_mutex_lock(&lock_transf);
	aux_sim->cant_transf_simultaneo++;
	if(aux_sim->cant_transf_simultaneo > est_transformacion->cant_max_tareas_simultaneas) {
		est_transformacion->cant_max_tareas_simultaneas = aux_sim->cant_transf_simultaneo;
	}
	pthread_mutex_unlock(&lock_transf);

	int socket_worker = connect_to_socket(combo->ip, combo->port);
	int result;

	if(socket_worker > 0) {
		int status = transform_req_send(socket_worker, respuesta->bloque, respuesta->bytes_ocupados, respuesta->archivo_temporal, transformador_file->filesize, transformador_file->file, logger);

		if(status > 0) {
			t_response_task * response_task = task_response_recv(socket_worker, logger);
			send_recv_status(socket_worker, response_task->exec_code);

			if(response_task->exec_code == DISCONNECTED_CLIENT) {
				result = NODO_DESCONECTADO;
				//result = TRANSF_ERROR;
			} else {
				result = traducir_respuesta(response_task->result_code, TRANSFORMACION);
			}
			//testeando free
			free(response_task);
		} else {
			result = NODO_DESCONECTADO;
			//result = TRANSF_ERROR;
		}
	} else {
		result = NODO_DESCONECTADO;
		//result = TRANSF_ERROR;
	}
	if(result == TRANSF_ERROR || result == NODO_DESCONECTADO) {
		est_transformacion->cant_fallos_job++;
	}

	yama_registrar_resultado_transf_bloque(yama_socket, job_id, respuesta->nodo, respuesta->bloque, result, logger);
	liberar_respuesta_transformacion(respuesta);
	liberar_combo_ip(combo);

	close(socket_worker);

	pthread_mutex_lock(&lock_transf);
	aux_sim->cant_transf_simultaneo--;
	pthread_mutex_unlock(&lock_transf);


	log_trace(logger, "Job: %d - Termina hilo para transformacion", job_id);
	gettimeofday(&tiempo_fin, NULL);
	dif_tiempo = ((tiempo_fin.tv_sec*1e6 + tiempo_fin.tv_usec) - (tiempo_inicio.tv_sec*1e6 + tiempo_inicio.tv_usec)) / 1000.0;

	est_transformacion->reg_promedio += dif_tiempo;
}
void atender_respuesta_reduccion(t_red_local * respuesta) {
	struct timeval tiempo_inicio, tiempo_fin;
	uint32_t dif_tiempo;
	ip_port_combo * combo = split_ipport(respuesta->ip_puerto);
	t_estadisticas * est_reduccion_local = metricas->metricas_reduccion_local;
	struct_file *script_reduccion = read_file(pedido->ruta_reduc);


	pthread_mutex_lock(&lock);
	aux_sim->cant_reduc_local_simultaneo++;
	if(aux_sim->cant_reduc_local_simultaneo > est_reduccion_local->cant_max_tareas_simultaneas) {
		est_reduccion_local->cant_max_tareas_simultaneas = aux_sim->cant_reduc_local_simultaneo;
	}
	pthread_mutex_unlock(&lock);


	log_trace(logger, "Job: %d - Se creo hilo para reduccion local", job_id);

	int socket_worker = connect_to_socket(combo->ip, combo->port);
	int result;

	if(socket_worker > 0) {
		int status = local_reduction_req_send(socket_worker, respuesta->archivos_temp, respuesta->archivo_rl_temp, script_reduccion->filesize, script_reduccion->file, logger);

		if(status > 0) {
			t_response_task * response_task = task_response_recv(socket_worker, logger);
			send_recv_status(socket_worker, response_task->exec_code);

			if(response_task->exec_code == DISCONNECTED_CLIENT) {
				result = REDUC_LOCAL_ERROR;
			} else {
				result = traducir_respuesta(response_task->result_code, REDUCCION_LOCAL);
			}
			//testeando free
			free(response_task);
		} else {
			result = REDUC_LOCAL_ERROR;
		}
	} else {
		result = REDUC_LOCAL_ERROR;
	}


	if(result == REDUC_LOCAL_ERROR || result == NODO_DESCONECTADO) {
		est_reduccion_local->cant_fallos_job++;
	}

	yama_registrar_resultado(yama_socket, job_id, respuesta->nodo, RESP_REDUCCION_LOCAL, result, logger);
	closure_rl(respuesta);
	liberar_combo_ip(combo);


	pthread_mutex_lock(&lock);
	aux_sim->cant_reduc_local_simultaneo--;
	pthread_mutex_unlock(&lock);

	unmap_file(script_reduccion->file, script_reduccion->filesize);
	free(script_reduccion);

	log_trace(logger, "Job: %d - Termino hilo para reduccion local", job_id);
	gettimeofday(&tiempo_fin, NULL);
	dif_tiempo = ((tiempo_fin.tv_sec*1e6 + tiempo_fin.tv_usec) - (tiempo_inicio.tv_sec*1e6 + tiempo_inicio.tv_usec)) / 1000.0;
	//list_add(est_reduccion_local->tiempo_ejecucion_hilos, &dif_tiempo);
	est_reduccion_local->reg_promedio += dif_tiempo;

}

void resolver_reduccion_global(t_yama_planificacion_resp *solicitud){
	int i, nodo_enc_socket;
	t_red_global * nodo_encargado;

	struct timeval tiempo_inicio, tiempo_fin;
	uint32_t dif_tiempo;
	t_estadisticas * est_reduccion_global = metricas->metricas_reduccion_global;

	gettimeofday(&tiempo_inicio, NULL);

	for(i = 0; i < list_size(solicitud->planificados); i++) {
		nodo_encargado = list_get(solicitud->planificados, i);
		if(nodo_encargado->designado)break;
	}
	ip_port_combo * ip_port = split_ipport(nodo_encargado->ip_puerto);
	nodo_enc_socket = connect_to_socket(ip_port->ip, ip_port->port);
	int result;

	liberar_combo_ip(ip_port);
	struct_file * file = read_file(pedido->ruta_reduc);
	log_trace(logger, "socket de nodo designado: %d", nodo_enc_socket);
	if(nodo_enc_socket > 0) {
		// Se envia script y lista de nodos a worker designado
		int status = global_reduction_req_send(nodo_enc_socket, file->filesize, file->file,  solicitud->planificados, logger);
		log_trace(logger, "Se envio a worker [%s] el paquete", nodo_encargado->nodo);

		unmap_file(file->file, file->filesize);
		free(file);

		if(status > 0) {
			// recibir respuesta de worker
			t_response_task * response_task = task_response_recv(nodo_enc_socket, logger);
			send_recv_status(nodo_enc_socket, response_task->exec_code);

			if(response_task->exec_code == DISCONNECTED_CLIENT) {
				result = REDUC_GLOBAL_ERROR;
			} else {
				result = traducir_respuesta(response_task->result_code, REDUCCION_GLOBAL);
			}
			// Enviar notificacion a YAMA
			free(response_task);
		}
		else {
			result = REDUC_GLOBAL_ERROR;
		}
	}
	else {
		result = REDUC_GLOBAL_ERROR;
	}
	if(result == REDUC_GLOBAL_ERROR) {
		est_reduccion_global->cant_fallos_job++;
	}

	yama_registrar_resultado(yama_socket, job_id, nodo_encargado->nodo, RESP_REDUCCION_GLOBAL, result, logger);


	gettimeofday(&tiempo_fin, NULL);
	dif_tiempo = ((tiempo_fin.tv_sec*1e6 + tiempo_fin.tv_usec) - (tiempo_inicio.tv_sec*1e6 + tiempo_inicio.tv_usec)) / 1000.0;

	//list_add(est_reduccion_global->tiempo_ejecucion_hilos, &dif_tiempo);
	est_reduccion_global->reg_promedio += dif_tiempo;
}
void atender_respuesta_almacenamiento(t_yama_planificacion_resp * solicitud) {
	log_trace(logger, "Job: %d - Iniciando Almacenamiento", job_id);
	t_almacenamiento *almacenamiento = list_get(solicitud->planificados, 0);
	log_trace(logger, "Almacenamient: nombre de archivo final %s", almacenamiento->archivo_rg);
	ip_port_combo * ip_port_combo = split_ipport(almacenamiento->ip_puerto);
	int nodo_enc_socket;
	int result;
	nodo_enc_socket = connect_to_socket(ip_port_combo->ip, ip_port_combo->port);

	if(nodo_enc_socket > 0) {
		// enviar solicitus a worker
		int status = final_storage_req_send(nodo_enc_socket, almacenamiento->archivo_rg, pedido->ruta_resul, logger);

		if(status > 0) {

			// recibir archivo y ruta
			t_response_task * response = task_response_recv(nodo_enc_socket, logger);
			send_recv_status(nodo_enc_socket, response->exec_code);

			if(response->exec_code == DISCONNECTED_CLIENT) {
				result = ALMACENAMIENTO_ERROR;
			} else {
				result = traducir_respuesta(response->result_code, ALMACENAMIENTO);
			}
		}
		else {
			result = ALMACENAMIENTO_ERROR;
		}
	}
	else {
		result = ALMACENAMIENTO_ERROR;
	}

	// guardar
	yama_registrar_resultado(yama_socket, job_id, almacenamiento->nodo, RESP_ALMACENAMIENTO, result, logger);
	liberar_combo_ip(ip_port_combo);
}

struct_file * read_file(char * path) {
	FILE * file;

	file = fopen(path, "r");

	if (file) {
		fseek(file, 0L, SEEK_END);
		size_t size = ftell(file); // st.st_size;
		fseek(file, 0L, SEEK_SET);
		struct_file * file_struct = malloc(sizeof(struct_file));
		file_struct->filesize = size;

		file_struct->file = map_file(path, O_RDWR);
		fclose(file);
		return file_struct;
	}
	return NULL;
}

void liberar_respuesta_transformacion(respuesta_yama_transform *respuesta){
	free(respuesta->archivo_temporal);
	free(respuesta->ip_port);
	free(respuesta->nodo);
	free(respuesta);
}

t_estadisticas * inicializar_struct_estadisticas(int etapa) {
	t_estadisticas * nueva_estadistica = malloc(sizeof(t_estadisticas));
	nueva_estadistica->etapa = etapa;
	nueva_estadistica->tiempo_promedio_ejecucion = 0;
	nueva_estadistica->cant_max_tareas_simultaneas = 0;
	nueva_estadistica->cant_total_tareas = 0;
	nueva_estadistica->cant_fallos_job = 0;
	nueva_estadistica->tiempo_ejecucion_hilos = list_create();
	nueva_estadistica->reg_promedio = 0;

	return nueva_estadistica;
}
void inicializar_estadisticas() {
	metricas = malloc(sizeof(t_metricas));
	aux_sim = malloc(sizeof(aux_procesos_simultaneos));
	metricas->tiempo_total = 0;
	metricas->cant_total_fallos_job = 0;
	metricas->metricas_transformacion = inicializar_struct_estadisticas(TRANSFORMACION);
	metricas->metricas_reduccion_local = inicializar_struct_estadisticas(REDUCCION_LOCAL);
	metricas->metricas_reduccion_global = inicializar_struct_estadisticas(REDUCCION_GLOBAL);
	aux_sim->cant_reduc_local_simultaneo = 0;
	aux_sim->cant_transf_simultaneo = 0;
}

void crear_hilo_transformador(t_transformacion *transformacion, int job_id){
	pthread_t hilo_solicitud;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	respuesta_yama_transform *transformacion_master = crear_transformacion_master(transformacion);
	transformacion_master->job = job_id;
	free(transformacion);
	pthread_create(&hilo_solicitud, &attr, &atender_respuesta_transform, transformacion_master);
	pthread_attr_destroy(&attr);
}

void crear_hilo_reduccion_local(t_red_local *reduccion){
	pthread_t hilo_solicitud;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&hilo_solicitud, &attr, &atender_respuesta_reduccion, reduccion);
	pthread_attr_destroy(&attr);

}

respuesta_yama_transform *crear_transformacion_master(t_transformacion *transformacion_yama){
	respuesta_yama_transform *transformacion_master = malloc(sizeof(respuesta_yama_transform));
	transformacion_master->archivo_temporal = transformacion_yama->archivo_temporal;
	transformacion_master->bloque = transformacion_yama->bloque;
	transformacion_master->bytes_ocupados = transformacion_yama->bytes_ocupados;
	transformacion_master->ip_port = transformacion_yama->ip_puerto;
	transformacion_master->nodo = transformacion_yama->nodo;

	return transformacion_master;
}

void atender_solicitud(t_yama_planificacion_resp *solicitud){
	int i, length, nodo_enc_socket;
	t_red_global * nodo_encargado;
	switch(solicitud->etapa){
	case TRANSFORMACION:
		log_trace(logger, "Job: %d - Iniciando Transformacion", job_id);
		t_estadisticas * est_transformacion = metricas->metricas_transformacion;
		est_transformacion->cant_total_tareas += list_size(solicitud->planificados);
		//est_transformacion->cant_max_tareas_simultaneas = max(list_size(solicitud->planificados), est_transformacion->cant_max_tareas_simultaneas);
		for(i = 0, length = list_size(solicitud->planificados); i < length; i++) {

			t_transformacion * transformacion = (t_transformacion *) list_get(solicitud->planificados, i);
			crear_hilo_transformador(transformacion, job_id);
		}

		break;
	case REDUCCION_LOCAL:
		log_trace(logger, "Job: %d - Iniciando Reduccion Local", job_id);
		t_estadisticas * est_reduccion_local = metricas->metricas_reduccion_local;
		est_reduccion_local->cant_total_tareas += list_size(solicitud->planificados);
		//est_reduccion_local->cant_max_tareas_simultaneas = max(list_size(solicitud->planificados), est_reduccion_local->cant_max_tareas_simultaneas);
		for(i = 0, length = list_size(solicitud->planificados); i < length; i++) {
			t_red_local *reduccion = list_get(solicitud->planificados, i);
			crear_hilo_reduccion_local(reduccion);
		}
		break;
	case REDUCCION_GLOBAL:
		log_trace(logger, "Job: %d - Iniciando Reduccion Global", job_id);
		resolver_reduccion_global(solicitud);
		t_estadisticas * est_reduccion_global = metricas->metricas_reduccion_global;
		est_reduccion_global->cant_total_tareas += list_size(solicitud->planificados);
		list_iterate(solicitud->planificados, closure_rg);
		break;
	case ALMACENAMIENTO:
//		nodo_encargado = malloc(sizeof(t_red_global));

//		atender_respuesta_almacenamiento(solicitud);
		log_trace(logger, "Job: %d - Iniciando Almacenamiento", job_id);
		t_almacenamiento *almacenamiento = list_get(solicitud->planificados, 0);
		log_trace(logger, "Almacenamient: nombre de archivo final %s", almacenamiento->archivo_rg);
		ip_port_combo * ip_port_combo = split_ipport(almacenamiento->ip_puerto);
		nodo_enc_socket = connect_to_socket(ip_port_combo->ip, ip_port_combo->port);
		int result;

		if(nodo_enc_socket > 0) {
			// enviar solicitus a worker
			int status = final_storage_req_send(nodo_enc_socket, almacenamiento->archivo_rg, pedido->ruta_resul, logger);

			if(status > 0) {

				// recibir archivo y ruta
				t_response_task * response = task_response_recv(nodo_enc_socket, logger);

				if(response->exec_code == DISCONNECTED_CLIENT) {
					result = ALMACENAMIENTO_ERROR;
				} else {
					result = traducir_respuesta(response->result_code, ALMACENAMIENTO);
				}
			}
			else {
				result = ALMACENAMIENTO_ERROR;
			}
		}
		else {
			result = ALMACENAMIENTO_ERROR;
		}

		// guardar
		yama_registrar_resultado(yama_socket, job_id, almacenamiento->nodo, RESP_ALMACENAMIENTO, result, logger);
		liberar_combo_ip(ip_port_combo);

		// TODO Terminar de liberar estructuras
		break;
	default:
		// Todavia nose
		printf("default");
	}
	// cada hilo tiene que liberar los atributos internos de su solicitud
	list_destroy(solicitud->planificados);
	free(solicitud);
}
int traducir_respuesta(int respuesta, int etapa) {
	if(respuesta == SUCCESS) {
		switch (etapa) {
		case TRANSFORMACION:
			return TRANSF_OK;
		case REDUCCION_LOCAL:
			return REDUC_LOCAL_OK;
		case REDUCCION_GLOBAL:
			return REDUC_GLOBAL_OK;
		case ALMACENAMIENTO:
			return ALMACENAMIENTO_OK;
		default:
			return 0;
		}
	} else {
		switch (etapa) {
		case TRANSFORMACION:
			return TRANSF_ERROR;
		case REDUCCION_LOCAL:
			return REDUC_LOCAL_ERROR;
		case REDUCCION_GLOBAL:
			return REDUC_GLOBAL_ERROR;
		case ALMACENAMIENTO:
			return ALMACENAMIENTO_ERROR;
		default:
			return 0;
		}
	}
}

void imprimir_estadisticas(){

	t_estadisticas * est_transformacion = metricas->metricas_transformacion;
	t_estadisticas * est_reduccion_local = metricas->metricas_reduccion_local;
	t_estadisticas * est_reduccion_global = metricas->metricas_reduccion_global;

	metricas->cant_total_fallos_job = est_transformacion->cant_fallos_job + est_reduccion_local->cant_fallos_job + est_reduccion_global->cant_fallos_job;
	printf("Job ID: %d \n", job_id);
	printf("Tiempo de ejecucion total: %d ms  \n", metricas->tiempo_total);
	printf("Cantidad de fallos: %d\n", metricas->cant_total_fallos_job);

	printf("\nETAPA DE TRANSFORMACION\n");

	if(est_transformacion->cant_total_tareas > 0) {
		printf("Tiempo promedio de ejecucion: %d ms\n", est_transformacion->reg_promedio/est_transformacion->cant_total_tareas);
	} else {
		printf("Tiempo promedio de ejecucion: No hubo tareas\n");
	}

	printf("Cantidad total de tareas realizadas: %d\n", est_transformacion->cant_total_tareas);
	printf("Cantidad máxima de tareas simultaneas: %d\n", est_transformacion->cant_max_tareas_simultaneas);
	printf("Cantidad de fallos en la etapa: %d\n", est_transformacion->cant_fallos_job);

	printf("\nETAPA DE REDUCCION LOCAL\n");

	if(est_reduccion_local->cant_total_tareas > 0) {
		printf("Tiempo promedio de ejecucion: %d ms\n", est_reduccion_local->reg_promedio/est_reduccion_local->cant_total_tareas);
	} else {
		printf("Tiempo promedio de ejecucion: No hubo tareas\n");
	}

	printf("Cantidad total de tareas realizadas: %d\n", est_reduccion_local->cant_total_tareas);
	printf("Cantidad máxima de tareas simultaneas: %d\n", est_reduccion_local->cant_max_tareas_simultaneas);
	printf("Cantidad de fallos en la etapa: %d\n", est_reduccion_local->cant_fallos_job);

	printf("\nETAPA DE REDUCCION GLOBAL\n");

	if(est_reduccion_global->cant_total_tareas > 0) {
		printf("Tiempo promedio de ejecucion: %d ms\n", est_reduccion_global->reg_promedio/est_reduccion_global->cant_total_tareas);
	} else {
		printf("Tiempo promedio de ejecucion: No hubo tareas\n");
	}

	printf("Cantidad total de tareas realizadas: %d\n", est_reduccion_global->cant_total_tareas);
	printf("Cantidad de fallos en la etapa: %d\n", est_reduccion_global->cant_fallos_job);

}
void liberar_estructuras() {
	free(metricas->metricas_transformacion);
	free(metricas->metricas_reduccion_local);
	free(metricas->metricas_reduccion_global);
	free(metricas);
	free(aux_sim);
	free(transformador_file);
	pthread_mutex_destroy(&lock);
	pthread_mutex_destroy(&lock_transf);

}
