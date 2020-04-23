// No Copyright. Vladislav Aleinik 2020
//==============================================
// Computing Cluster Server
//==============================================
// - Performs time-to-time client node discovery
// - Gives out computational tasks
// - Aggregates computation results
//==============================================
#ifndef COMPUTING_CLUSTER_SERVER_HPP_INCLUDED
#define COMPUTING_CLUSTER_SERVER_HPP_INCLUDED

//----------------
// Enable Logging 
//----------------

#include "Logging.h"

//-------------
// Server Data 
//-------------

#include <pthread.h>

struct ClusterServerHandle
{
	// Eventloop:
	int epoll_fd;
	pthread_t eventloop_thr_id;

	// Discovery:
	int discovery_socket_fd;
	int discovery_timer_fd;
};

//-------------------------------------
// Initialization and deinitialization 
//-------------------------------------

void init_cluster_server(struct ClusterServerHandle* handle);
void stop_cluster_server(struct ClusterServerHandle* handle);

//-----------------------------
// Computation task management 
//-----------------------------

void add_computation_tasks  (struct ClusterServerHandle* handle);
void get_computation_results(struct ClusterServerHandle* handle);

#endif // COMPUTING_CLUSTER_SERVER_HPP_INCLUDED