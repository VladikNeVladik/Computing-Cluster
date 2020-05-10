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

#include "Connection.h"

struct ClusterClientHandle
{
	// Eventloop:
	int epoll_fd;
	pthread_t eventloop_thr_id;

	// Server tracking:
	int server_tracking_socket_fd;
	int server_tracking_timeout_fd;

	// Connection management:
	struct Connection server_conns;

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

void init_cluster_client(struct ClusterClientHandle* handle, unsigned max_threads, const char* master_host);
void stop_cluster_client(struct ClusterClientHandle* handle);

<<<<<<< HEAD
//-----------------------------
// Computation task management
//-----------------------------

client_compute(struct ClusterClientHandle* handle, size_t num_threads, size_t task_size, size_t ret_size);

#endif // COMPUTING_CLUSTER_CLIENT_HPP_INCLUDED
=======
#endif // COMPUTING_CLUSTER_CLIENT_HPP_INCLUDED
>>>>>>> a87e10088412ba24be19c38f9f9e14772993429a
