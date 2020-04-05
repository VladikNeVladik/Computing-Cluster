// No Copyright. Vladislav Aleinik 2020
//=========================================
// Computing Cluster Client Implementation
//=========================================
// Feature test macros:
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// Epoll:
#include <sys/epoll.h>
// Sockets:
#include <sys/types.h>
#include <sys/socket.h>
// Getaddrinfo:
#include <netdb.h>
// Multithreading:
#include <pthread.h>
// Read/Write:
#include <unistd.h>
// Errno:
#include <errno.h>

#include "ClusterClient.h"

//-------------------------------------
// Initialization and deinitialization 
//-------------------------------------

// Required predeclarations:
static void init_still_alive_routine(struct ClusterClientHandle* handle);
static void free_still_alive_routine(struct ClusterClientHandle* handle);

static void init_task_computing_routine(struct ClusterClientHandle* handle);
static void free_task_computing_routine(struct ClusterClientHandle* handle);

static void* client_eventloop(void* arg);

void init_cluster_client(struct ClusterClientHandle* handle)
{
	if (handle == NULL)
	{
		LOG_ERROR("[init_cluster_client] Nullptr argument");
		exit(EXIT_FAILURE);
	}

	// Create epoll instance:
	handle->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (handle->epoll_fd == -1)
	{
		LOG_ERROR("[init_cluster_client] epoll_create1() failed");
		exit(EXIT_FAILURE);
	}

	// Start eventloop:
	int err = pthread_create(&handle->eventloop_thr_id, NULL, client_eventloop, (void*) handle);
	if (err != 0)
	{
		LOG_ERROR("[init_cluster_client] pthread_create() failed with error %d", err);
		exit(EXIT_FAILURE);
	}

	// Init subroutines:
	init_still_alive_routine   (handle);
	init_task_computing_routine(handle);

	// Discovery:
	// Log:
	LOG("[CLUSTER-CLIENT] Cluster-client initialized");
}

void stop_cluster_client(struct ClusterClientHandle* handle)
{
	if (handle == NULL)
	{
		LOG_ERROR("[stop_cluster_client] Nullptr argument");
		exit(EXIT_FAILURE);
	}

	// Stop eventloop:
	int err = pthread_cancel(handle->eventloop_thr_id);
	if (err != 0)
	{
		LOG_ERROR("[stop_cluster_client] pthread_cancel() failed with error %d", err);
		exit(EXIT_FAILURE);
	}

	err = pthread_join(handle->eventloop_thr_id, NULL);
	if (err != 0)
	{
		LOG_ERROR("[stop_cluster_client] pthread_join() failed with error %d", err);
		exit(EXIT_FAILURE);
	}

	if (close(handle->epoll_fd) == -1)
	{
		LOG_ERROR("[stop_cluster_client] Unable to close() epoll file descriptor");
		exit(EXIT_FAILURE);
	}

	// Free resources allocated for subroutines:
	free_still_alive_routine   (handle);
	free_task_computing_routine(handle);

	// Log:
	LOG("[CLUSTER-CLIENT] Cluster-client stopped");
}

//-------------------
// Discovery process 
//-------------------

void discover_server(struct ClusterClientHandle* handle)
{
	static const int DATAGRAM_SIZE = 16;
	static const int PORT = 20000;

	if (handle == NULL)
	{
		LOG_ERROR("[discover_server] Nullptr argument");
		exit(EXIT_FAILURE);
	}

	// Acquire discovery socket:
	int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_fd == -1)
	{
		LOG_ERROR("[discover_server] Unable to create socket");
		exit(EXIT_FAILURE);
	}

	uint64_t setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[discover_server] Unable to call setsockopt()");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in broadcast_addr =
	{
		.sin_family      = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_ANY),
		.sin_port        = htons(PORT)
	};

	if (bind(sock_fd, &broadcast_addr, sizeof(broadcast_addr)) == -1)
	{
		LOG_ERROR("[discover_server] Unable to bind()");
		exit(EXIT_FAILURE);
	}

	// Wait for incomping datagram:
	LOG("[CLUSTER-CLIENT] Waiting for discovery datagram");

	struct sockaddr_in peer_addr;
	socklen_t peer_addr_len = sizeof(peer_addr);

	char buffer[DATAGRAM_SIZE];

	int bytes_read = recvfrom(sock_fd, buffer, DATAGRAM_SIZE, 0, (struct sockaddr*) &peer_addr, &peer_addr_len);
	if (bytes_read == -1)
	{
		LOG_ERROR("[discover_server] Unable to recieve discovery datagram (errno = %d)", errno);
		exit(EXIT_FAILURE);
	}

	handle->server_addr = peer_addr;

	char server_host[32];
	char server_port[32];
	if (getnameinfo((struct sockaddr*) &handle->server_addr, sizeof(handle->server_addr),
		server_host, 32, server_port, 32, NI_NUMERICHOST|NI_NUMERICSERV) != 0)
	{
		LOG_ERROR("[discover_server] Unable to call getnameinfo()");
		exit(EXIT_FAILURE);
	}

	LOG("[CLUSTER-CLIENT] Discovered cluster-server at ip %s-%s", server_host, server_port);
}

void forget_server(struct ClusterClientHandle* handle) {}

//-----------------
// Login precedure
//-----------------

void login_to_server(struct ClusterClientHandle* handle) {}

//---------------------
// Still-alive process
//---------------------

static void init_still_alive_routine(struct ClusterClientHandle* handle) {}
static void free_still_alive_routine(struct ClusterClientHandle* handle) {}

void start_still_alive_routine(struct ClusterClientHandle* handle) {}
void pause_still_alive_routine(struct ClusterClientHandle* handle) {}

//-----------------------------
// Computation task management 
//-----------------------------

static void init_task_computing_routine(struct ClusterClientHandle* handle) {}
static void free_task_computing_routine(struct ClusterClientHandle* handle) {}

void start_task_computing_routine(struct ClusterClientHandle* handle) {}
void pause_task_computing_routine(struct ClusterClientHandle* handle) {}

void set_maximum_load(struct ClusterClientHandle* handle) {}

//------------------
// Client Eventloop 
//------------------

static void* client_eventloop(void* arg)
{
	static const int MAX_EVENTS = 16;

	struct ClusterClientHandle* handle = arg;

	struct epoll_event pending_events[MAX_EVENTS];
	while (1)
	{
		int num_events = epoll_wait(handle->epoll_fd, pending_events, MAX_EVENTS, -1);
		if (num_events == -1)
		{
			LOG_ERROR("[client_eventloop] Failure in epoll_wait()");
			exit(EXIT_FAILURE);
		}

		for (int ev = 0; ev < num_events; ++ev) {}
	}

	return NULL;
}