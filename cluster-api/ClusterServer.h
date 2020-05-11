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

// Bool:
typedef char bool;

struct Connection
{
	int  socket_fd;
	bool can_read;
	bool can_write;

	// Hot fix variable: !!!!
	bool   returned_task;
	size_t active_computations;
};

struct ClusterServerHandle
{
	// Eventloop:
	int epoll_fd;
	pthread_t eventloop_thr_id;

	// Discovery:
	int discovery_socket_fd;
	int discovery_timer_fd;

	// Connection management:
	int accept_socket_fd;
	struct Connection* client_conns;
	size_t num_clients;
	size_t max_clients;
};

//-------------------------------------
// Initialization and deinitialization
//-------------------------------------

void init_cluster_server(struct ClusterServerHandle* handle);
void stop_cluster_server(struct ClusterServerHandle* handle);

//-----------------------------
// Computation task management
//-----------------------------

void* compute_task(struct ClusterServerHandle* handle, size_t num_tasks, void* tasks, size_t size_task, void* rets, size_t size_ret);

#endif // COMPUTING_CLUSTER_SERVER_HPP_INCLUDED
