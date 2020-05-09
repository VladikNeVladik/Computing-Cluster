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
	bool local_discovery;
	struct sockaddr_in server_addr;
	const char* server_hostname;

	// Server tracking:
	int server_tracking_socket_fd;
	int server_tracking_timeout_fd;

	// Connection management:
	int conn_socket_fd;

	// Computation task management:
	unsigned max_threads;
};

//-------------------------------------
// Initialization and deinitialization 
//-------------------------------------

void init_cluster_client(struct ClusterClientHandle* handle, unsigned max_threads, const char* master_host = NULL);
void stop_cluster_client(struct ClusterClientHandle* handle);

#endif // COMPUTING_CLUSTER_CLIENT_HPP_INCLUDED