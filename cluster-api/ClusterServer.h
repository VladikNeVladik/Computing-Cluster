// No Copyright. Vladislav Aleinik && Maxim Davydov 2020
//=======================================================
// Computing Cluster Server
//=======================================================
// - Answers to clients sendout with "Here I Am, client!"
// - Gives out computational tasks
// - Aggregates computation results
// - Detects disconnected clients
//=======================================================
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
#include <sys/socket.h>
#include <netdb.h>

// Bool:
typedef char bool;

struct Connection
{
	int  socket_fd;

	// Recv buffer:
	char*  recv_buffer;
	size_t bytes_recieved;

	// Send operation:
	uint32_t now_sending_task_i;

	// Task managment:
	size_t requested_tasks;

	uint32_t* task_list;
};

struct TaskInfo
{
	void* task;
	void* ret;
	int   status;
};

struct ClusterServerHandle
{
	// Eventloop:
	int epoll_fd;
	pthread_t eventloop_thr_id;

	// Discovery:
	int discovery_socket_fd;
	struct sockaddr_in broadcast_addr;

	// Connection management:
	int accept_socket_fd;
	struct Connection* client_conns;
	size_t num_clients;
	size_t max_clients;

	// Task management:
	struct TaskInfo* task_manager;
	size_t num_not_resolved;
	size_t num_completed;
	size_t min_not_resolved_task;

	// Task computation:
	char* user_tasks; // Those two are not allocated, but rest in the userland
	char* user_rets;

	size_t num_tasks;
	size_t task_size;
	size_t ret_size;
};

enum
{
	NOT_RESOLVED = 0,
	RESOLVING,
	COMPLETED
};

//-------------
// Computation 
//-------------

int compute_task(size_t num_tasks, void* tasks, size_t size_task, void* rets, size_t size_ret);

#endif // COMPUTING_CLUSTER_SERVER_HPP_INCLUDED
