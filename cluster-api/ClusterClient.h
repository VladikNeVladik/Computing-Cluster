// No Copyright. Vladislav Aleinik && Maxim Davydov 2020
//==========================================================
// Computing Cluster Client
//==========================================================
// - Performs time-to-time "Where are you, server?" sendout
// - Connects to computation cluster
// - Performs computations
// - Sends results back
//=========================================================
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
#include <sys/sysinfo.h>
#include <sys/eventfd.h>

// Bool:
typedef char bool;

struct Connection
{
	int  socket_fd;

	// Recv buffer:
	char*  recv_buffer;
	size_t bytes_recieved;
};

struct ComputeInfo
{
	void* data_pack;
	void* ret_pack;
};

struct ThreadInfo
{
	// Entry emptiness:
	bool empty;

	// What to compute:
	struct ComputeInfo in_args;
	void* (*computation_func)(void*);
	
	// How to compute
	pthread_t thread_id;
	cpu_set_t cpu;
	uint32_t  task_id;

	// How to signal the completion:
    int event_fd;
	bool waiting_to_be_sent;
};

enum Errors
{
    E_ERROR = -1,
    E_CACHE_INFO = -2,
    E_BADARGS = -3,
};

struct ClusterClientHandle
{
	// Eventloop:
	int epoll_fd;
	pthread_t eventloop_thr_id;
	
	// Server discovery:
	const char* server_hostname;
	bool local_discovery;

	int discovery_socket_fd;
	int discovery_timer_fd;

	struct sockaddr_in broadcast_addr;

	// Connection management:
	struct Connection server_conn;
	struct sockaddr_in server_addr;

	// Task management:
	size_t initial_requests;
	
	struct ThreadInfo* thread_manager;
	size_t max_threads;
	
	void*  task_buffer;
	size_t task_size;
	
	void*  ret_buffer;
	size_t ret_size;

	// Task computation:
	int max_cpu;
    int cache_line_size;
	void* (*computation_func)(void*);
};

//-------------
// Computation
//-------------

void client_compute(size_t num_threads, size_t task_size, size_t ret_size, void* (*thread_func)(void*), const char* master_host);

#endif // COMPUTING_CLUSTER_CLIENT_HPP_INCLUDED
