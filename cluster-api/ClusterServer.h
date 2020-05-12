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
	bool want_task;

	// Hot fix variable: !!!!
	bool   returned_task;
	size_t active_computations;

	// task managment
	int* task_list;
	size_t num_tasks;
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

	// task management:
	struct task_info* task_manager;
	size_t num_unresolved;
	size_t num_tasks;
	size_t size_task;
	size_t size_ret;
};

enum{
	NOT_RESOLVED = 0,
	RESOLVING,
	COMPLETED
};

struct task_info
{
	void* task;
	void* ret;
	int   status;
};

//-------------------------------------
// Initialization and deinitialization
//-------------------------------------

void init_cluster_server(struct ClusterServerHandle* handle);
void stop_cluster_server(struct ClusterServerHandle* handle);

//-----------------------------
// Computation task management
//-----------------------------

int compute_task(size_t num_tasks, void* tasks, size_t size_task, void* rets, size_t size_ret);

#endif // COMPUTING_CLUSTER_SERVER_HPP_INCLUDED
