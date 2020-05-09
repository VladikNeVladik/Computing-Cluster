// No Copyright. Vladislav Aleinik 2020
//=========================================
// Computing Cluster Server Implementation
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
// Timer-fd:
#include <sys/timerfd.h>
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

// Cluster Server API:
#include "ClusterServer.h"

// System Timing Specification:
#include "Timeouts.h"

//-------------------
// Discovery process 
//-------------------

static void init_discovery_routine(struct ClusterServerHandle* handle)
{
	static const int PORT = 9787;

	BUG_ON(handle == NULL, "[init_discovery_routine] Nullptr argument");

	// Acquire discovery socket:
	int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_fd == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to create socket");
		exit(EXIT_FAILURE);
	}

	uint8_t setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to call setsockopt()");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in broadcast_addr =
	{
		.sin_family      = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_BROADCAST),
		.sin_port        = htons(PORT)
	};

	if (connect(sock_fd, &broadcast_addr, sizeof(broadcast_addr)) == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to connect to broadcast address");
		exit(EXIT_FAILURE);
	}

	handle->discovery_socket_fd = sock_fd;

	// Set timer:
	int timer_fd = timerfd_create(CLOCK_BOOTTIME, TFD_NONBLOCK|TFD_CLOEXEC);
	if (timer_fd == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to create timer file descriptor");
		exit(EXIT_FAILURE);
	}

	struct itimerspec timer_config =
	{
		.it_interval = {5, 0},
		.it_value    = {1, 0}
	};
	if (timerfd_settime(timer_fd, 0, &timer_config, NULL) == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to configure a timer file descriptor");
		exit(EXIT_FAILURE);
	}

	handle->discovery_timer_fd = timer_fd;

	// Log:
	LOG("[CLUSTER-SERVER] Discovery routine initialized");
}

static void free_discovery_routine(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[free_discovery_routine] Nullptr argument");

	if (close(handle->discovery_socket_fd) == -1)
	{
		LOG_ERROR("[free_discovery_routine] Unable to close socket");
		exit(EXIT_FAILURE);
	}

	if (close(handle->discovery_timer_fd) == -1)
	{
		LOG_ERROR("[free_discovery_routine] Unable to close timer-assosiated file descriptor");
		exit(EXIT_FAILURE);
	}

	handle->discovery_socket_fd = -1;
	handle->discovery_timer_fd  = -1;

	// Log:
	LOG("[CLUSTER-SERVER] Discovery routine resources freed");
}

void start_discovery_routine(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[start_discovery_routine] Nullptr argument");

	epoll_data_t event_data = 
	{
		.fd = handle->discovery_timer_fd
	};
	struct epoll_event event_config =
	{
		.events = EPOLLIN,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->discovery_timer_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_discovery_routine] Unable to register file descriptor for epoll");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("[CLUSTER-SERVER] Discovery routine started");
}

void pause_discovery_routine(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[pause_discovery_routine] Nullptr argument");

	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_DEL, handle->discovery_timer_fd, NULL) == -1)
	{
		LOG_ERROR("[start_discovery_routine] Unable to register file descriptor for epoll");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("[CLUSTER-SERVER] Discovery routine paused");
}

void perform_discovery_send(struct ClusterServerHandle* handle)
{
	static const int DATAGRAM_SIZE = 16;

	BUG_ON(handle == NULL, "[perform_discovery_send] Nullptr argument");

	char send_buffer[DATAGRAM_SIZE];
	int bytes_written = write(handle->discovery_socket_fd, send_buffer, 16);
	if (bytes_written == -1)
	{
		if (errno == ECONNREFUSED)
		{
			LOG("[CLUSTER-SERVER] No clients detected by a discovery datagram!");
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			LOG_ERROR("[perform_discovery_send] Unable to broadcast discovery datagram");
			exit(EXIT_FAILURE);
		}
		else
		{
			LOG_ERROR("[perform_discovery_send] Unable to send data");
			exit(EXIT_FAILURE);
		}
	}

	LOG("[CLUSTER-SERVER] Sent discovery datagram");
}

//----------------------
// Still-alive tracking
//----------------------

static void init_still_alive_tracking_routine(struct ClusterServerHandle* handle) {}
static void free_still_alive_tracking_routine(struct ClusterServerHandle* handle) {}

void start_still_alive_tracking_routine(struct ClusterServerHandle* handle) {}
void pause_still_alive_tracking_routine(struct ClusterServerHandle* handle) {}

//-----------------------------
// Computation task management 
//-----------------------------

static void init_task_tracking_routine(struct ClusterServerHandle* handle) {}
static void free_task_tracking_routine(struct ClusterServerHandle* handle) {}

void start_task_tracking_routine(struct ClusterServerHandle* handle) {}
void pause_task_tracking_routine(struct ClusterServerHandle* handle) {}

void add_computation_tasks  (struct ClusterServerHandle* handle) {}
void get_computation_results(struct ClusterServerHandle* handle) {}

//------------------
// Server Eventloop 
//------------------

static void* server_eventloop(void* arg)
{
	static const int MAX_EVENTS = 16;

	struct ClusterServerHandle* handle = arg;
	BUG_ON(handle == NULL, "[server_eventloop] Nullptr argument");

	struct epoll_event pending_events[MAX_EVENTS];
	while (1)
	{
		int num_events = epoll_wait(handle->epoll_fd, pending_events, MAX_EVENTS, -1);
		if (num_events == -1)
		{
			LOG_ERROR("[server_eventloop] Failure in epoll_wait()");
			exit(EXIT_FAILURE);
		}

		for (int ev = 0; ev < num_events; ++ev)
		{
			// Discovery process:
			if (pending_events[ev].data.fd == handle->discovery_timer_fd)
			{
				perform_discovery_send(handle);

				uint64_t timer_expirations = 0;
				if (read(handle->discovery_timer_fd, &timer_expirations, 8) == -1)
				{
					LOG_ERROR("[server_eventloop] Unable to perform read on timer file descriptor");
					exit(EXIT_FAILURE);
				}
			}
		}
	}

	return NULL;
}

//-------------------------------------
// Initialization and deinitialization 
//-------------------------------------

void init_cluster_server(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[init_cluster_server] Nullptr argument");

	// Create epoll instance:
	handle->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (handle->epoll_fd == -1)
	{
		LOG_ERROR("[init_cluster_server] epoll_create1() failed");
		exit(EXIT_FAILURE);
	}

	// Start eventloop:
	int err = pthread_create(&handle->eventloop_thr_id, NULL, server_eventloop, (void*) handle);
	if (err != 0)
	{
		LOG_ERROR("[init_cluster_server] pthread_create() failed with error %d", err);
		exit(EXIT_FAILURE);
	}

	// Discovery:
	handle->discovery_socket_fd = -1;
	handle->discovery_timer_fd  = -1;

	// Init subroutines:
	init_discovery_routine           (handle);
	init_still_alive_tracking_routine(handle);
	init_task_tracking_routine       (handle);

	// Start discovery:
	start_discovery_routine(handle);

	// Log:
	LOG("[CLUSTER-SERVER] Cluster-server initialized");
}

void stop_cluster_server(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[stop_cluster_server] Nullptr argument");

	// Stop eventloop:
	int err = pthread_cancel(handle->eventloop_thr_id);
	if (err != 0)
	{
		LOG_ERROR("[stop_cluster_server] pthread_cancel() failed with error %d", err);
		exit(EXIT_FAILURE);
	}

	err = pthread_join(handle->eventloop_thr_id, NULL);
	if (err != 0)
	{
		LOG_ERROR("[stop_cluster_server] pthread_join() failed with error %d", err);
		exit(EXIT_FAILURE);
	}

	// (After this point all start/pause routine actions will fail with error)
	if (close(handle->epoll_fd) == -1)
	{
		LOG_ERROR("[stop_cluster_server] Unable to close() epoll file descriptor");
		exit(EXIT_FAILURE);
	}

	handle->epoll_fd = -1;

	// Free resources allocated for subroutines:
	free_discovery_routine           (handle);
	free_still_alive_tracking_routine(handle);
	free_task_tracking_routine       (handle);

	// Log:
	LOG("[CLUSTER-SERVER] Cluster-server stopped");
}
