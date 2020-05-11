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

// Bool:
typedef char bool;

struct Connection
{
	int  socket_fd;
	bool can_read;
	bool can_write;
};

struct ClusterClientHandle
{
	// Eventloop:
	int epoll_fd;
	pthread_t eventloop_thr_id;

	// Server discovery:
	bool local_discovery;
	const char* server_hostname;
	struct sockaddr_in server_addr;

	// Server tracking:
	int server_tracking_socket_fd;
	int server_tracking_timeout_fd;

	// Connection management:
	struct Connection server_conn;

	// Computation task management:
	size_t max_threads;
	bool* computations_ready;
};

//-------------------------------------
// Initialization and deinitialization 
//-------------------------------------

void init_cluster_client(struct ClusterClientHandle* handle, size_t max_threads, const char* master_host);
void stop_cluster_client(struct ClusterClientHandle* handle);

#endif // COMPUTING_CLUSTER_CLIENT_HPP_INCLUDED