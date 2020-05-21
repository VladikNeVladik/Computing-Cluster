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
// TCP keepalive options:
#include <netinet/in.h>
#include <netinet/tcp.h>
// Multithreading:
#include <pthread.h>
// Read/Write:
#include <unistd.h>
// Errno:
#include <errno.h>
// Memcmp:
#include <string.h>
// Calloc:
#include <stdlib.h>
// Inet_addr:
#include <arpa/inet.h>

// Cluster Client API:
#include "ClusterClient.h"

// System Configuration:
#include "Config.h"

//-------------------
// Discovery Routine
//-------------------

static void init_discovery_routine(struct ClusterClientHandle* handle)
{
	if (!handle->local_discovery)
	{
		LOG("Discovery routine initialized (distant discoverymode)");
		return;
	}

	int sock_fd = socket(AF_INET, SOCK_DGRAM|SOCK_NONBLOCK, 0);
	if (sock_fd == -1)
	{
		LOG_ERROR("[init_discovery_routine] socket() failed");
		exit(EXIT_FAILURE);
	}

	uint64_t setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[init_discovery_routine] setsockopt() failed");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in broadcast_addr =
	{
		.sin_family      = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_BROADCAST),
		.sin_port        = htons(DISCOVERY_PORT)
	};

	if (connect(sock_fd, &broadcast_addr, sizeof(broadcast_addr)) == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to connect()");
		exit(EXIT_FAILURE);
	}

	handle->discovery_socket_fd = sock_fd;

	// Create (disarmed) timer:
	int timer_fd = timerfd_create(CLOCK_BOOTTIME, TFD_NONBLOCK|TFD_CLOEXEC);
	if (timer_fd == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to create timer file descriptor");
		exit(EXIT_FAILURE);
	}

	struct itimerspec timer_config =
	{
		.it_interval = {0, 0},
		.it_value    = {0, 0}
	};
	if (timerfd_settime(timer_fd, 0, &timer_config, NULL) == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to configure a timer file descriptor");
		exit(EXIT_FAILURE);
	}

	handle->discovery_timer_fd = timer_fd;

	// Log:
	LOG("Discovery routine initialized (local discovery mode)");
}

static void free_discovery_routine(struct ClusterClientHandle* handle)
{
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

	// Log:
	LOG("Discovery routine resources freed");
}

static void start_discovery_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(!handle->local_discovery, "[start_discovery_routine] Unneeded phase for distant discovery");

	// Add the discovery socket to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->discovery_socket_fd
	};
	struct epoll_event event_config =
	{
		.events = EPOLLIN,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->discovery_socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_discovery_routine] Unable to register socket file descriptor for epoll");
		exit(EXIT_FAILURE);
	}

	// Add the timer to epoll:
	event_data.fd       = handle->discovery_timer_fd;
	event_config.events = EPOLLIN;
	event_config.data   = event_data;
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->discovery_timer_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_discovery_routine] Unable to register timer file descriptor for epoll");
		exit(EXIT_FAILURE);
	}

	// Arm the timer:
	struct itimerspec timer_config =
	{
		.it_interval = {DISCOVERY_REPEAT_INTERVAL, 0},
		.it_value    = {                        1, 0}
	};
	if (timerfd_settime(handle->discovery_timer_fd, 0, &timer_config, NULL) == -1)
	{
		LOG_ERROR("[start_discovery_routine] Unable to configure a timer file descriptor");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("Discovery routine running");
}

static void pause_discovery_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(!handle->local_discovery, "[pause_discovery_routine] Unneeded phase for distant discovery");

	// Delete the discovery socket from epoll:
	struct epoll_event event_config;
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_DEL, handle->discovery_socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[pause_discovery_routine] Unable to delet socket file descriptor from epoll");
		exit(EXIT_FAILURE);
	}

	// Delete the timer from epoll:
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_DEL, handle->discovery_timer_fd, &event_config) == -1)
	{
		LOG_ERROR("[pause_discovery_routine] Unable to delete timer file descriptor from epoll");
		exit(EXIT_FAILURE);
	}

	// Disarm the timer:
	struct itimerspec timer_config =
	{
		.it_interval = {0, 0},
		.it_value    = {0, 0}
	};
	if (timerfd_settime(handle->discovery_timer_fd, 0, &timer_config, NULL) == -1)
	{
		LOG_ERROR("[pause_discovery_routine] Unable to configure a timer file descriptor");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("Discovery routine paused");
}

static void perform_discovery_send(struct ClusterClientHandle* handle)
{
	BUG_ON(!handle->local_discovery, "[pause_discovery_routine] Unneeded phase for distant discovery");

	int bytes_written = send(handle->discovery_socket_fd, CLIENTS_DISCOVERY_DATAGRAM, CLIENTS_DISCOVERY_DATAGRAM_SIZE, MSG_NOSIGNAL);
	if (bytes_written != CLIENTS_DISCOVERY_DATAGRAM_SIZE)
	{
		LOG_ERROR("[perform_discovery_send] Unable to broadcast discovery datagram");
		exit(EXIT_FAILURE);
	}

	uint64_t timer_expirations = 0;
	if (read(handle->discovery_timer_fd, &timer_expirations, 8) == -1)
	{
		LOG_ERROR("[perform_discovery_send] Unable to perform read on timer file descriptor");
		exit(EXIT_FAILURE);
	}

	LOG("Sent discovery datagram");
}

// Prototype reqired by catch_servers_discovery_datagram()
static void start_connection_management_routine(struct ClusterClientHandle* handle);

static void catch_servers_discovery_datagram(struct ClusterClientHandle* handle)
{
	BUG_ON(!handle->local_discovery, "[pause_discovery_routine] Unneeded phase for distant discovery");

	struct sockaddr_in peer_addr;
	socklen_t peer_addr_len = sizeof(peer_addr);
	char buffer[SERVERS_DISCOVERY_DATAGRAM_SIZE];

	int bytes_read;
	do
	{
		bytes_read = recvfrom(handle->discovery_socket_fd, buffer, SERVERS_DISCOVERY_DATAGRAM_SIZE,
		                      0, (struct sockaddr*) &peer_addr, &peer_addr_len);
		if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			LOG_ERROR("[catch_servers_discovery_datagram] Unable to recieve discovery datagram (errno = %d)", errno);
			exit(EXIT_FAILURE);
		}

		buffer[SERVERS_DISCOVERY_DATAGRAM_SIZE - 1] = '\0';

		// Restart timer:
		if (bytes_read == SERVERS_DISCOVERY_DATAGRAM_SIZE && strcmp(buffer, SERVERS_DISCOVERY_DATAGRAM) == 0)
		{
			// Save server address:
			handle->server_addr = peer_addr;

			// Log discovery:
			char server_host[32];
			char server_port[32];
			if (getnameinfo((struct sockaddr*) &peer_addr, sizeof(peer_addr),
				server_host, 32, server_port, 32, NI_NUMERICHOST|NI_NUMERICSERV) != 0)
			{
				LOG_ERROR("[discover_server] Unable to call getnameinfo()");
				exit(EXIT_FAILURE);
			}

			LOG("Automatically discovered cluster-server at %s:%s", server_host, server_port);

			// Connect to server:
			start_connection_management_routine(handle);

			// Pause discovery:
			pause_discovery_routine(handle);

			return;
		}
	}
	while (bytes_read != -1);
}

//-------------------------------
// Connection Management Routine
//-------------------------------

static void init_connection_management_routine(struct ClusterClientHandle* handle)
{
	handle->server_conn.recv_buffer = (char*) calloc(handle->task_size + sizeof(size_t), sizeof(char));
	if (handle->server_conn.recv_buffer == NULL)
	{
		LOG_ERROR("[init_cluster_client] Unable to allocate recv-buffer");
		exit(EXIT_FAILURE);
	}

	handle->server_conn.bytes_recieved = 0;

	// Log:
	LOG("Connection management routine initialized");
}

static void free_connection_management_routine(struct ClusterClientHandle* handle)
{
	if (close(handle->server_conn.socket_fd) == -1)
	{
		LOG_ERROR("[free_connection_management_routine] Unable to free recv-buffer");
		exit(EXIT_FAILURE);
	}

	free(handle->server_conn.recv_buffer);

	// Log:
	LOG("Connection management routine resources freed");
}

static void start_connection_management_routine(struct ClusterClientHandle* handle)
{
	// Connect to peer:
	if (handle->local_discovery)
	{
		// Acquire connection socket:
		int sock_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, IPPROTO_TCP);
		if (sock_fd == -1)
		{
			LOG_ERROR("[start_connection_management_routine] Unable to create socket");
			exit(EXIT_FAILURE);
		}
		
		if (connect(sock_fd, (struct sockaddr*) &handle->server_addr, sizeof(handle->server_addr)) == -1)
		{
			LOG_ERROR("[start_connection_management_routine] Unable to connect to server");
			exit(EXIT_FAILURE);
		}

		handle->server_conn.socket_fd = sock_fd;
	}
	else
	{
		struct addrinfo hints;
		memset(&hints, 0, sizeof(struct addrinfo));
		hints.ai_family    = AF_INET;
		hints.ai_socktype  = SOCK_STREAM|SOCK_NONBLOCK;
		hints.ai_flags     = 0;
		hints.ai_protocol  = 0;
		hints.ai_canonname = NULL;
		hints.ai_addr      = NULL;
		hints.ai_next      = NULL;

		struct addrinfo* result;
		if (getaddrinfo(handle->server_hostname, CONNECTION_PORT_STR, &hints, &result) != 0)
		{
			LOG_ERROR("[start_connection_management_routine] Unable to call getaddrinfo()");
			exit(EXIT_FAILURE);
		}
		
		int sock_fd = -1;
		for (struct addrinfo* addr = result; addr != NULL; addr = addr->ai_next)
		{
			// Initialize socket:
			sock_fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
			if (sock_fd == -1)
			{
				continue;
			}

			if (connect(sock_fd, addr->ai_addr, addr->ai_addrlen) == -1)
			{
				close(sock_fd);
				sock_fd = -1;
				continue;
			}

			break;
		}

		if (sock_fd == -1)
		{
			LOG_ERROR("[start_connection_management_routine] Unable to aquire socket via getaddrinfo()");
			exit(EXIT_FAILURE);
		}

		handle->server_conn.socket_fd = sock_fd;
	}

	// Ask socket to automatically detect disconnection:
	int setsockopt_yes = 1;
	if (setsockopt(handle->server_conn.socket_fd, SOL_SOCKET, SO_KEEPALIVE, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to set SO_KEEPALIVE socket option");
		exit(EXIT_FAILURE);
	}

	int setsockopt_arg = TCP_KEEPALIVE_IDLE_TIME;
	if (setsockopt(handle->server_conn.socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to set TCP_KEEPIDLE socket option");
		exit(EXIT_FAILURE);
	}

	setsockopt_arg = TCP_KEEPALIVE_INTERVAL;
	if (setsockopt(handle->server_conn.socket_fd, IPPROTO_TCP, TCP_KEEPINTVL, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to set TCP_KEEPINTVL socket option");
		exit(EXIT_FAILURE);
	}

	setsockopt_arg = TCP_KEEPALIVE_NUM_PROBES;
	if (setsockopt(handle->server_conn.socket_fd, IPPROTO_TCP, TCP_KEEPCNT, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to set TCP_KEEPCNT socket option");
		exit(EXIT_FAILURE);
	}

	// Add socket to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->server_conn.socket_fd
	};
	struct epoll_event event_config =
	{
		.events = EPOLLIN|EPOLLOUT|EPOLLHUP,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->server_conn.socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to register connection socket for epoll");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("Connected to server");
}

enum
{
	WRITE_DISABLED,
	WRITE_ENABLED
};

static void update_connection_management(struct ClusterClientHandle* handle, bool can_write)
{
	// Add socket to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->server_conn.socket_fd
	};
	struct epoll_event event_config =
	{
		.events = EPOLLHUP|EPOLLIN|(can_write ? EPOLLOUT : 0),
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_MOD, handle->server_conn.socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to register connection socket for epoll");
		exit(EXIT_FAILURE);
	}
}

//-------------------------
// Task Management Routine
//-------------------------

static int cache_line_size()
{
    FILE* cache_info = fopen("/sys/bus/cpu/devices/cpu0/cache/index0/coherency_line_size", "r");
    if (cache_info == NULL)
    {
        LOG_ERROR("[cache_line_size] Can't open /sys/bus/cpu/devices/cpu0/cache/index0/coherency_line_size (errno = %d)", errno);
        exit(EXIT_FAILURE);
    }

    int line_size = 0;
    if (fscanf(cache_info, "%d", &line_size) != 1)
    {
        LOG_ERROR("[cache_line_size] Can't scan coherency_line_size (errno = %d)", errno);
        exit(EXIT_FAILURE);
    }

    fclose(cache_info);

    return line_size;
}

static void init_task_management_routine(struct ClusterClientHandle* handle)
{
	handle->requests_to_send = handle->max_threads;
	handle->in_process       = 0;

	handle->thread_manager = (struct ThreadInfo*) calloc(handle->max_threads, sizeof(struct ThreadInfo));
	if (handle->thread_manager == NULL)
	{
		LOG_ERROR("[init_task_management_routine] alloc info mem");
		exit(EXIT_FAILURE);
	}

	handle->task_buffer = malloc(handle->max_threads * (handle->task_size + handle->cache_line_size));
	if (handle->task_buffer == NULL)
	{
		LOG_ERROR("[init_task_management_routine] alloc task buffer");
		exit(EXIT_FAILURE);
	}

	handle->ret_buffer = malloc(handle->max_threads * (handle->ret_size + handle->cache_line_size));
	if (handle->ret_buffer == NULL)
	{
		LOG_ERROR("[init_task_management_routine] alloc ret buffer");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < handle->max_threads; i++)
	{
		// Configure computation arguments:
		struct ComputeInfo in_args =
		{
			.data_pack   = handle->task_buffer + i * (handle->task_size + handle->cache_line_size),
			.ret_pack    = handle->ret_buffer  + i * (handle->ret_size  + handle->cache_line_size),
			.thread_func = handle->thread_func
		};

		// Set cpu to run on:
		cpu_set_t cpu_set;
		CPU_ZERO(&cpu_set);
		CPU_SET(i % handle->max_cpu, &cpu_set);

		// Create event file descriptor to notificate end of computation:
		int event_fd = eventfd(0, EFD_NONBLOCK);
		if (event_fd < 0)
		{
			LOG_ERROR("[init_task_management_routine] Unable to init eventfd");
			exit(EXIT_FAILURE);
		}

		// Define thread manager entry:
		handle->thread_manager[i] = (struct ThreadInfo)
		{
			.empty       = 1,
			.in_args     = in_args,
			.thread_id   = -1, /*not yet known*/
			.cpu         = cpu_set,
			.num_of_task = -1, /*not yet known*/
			.event_fd    = event_fd,
			.ready       = 0
		};
	}

	// Query local machine parameters:
	handle->cache_line_size = cache_line_size();
	handle->max_cpu         = get_nprocs();

	// Log:
	LOG("Task management routine initialized");
}

static void free_task_management_routine(struct ClusterClientHandle* handle)
{
	for (int i = 0; i < handle->max_threads; i++)
	{
		if (close(handle->thread_manager[i].event_fd) == -1)
		{
			LOG_ERROR("[free_eventfd_managment_routine] Unable to close eventfd");
			exit(EXIT_FAILURE);
		}
	}

	free(handle->thread_manager);
	free(handle->task_buffer);
	free(handle->ret_buffer);

	// Log:
	LOG("Task management routine resources freed");
}

static void start_task_management_routine(struct ClusterClientHandle* handle)
{
	for(int i = 0; i < handle->max_threads; i++)
	{
		epoll_data_t event_data =
		{
			.fd = handle->thread_manager[i].event_fd
		};
		struct epoll_event event_config =
		{
			.events = EPOLLIN,
			.data   = event_data
		};
		if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->thread_manager[i].event_fd, &event_config) == -1)
		{
			LOG_ERROR("[start_eventfd_managment_routine] Add event-fd to epoll");
			exit(EXIT_FAILURE);
		}
	}

	// Log:
	LOG("Task management routine running");
}

//---------------------
// Computation Routine
//---------------------

static void* computation_wrapper(void* data)
{
	struct ThreadInfo* info = (struct ThreadInfo*) data;

	void* (*func)(void*) = info->in_args.thread_func;

	// Start computation:
	void* ret_val = (*func)(&(info->in_args));

	// Return results:
	uint64_t val = 1u;
	if (write(info->event_fd, &val, sizeof(uint64_t)) == -1)
	{
		LOG_ERROR("[integral_thread] Unable to write to event-fd");
		exit(EXIT_FAILURE);
	}

	return ret_val;
}

static void start_thread(struct ClusterClientHandle* handle, size_t num, char* buff)
{
	handle->thread_manager[num].num_of_task = *((size_t*)buff);

	buff += sizeof(size_t);
    memcpy(handle->thread_manager[num].in_args.data_pack, buff, handle->task_size);

    // Set thread attributes:
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
	{
		LOG_ERROR("[start_thread] Unable to call pthread_attr_init()");
		exit(EXIT_FAILURE);
	}

	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0)
	{
		LOG_ERROR("[start_thread] Unable to set \"detached\" thread attribute");
		exit(EXIT_FAILURE);
	}

	cpu_set_t cpu_to_run_on = handle->thread_manager[num].cpu;
	if (pthread_attr_setaffinity_np(&attr, sizeof(cpu_to_run_on), &cpu_to_run_on) != 0)
	{
		LOG_ERROR("[start_thread] Unable to call pthread_attr_setaffinity_np()");
		exit(EXIT_FAILURE);
	}

	// Set the thread state:
    handle->in_process++;
	handle->thread_manager[num].empty = 0;

	// Run thread:
	if (pthread_create(&handle->thread_manager[num].thread_id, &attr, computation_wrapper, &handle->thread_manager[num]) != 0)
	{
		LOG_ERROR("[start_thread] Unable to create thread");
		exit(EXIT_FAILURE);
	}
}

// !!!!
static void prepare_ret_buff(struct ClusterClientHandle* handle, size_t num, char* buff)
{
	BUG_ON(buff == NULL, "[prepare_ret_buff] recv buff is NULL");

	buff[0] = 1;
	buff++;

	(*(size_t*)buff) = handle->thread_manager[num].num_of_task;
	buff += sizeof(size_t);

    memcpy(buff, handle->thread_manager[num].in_args.ret_pack, handle->ret_size);

	handle->in_process--;
	handle->thread_manager[num].empty = 1;
}

// !!!!
static void eventfd_handler(struct ClusterClientHandle* handle, struct epoll_event* pending_events)
{
	BUG_ON(pending_events == NULL, "[eventfd_handler] Bad arg - pending_events");

	for(int j = 0; j < handle->max_threads; j++)
	{
		if (pending_events->data.fd == handle->thread_manager[j].event_fd && pending_events->events & EPOLLIN)
		{
			handle->thread_manager[j].ready = 1;

			uint64_t out = 0;
			int ret = read(handle->thread_manager[j].event_fd, &out, sizeof(uint64_t));
			if (ret < 0)
			{
				LOG_ERROR("[eventfd_handler] Unable to reduce eventfd");
				exit(EXIT_FAILURE);
			}

			update_connection_management(handle, WRITE_ENABLED);
		}
	}
}

void init_cluster_client(struct ClusterClientHandle* handle);
void stop_cluster_client(struct ClusterClientHandle* handle);

void client_compute(size_t num_threads, size_t task_size, size_t ret_size, const char* master_host, void* (*thread_func)(void*))
{
	BUG_ON(num_threads == 0,    "[client_compute] Number of threads is zero");
	BUG_ON(task_size == 0,      "[client_compute] Task size is zero");
	BUG_ON(ret_size == 0,       "[client_compute] Ret size is zero");
	BUG_ON(thread_func == NULL, "[client_compute] Nullptr computation");

	struct ClusterClientHandle handle;

	// Discovery parameters:
	handle.local_discovery  = (master_host == NULL);
	handle.server_hostname  = master_host;

	// Computation parameters:
	handle.max_threads      = num_threads;
	handle.ret_size         = ret_size;
	handle.task_size        = task_size;
	handle.thread_func      = thread_func;

	init_cluster_client(&handle);

	stop_cluster_client(&handle);
}

// !!!!
static void in_handler(struct ClusterClientHandle* handle)
{
	size_t RECV_BUFFER_SIZE = sizeof(size_t) + handle->task_size;

	char* buf_ptr = handle->server_conn.recv_buffer + handle->server_conn.bytes_recieved;
	size_t bytes_to_read = RECV_BUFFER_SIZE - handle->server_conn.bytes_recieved;

	int bytes_read = recv(handle->server_conn.socket_fd, buf_ptr, bytes_to_read, MSG_WAITALL);
	if (bytes_read == -1 || (bytes_read == 1 && *buf_ptr == 0))
	{
		LOG_ERROR("[in_handler] Unable to recv() incoming computation request");
		exit(EXIT_FAILURE);
	}

	handle->server_conn.bytes_recieved += bytes_read;
	if (handle->server_conn.bytes_recieved < RECV_BUFFER_SIZE)
	{
		// Do not handle request:
		return;
	}

	handle->server_conn.bytes_recieved = 0;

	LOG("Recieved computation task#%zu", *(size_t*)(handle->server_conn.recv_buffer));

	for (size_t i = 0; i < handle->max_threads; i++)
	{
		if (handle->thread_manager[i].empty == 1)
		{
			start_thread(handle, i, handle->server_conn.recv_buffer);
			break;
		}
	}

	// Enable write if handle->in_process != handle->max_threads:
	update_connection_management(handle, handle->in_process != handle->max_threads);
}

// !!!!
static void out_handler(struct ClusterClientHandle* handle)
{
	size_t SEND_BUFFER_SIZE = sizeof(size_t) + 1 + handle->ret_size;

	size_t sent_returns = 0;
	for(size_t i = 0; i < handle->max_threads; i++)
	{
		if (handle->thread_manager[i].ready == 1)
		{
			char send_buffer[SEND_BUFFER_SIZE];
			prepare_ret_buff(handle, i, send_buffer);

			int bytes_written = send(handle->server_conn.socket_fd, send_buffer, SEND_BUFFER_SIZE, MSG_NOSIGNAL);
			if (bytes_written != SEND_BUFFER_SIZE)
			{
				LOG_ERROR("[out_handler] Unable to send packet to server");
				exit(EXIT_FAILURE);
			}

			sent_returns++;
			handle->thread_manager[i].ready = 0;
			(handle->requests_to_send)++;

			LOG("Sent result of task#%zu to server", *(size_t*)(send_buffer + 1));
			update_connection_management(handle, WRITE_ENABLED);
		}
	}
	if (sent_returns != 0)
		return;

	for(; handle->requests_to_send > 0; handle->requests_to_send -= 1)
	{
		char send_buffer[SEND_BUFFER_SIZE];
		send_buffer[0] = 0;

		int bytes_written = send(handle->server_conn.socket_fd, send_buffer, SEND_BUFFER_SIZE, MSG_NOSIGNAL);
		if (bytes_written != SEND_BUFFER_SIZE)
		{
			LOG_ERROR("[client_eventloop] Unable to request to server");
			exit(EXIT_FAILURE);
		}

		LOG("Sent request for more data");
	}

	update_connection_management(handle, WRITE_DISABLED);
}

//------------------
// Client Eventloop
//------------------

static void* client_eventloop(void* arg)
{
	struct ClusterClientHandle* handle = arg;

	static const int MAX_EVENTS = 16;

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
			// Perform discovery:
			if (pending_events[ev].data.fd == handle->discovery_timer_fd)
			{
				perform_discovery_send(handle);
			}

			// Catch server's replies to discovery:
			if (pending_events[ev].data.fd == handle->discovery_socket_fd && pending_events[ev].events & EPOLLIN)
			{
				catch_servers_discovery_datagram(handle);
			}

			// Handle connection shutdown or connection timeout:
			if (pending_events[ev].data.fd == handle->server_conn.socket_fd && pending_events[ev].events & EPOLLHUP)
			{
				if (handle->in_process != 0)
				{
					LOG("Connection shutdown observed. Quitting");
					exit(EXIT_SUCCESS);
				}

				return NULL;
			}

			// Handle event_fd - finish of the thread
			eventfd_handler(handle, pending_events + ev);

			// Handle connection read:
			if (pending_events[ev].data.fd == handle->server_conn.socket_fd && pending_events[ev].events & EPOLLIN)
			{
				in_handler(handle);
			}

			// Handle connection write:
			if (pending_events[ev].data.fd == handle->server_conn.socket_fd && pending_events[ev].events & EPOLLOUT)
			{
				out_handler(handle);
			}
		}
	}

	return NULL;
}

//-------------------------------------
// Initialization and deinitialization
//-------------------------------------

void init_cluster_client(struct ClusterClientHandle* handle)
{
	// Init subroutines:
	init_discovery_routine            (handle);
	init_connection_management_routine(handle);
	init_task_management_routine      (handle);

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

	// Start managing tasks:
	start_task_management_routine(handle); 

	// Perform discovery:
	start_discovery_routine(handle);

	// Log:
	LOG("Cluster-client initialized");
}

void stop_cluster_client(struct ClusterClientHandle* handle)
{
	int err = pthread_join(handle->eventloop_thr_id, NULL);
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
	free_discovery_routine            (handle);
	free_connection_management_routine(handle);
	free_task_management_routine      (handle);

	// Log:
	LOG("Cluster-client stopped");
}
