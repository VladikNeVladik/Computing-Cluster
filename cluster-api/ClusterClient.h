// No Copyright. Vladislav Aleinik 2020
//======================================
// Computing Cluster Client
//======================================
// - Connects to computation cluster
// - Performs computations
// - Sends results back
//======================================
#ifndef COMPUTING_CLUSTER_CLIENT_HPP_INCLUDED
#define COMPUTING_CLUSTER_CLIENT_HPP_INCLUDED

//----------------
// Enable Logging 
//----------------

#include "Logging.h"

//-------------
// Client Data 
//-------------

#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>

struct ClusterClientHandle
{
	// Eventloop:
	int epoll_fd;
	pthread_t eventloop_thr_id;

	// Server discovery:
	struct sockaddr_in server_addr;

	// Server tracking:
	
};

//-------------------------------------
// Initialization and deinitialization 
//-------------------------------------

void init_cluster_client(struct ClusterClientHandle* handle);
void stop_cluster_client(struct ClusterClientHandle* handle);

//-------------------
// Discovery process 
//-------------------

void discover_server(struct ClusterClientHandle* handle);
void   forget_server(struct ClusterClientHandle* handle);

//-----------------
// Login precedure
//-----------------

void login_to_server(struct ClusterClientHandle* handle);

//---------------------
// Still-alive process
//---------------------

void start_still_alive_routine(struct ClusterClientHandle* handle);
void pause_still_alive_routine(struct ClusterClientHandle* handle);

//-----------------------------
// Computation task management 
//-----------------------------

void start_task_computing_routine(struct ClusterClientHandle* handle);
void pause_task_computing_routine(struct ClusterClientHandle* handle);

void set_maximum_load(struct ClusterClientHandle* handle);

#endif // COMPUTING_CLUSTER_CLIENT_HPP_INCLUDED