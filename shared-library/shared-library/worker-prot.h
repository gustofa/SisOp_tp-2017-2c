/*
 * worker-prot.h
 *
 *  Created on: 19/9/2017
 *      Author: Gustavo Tofaletti
 */

#ifndef WORKER_PROT_H_
#define WORKER_PROT_H_

#include "yama-prot.h"

#define TRANSFORM_OC			1
#define REDUCE_LOCALLY_OC		2
#define REDUCE_GLOBAL_OC		3
#define STORAGE_OC				4
#define REQUEST_TEMP_FILE		5
#define REDUCE_GLOBAL_OC_N		6

#define	SUCCESS							1
#define	ERROR							-200
#define	DISCONNECTED_CLIENT				-201
#define	DISCONNECTED_SERVER				-202


typedef struct{
	 int block;
	 int used_size;
	 char* result_file;
	 int script_size;
	 void* script;
	 int16_t exec_code;
} t_request_transformation;

typedef struct{
	 char* temp_files;
	 char* result_file;
	 int script_size;
	 void* script;
	 int16_t exec_code;
} t_request_local_reduction;

typedef struct{
	void *script;
	t_list *lista_nodos_reduccion_global;
	uint16_t exec_code;
} t_request_global_reduction;

typedef struct{
	int16_t oc_code; 		//Etapa realizada
	int16_t result_code;	//Resultado de la etapa
	int16_t exec_code;		//Resultado de la recepción del mensaje
} t_response_task;


/*
 * Respuesta desde Worker a Master al recibir el pedido de cada etapa
 */
void request_send_resp(int * master_socket, int status);

/*
 * solicitud de Etapa 1 (Transformación) desde Master hacia Worker
 */
int transform_req_send(int worker_socket, int block, int used_size, char* result_file, int script_size, void* script, t_log * logger);

/*
 * Recepción en Worker de solicitud de Etapa 1 (Transformación)
 */
t_request_transformation * transform_req_recv(int * client_socket, t_log * logger);

/*
 * solicitud de Etapa 2 (Reducción Local) desde Master hacia Worker
 * char* temp_files ---> cadena que contiene lista de los archivos separados por ";" para poder hacer un split luego
 */
int local_reduction_req_send(int worker_socket, char* temp_files, char* result_file, int script_size, void* script, t_log * logger);

/*
 * Recepción en Worker de solicitud de Etapa 2 (Reducción Local)
 */
t_request_local_reduction * local_reduction_req_recv(int * client_socket, t_log * logger);

/*
 * solicitud de Etapa 3 (Reducción Global) desde Master hacia Worker
 */
int global_reduction_req_send(int worker_socket, int script_size, void *script, t_list* lista_nodos, t_log * logger);

/*
 * Recepción en Worker de solicitud de Etapa 3 (Reducción Global)
 */
t_request_global_reduction *global_reduction_req_recv(int * client_socket, t_log * logger);

/*
 * Respuesta desde Worker hacia Master de resultado de una Etapa (solo responde un código, por lo que es la misma función para todas las etapas)
 */
void task_response_send(int master_socket,int OC, int resp_code, t_log * logger);

/*
 * Recepción de respuesta en Master del resultado de la etapa
 */
t_response_task*  task_response_recv(int worker_socket, t_log * logger);
void mandar_archivo_temporal(int fd, char *nombre_archivo);

#endif /* WORKER_PROT_H_ */
