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
// Memcmp:
#include <string.h>

// Cluster Client API:
#include "ClusterClient.h"

// System Timing Specification:
#include "Timeouts.h"

//-----------------
// Server Tracking 
//-----------------

static void init_server_tracking_routine(struct ClusterClientHandle* handle)
{
	static const int PORT = 9787;

	BUG_ON(handle == NULL, "[init_server_tracking_routine] Nullptr argument");

	int sock_fd = socket(AF_INET, SOCK_DGRAM|SOCK_NONBLOCK, 0);
	if (sock_fd == -1)
	{
		LOG_ERROR("[init_server_tracking_routine] socket() failed");
		exit(EXIT_FAILURE);
	}

	uint64_t setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[init_server_tracking_routine] setsockopt() failed");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in broadcast_addr =
	{
		.sin_family      = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_BROADCAST),
		.sin_port        = htons(PORT)
	};

	if (bind(sock_fd, &broadcast_addr, sizeof(broadcast_addr)) == -1)
	{
		LOG_ERROR("[init_server_tracking_routine] Unable to bind()");
		exit(EXIT_FAILURE);
	}

	handle->server_tracking_socket_fd = sock_fd;

	// Create (disarmed) timer:
	int timer_fd = timerfd_create(CLOCK_BOOTTIME, TFD_NONBLOCK|TFD_CLOEXEC);
	if (timer_fd == -1)
	{
		LOG_ERROR("[init_server_tracking_routine] Unable to create timer file descriptor");
		exit(EXIT_FAILURE);
	}

	struct itimerspec timer_config =
	{
		.it_interval = {0, 0}, 
		.it_value    = {0, 0}
	};
	if (timerfd_settime(timer_fd, 0, &timer_config, NULL) == -1)
	{
		LOG_ERROR("[init_server_tracking_routine] Unable to configure a timer file descriptor");
		exit(EXIT_FAILURE);
	}

	handle->server_tracking_timeout_fd = timer_fd;

	// Log:
	LOG("[CLUSTER-CLIENT] Server tracking routine initialized");
}

static void free_server_tracking_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[free_server_tracking_routine] Nullptr argument");

	if (close(handle->server_tracking_socket_fd) == -1)
	{
		LOG_ERROR("[free_server_tracking_routine] Unable to close socket");
		exit(EXIT_FAILURE);
	}

	if (close(handle->server_tracking_timeout_fd) == -1)
	{
		LOG_ERROR("[free_server_tracking_routine] Unable to close timer-assosiated file descriptor");
		exit(EXIT_FAILURE);
	}

	handle->server_tracking_socket_fd  = -1;
	handle->server_tracking_timeout_fd = -1;

	// Log:
	LOG("[CLUSTER-CLIENT] Server tracking routine resources freed");
}

static void start_server_tracking_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[start_server_tracking_routine] Nullptr argument");

	// Add the server tracking socket to epoll:
	epoll_data_t event_data = 
	{
		.fd = handle->server_tracking_socket_fd
	};
	struct epoll_event event_config = 
	{
		.events = EPOLLIN,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->server_tracking_socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_server_tracking_routine] Unable to register socket file descriptor for epoll");
		exit(EXIT_FAILURE);
	}

	// Add the timer to epoll:
	event_data.fd       = handle->server_tracking_timeout_fd;
	event_config.events = EPOLLIN;
	event_config.data   = event_data;
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->server_tracking_timeout_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_server_tracking_routine] Unable to register timer file descriptor for epoll");
		exit(EXIT_FAILURE);
	}

	// Arm the timer:
	struct itimerspec timer_config =
	{
		.it_interval = {                                         0, 0}, 
		.it_value    = {TIMEOUT_NO_DISCOVERY_DATAGRAMS_FROM_SERVER, 0}
	};
	if (timerfd_settime(handle->server_tracking_timeout_fd, 0, &timer_config, NULL) == -1)
	{
		LOG_ERROR("[start_server_tracking_routine] Unable to configure a timer file descriptor");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("[CLUSTER-CLIENT] Server tracking routine running");
}

// static void pause_server_tracking_routine(struct ClusterClientHandle* handle)
// {
// 	BUG_ON(handle == NULL, "[pause_server_tracking_routine] Nullptr argument");

// 	// Disarm the timer:
// 	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_DEL, handle->server_tracking_timeout_fd, NULL) == -1)
// 	{
// 		LOG_ERROR("[pause_server_tracking_routine] Unable to unregister timer file descriptor from epoll");
// 		exit(EXIT_FAILURE);
// 	}	

// 	// Delete the socket from epoll:
// 	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_DEL, handle->server_tracking_socket_fd, NULL) == -1)
// 	{
// 		LOG_ERROR("[pause_server_tracking_routine] Unable to unregister socket file descriptor from epoll");
// 		exit(EXIT_FAILURE);
// 	}

// 	// Log:
// 	LOG("[CLUSTER-CLIENT] Server tracking routine paused");
// }

static const int DISCOVERY_DATAGRAM_SIZE = 16;

static void catch_server_discovery_datagram(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[catch_server_discovery_datagram] Nullptr argument");

	struct sockaddr_in peer_addr;
	socklen_t peer_addr_len = sizeof(peer_addr);
	char buffer[DISCOVERY_DATAGRAM_SIZE];

	int bytes_read;
	do
	{
		bytes_read = recvfrom(handle->server_tracking_socket_fd, buffer, DISCOVERY_DATAGRAM_SIZE, 0, (struct sockaddr*) &peer_addr, &peer_addr_len);
		if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			LOG_ERROR("[catch_server_discovery_datagram] Unable to recieve discovery datagram (errno = %d)", errno);
			exit(EXIT_FAILURE);
		}

		// Restart timer:
		if (bytes_read == DISCOVERY_DATAGRAM_SIZE && bcmp(&peer_addr, &handle->server_addr, peer_addr_len) == 0)
		{
			struct itimerspec timer_config =
			{
				.it_interval = {                                         0, 0},
				.it_value    = {TIMEOUT_NO_DISCOVERY_DATAGRAMS_FROM_SERVER, 0}
			};
			if (timerfd_settime(handle->server_tracking_timeout_fd, 0, &timer_config, NULL) == -1)
			{
				LOG_ERROR("[catch_server_discovery_datagram] Unable to configure a timer file descriptor");
				exit(EXIT_FAILURE);
			}

			break;
		}
	}
	while (bytes_read != -1);
}

//-------------------
// Discovery process 
//-------------------

static void discover_server(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[discover_server_local] Nullptr argument");

	// Wait for incomping datagram:
	LOG("[CLUSTER-CLIENT] Waiting for discovery datagram");

	struct sockaddr_in peer_addr;
	socklen_t peer_addr_len = sizeof(peer_addr);
	char buffer[DISCOVERY_DATAGRAM_SIZE];

	int bytes_read;
	do
	{
		bytes_read = recvfrom(handle->server_tracking_socket_fd, buffer, DISCOVERY_DATAGRAM_SIZE, 0, (struct sockaddr*) &peer_addr, &peer_addr_len);
		if (bytes_read == -1)
		{
			LOG_ERROR("[discover_server_local] Unable to recieve discovery datagram (errno = %d)", errno);
			exit(EXIT_FAILURE);
		}
	}
	while (bytes_read != DISCOVERY_DATAGRAM_SIZE);

	// Save server address:
	handle->server_addr = peer_addr;

	// Switch socket into nonblocking mode:
	if (fcntl(handle->server_tracking_socket_fd, F_SETFD, O_NONBLOCK) == -1)
	{
		LOG_ERROR("[discover_server_local] Unable to switch socket into non-blocking mode");
		exit(EXIT_FAILURE);
	}

	// Log discovery:
	char server_host[32];
	char server_port[32];
	if (getnameinfo((struct sockaddr*) &handle->server_addr, sizeof(handle->server_addr),
		server_host, 32, server_port, 32, NI_NUMERICHOST|NI_NUMERICSERV) != 0)
	{
		LOG_ERROR("[discover_server_local] Unable to call getnameinfo()");
		exit(EXIT_FAILURE);
	}

	LOG("[CLUSTER-CLIENT] Automatically discovered cluster-server at %s:%s", server_host, server_port);

	// Start tracking server discovery datagrams to drop all tasks in case server dies:
	start_server_tracking_routine(handle);
}

//-------------------------------
// Connection Management Routine 
//-------------------------------

static void init_connection_management_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[init_connection_management_routine] Nullptr argument");

	// Acquire socket:
	int sock_fd = -1;
	if (handle->local_discovery)
	{
		sock_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
		if (sock_fd == -1)
		{
			LOG_ERROR("[init_connection_management_routine] Unable to call socket()");
			exit(EXIT_FAILURE);
		}

		uint8_t setsockopt_yes = 1;
		if (setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
		{
			LOG_ERROR("[init_connection_management_routine] Unable to call setsockopt()");
			exit(EXIT_FAILURE);
		}


	} 
}

static void free_connection_management_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[free_connection_management_routine] Nullptr argument");

	
}

static void start_connection_management_routine(struct ClusterClientHandle* handle) {}
static void pause_connection_management_routine(struct ClusterClientHandle* handle) {}

//-----------------------------
// Computation task management 
//-----------------------------

static void init_task_computing_routine(struct ClusterClientHandle* handle) {}
static void free_task_computing_routine(struct ClusterClientHandle* handle) {}

// static void start_task_computing_routine(struct ClusterClientHandle* handle) {}
// static void pause_task_computing_routine(struct ClusterClientHandle* handle) {}

//------------------
// Client Eventloop 
//------------------

static void* client_eventloop(void* arg)
{
	static const int MAX_EVENTS = 16;

	struct ClusterClientHandle* handle = arg;
	BUG_ON(handle == NULL, "[client_eventloop] Nullptr argument");

	struct epoll_event pending_events[MAX_EVENTS];
	while (1)
	{
		int num_events = epoll_wait(handle->epoll_fd, pending_events, MAX_EVENTS, -1);
		if (num_events == -1)
		{
			LOG_ERROR("[client_eventloop] Failure in epoll_wait()");
			exit(EXIT_FAILURE);
		}

		for (int ev = 0; ev < num_events; ++ev)
		{
			// Track server datagrams:
			if (pending_events[ev].data.fd == handle->server_tracking_socket_fd)
			{
				catch_server_discovery_datagram(handle);
			}

			// Handle "No Discovery Datagrams" Timeout:
			if (pending_events[ev].data.fd == handle->server_tracking_timeout_fd)
			{
				LOG("[CLUSTER-CLIENT] No discovery datagrams recieved in a while. Quitting.");
				exit(EXIT_SUCCESS);
			}
		}
	}

	return NULL;
}


//-------------------------------------
// Initialization and deinitialization 
//-------------------------------------

void init_cluster_client(struct ClusterClientHandle* handle, unsigned max_threads, const char* master_host)
{
	BUG_ON(handle == NULL, "[init_cluster_client] Nullptr argument");

	// Save desired client configuration:
	handle->max_threads = max_threads;
	handle->local_discovery = (master_host == NULL);
	handle->server_hostname = master_host;

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
	if (!handle->local_discovery)
	{
		init_server_tracking_routine(handle);
	}

	init_task_computing_routine(handle);

	// Perform discovery:
	if (handle->local_discovery)
	{
		discover_server(handle);
	}

	// Perform login procedure:


	// Log:
	LOG("[CLUSTER-CLIENT] Cluster-client initialized");
}

void stop_cluster_client(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[stop_cluster_client] Nullptr argument");

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
	free_server_tracking_routine(handle);
	free_task_computing_routine (handle);

	// Log:
	LOG("[CLUSTER-CLIENT] Cluster-client stopped");
}
