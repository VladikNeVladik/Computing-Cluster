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

	// Server tracking:
	int server_tracking_socket_fd;
	int server_tracking_timeout_fd;

	// Server discovery:
	struct sockaddr_in server_addr;
};

struct thread_info
{
    int num_cpu;
	int line_size;
	int event_fd;
	void* data_pack;
};

//-------------------------------------
// Initialization and deinitialization
//-------------------------------------

void init_cluster_client(struct ClusterClientHandle* handle);
void stop_cluster_client(struct ClusterClientHandle* handle);

//-----------------------------
// Computation task management
//-----------------------------

void set_maximum_load(struct ClusterClientHandle* handle);

#endif // COMPUTING_CLUSTER_CLIENT_HPP_INCLUDED
