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
// Calloc:
#include <stdlib.h>
// Inet_addr:
#include <arpa/inet.h>

// Cluster Client API:
#include "ClusterClient.h"

// System Configuration:
#include "Config.h"

//-----------------
// Server Tracking
//-----------------

static void init_server_tracking_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[init_server_tracking_routine] Nullptr argument");

	if (!handle->local_discovery)
	{
		LOG("Server tracking routine initialized (distant discovery)");
		return;
	}

	int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
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
		.sin_port        = htons(DISCOVERY_PORT)
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
	LOG("Server tracking routine initialized");
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
	LOG("Server tracking routine resources freed");
}

static void start_server_tracking_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[start_server_tracking_routine] Nullptr argument");

	if (!handle->local_discovery)
	{
		// Log:
		LOG("Server tracking routine running (distant discovery mode)");

		return;
	}

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
	LOG("Server tracking routine running");
}

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
	BUG_ON(handle == NULL, "[discover_server] Nullptr argument");

	// Wait for incomping datagram:
	LOG("Waiting for discovery datagram");

	struct sockaddr_in peer_addr;
	socklen_t peer_addr_len = sizeof(peer_addr);
	char buffer[DISCOVERY_DATAGRAM_SIZE];

	int bytes_read;
	do
	{
		bytes_read = recvfrom(handle->server_tracking_socket_fd, buffer, DISCOVERY_DATAGRAM_SIZE, 0, (struct sockaddr*) &peer_addr, &peer_addr_len);
		if (bytes_read == -1)
		{
			LOG_ERROR("[discover_server] Unable to recieve discovery datagram (errno = %d)", errno);
			exit(EXIT_FAILURE);
		}
	}
	while (bytes_read != DISCOVERY_DATAGRAM_SIZE);

	// Save server address:
	handle->server_addr = peer_addr;

	// Switch socket into nonblocking mode:
	if (fcntl(handle->server_tracking_socket_fd, F_SETFD, O_NONBLOCK) == -1)
	{
		LOG_ERROR("[discover_server] Unable to switch socket into non-blocking mode");
		exit(EXIT_FAILURE);
	}

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
}

//-------------------------------
// Connection Management Routine
//-------------------------------

static void init_connection_management_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[init_connection_management_routine] Nullptr argument");

	// Log:
	LOG("Connection management routine initialized");
}

static void free_connection_management_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[free_connection_management_routine] Nullptr argument");

	close(handle->server_conn.socket_fd);

	// Log:
	LOG("Connection management routine resources freed");
}

static void start_connection_management_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[start_connection_management_routine] Nullptr argument");

	// Acquire connection socket:
	int sock_fd = sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock_fd == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to create socket");
		exit(EXIT_FAILURE);
	}

	// Ask socket to automatically detect disconnection:
	int setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to set SO_KEEPALIVE socket option");
		exit(EXIT_FAILURE);
	}

	// Acquire address:
	struct sockaddr_in server_addr;
	server_addr.sin_family      = AF_INET;
	server_addr.sin_port        = htons(CONNECTION_PORT);
	server_addr.sin_addr.s_addr = (handle->local_discovery)           ?
	                              handle->server_addr.sin_addr.s_addr :
	                              inet_addr(handle->server_hostname);

	// Connect to peer:
	if (connect(sock_fd, (struct sockaddr*) &server_addr, sizeof(server_addr)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to connect to server");
		exit(EXIT_FAILURE);
	}

	// Make socket non-blocking:
	if (fcntl(sock_fd, F_SETFD, O_NONBLOCK) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to set O_NONBLOCK flag with fcnt()");
		exit(EXIT_FAILURE);
	}

	handle->server_conn.socket_fd = sock_fd;
	handle->server_conn.can_read  = 1;
	handle->server_conn.can_write = 1;

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
	LOG("Connection management routine running");
}

static void update_conn_management(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[start_conn_in_management] Nullptr argument");

	// Add socket to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->server_conn.socket_fd
	};
	struct epoll_event event_config =
	{
		.events = (handle->server_conn.can_read  ? EPOLLIN  : 0) |
		          (handle->server_conn.can_write ? EPOLLOUT : 0) | EPOLLHUP,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_MOD, handle->server_conn.socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to register connection socket for epoll");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("Updated connection management: read %s, write %s",
	    handle->server_conn.can_read ? "enabled" : "disabled", handle->server_conn.can_write ? "enabled" : "disabled");
}

//-----------------------------
// Computation task management
//-----------------------------

static void init_task_computing_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[init_task_computing_routine] Nullptr argument");

	handle->computations_ready = (bool*) calloc(handle->max_threads, sizeof(*handle->computations_ready));
	if (handle->computations_ready == NULL)
	{
		LOG_ERROR("[init_task_computing_routine] Unable to allocate memory");
		exit(EXIT_FAILURE);
	}
	handle->empty_thread = (bool*) calloc(handle->max_threads, sizeof(*handle->empty_thread));
	if (handle->empty_thread == NULL)
	{
		LOG_ERROR("[init_task_computing_routine] Unable to allocate memory");
		exit(EXIT_FAILURE);
	}

	for (size_t i = 0; i < handle->max_threads; ++i)
	{
		handle->computations_ready[i] = 0;
		handle->empty_thread[i]       = 1;
	}

	// Log:
	LOG("Task computing routine initialized");
}

static void free_task_computing_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[free_task_computing_routine] Nullptr argument");

	free(handle->computations_ready);
	free(handle->empty_thread);

	// Log:
	LOG("Task computing routine resources freed");
}

//-------------------------
// Task Management Routine
//-------------------------

static int cache_line_size()
{
    errno = 0;
    FILE* cache_info = fopen("/sys/bus/cpu/devices/cpu0/cache/index0/coherency_line_size", "r");
    if (cache_info == NULL)
    {
        perror("Can't open /sys/bus/cpu/devices/cpu0/cache/index0/coherency_line_size\n");
        return E_ERROR;
    }

    int line_size = 0;
    int ret = fscanf(cache_info, "%d", &line_size);
    if (ret != 1)
    {
        perror("Can't scan coherency_line_size\n");
        return E_ERROR;
    }

    return line_size;
}

void client_compute(size_t num_threads, size_t task_size, size_t ret_size, const char* master_host, void* (*thread_func)(void*))
{
	BUG_ON(num_threads == 0, "[client_compute] number of threads is zero");
	BUG_ON(task_size == 0, "[client_compute] task size is zero");
	BUG_ON(ret_size == 0, "[client_compute] ret size is zero");
	BUG_ON(thread_func == NULL, "[client_compute] Nullptr handle");

	struct ClusterClientHandle handle;

	handle.max_threads     = num_threads;
	handle.local_discovery = (master_host == NULL);
	handle.server_hostname = master_host;
	handle.ret_size        = ret_size;
	handle.task_size       = task_size;
	handle.thread_func     = thread_func;
	handle.in_process      = 0;

	handle.thread_manager = (struct ThreadInfo*) calloc(num_threads, sizeof(struct ThreadInfo));
	if (handle.thread_manager == NULL)
	{
		LOG_ERROR("[init_cluster_client] alloc info mem");
		exit(EXIT_FAILURE);
	}

	int cache = cache_line_size();
	int cpu_size = get_nprocs();

	handle.task_buffer = malloc(num_threads * (task_size + cpu_size));
	if (handle.task_buffer == NULL)
	{
		LOG_ERROR("[init_cluster_client] alloc task buffer");
		exit(EXIT_FAILURE);
	}

	handle.ret_buffer = malloc(num_threads * (ret_size + cpu_size));
	if (handle.ret_buffer == NULL)
	{
		LOG_ERROR("[init_cluster_client] alloc ret buffer");
		exit(EXIT_FAILURE);
	}

	handle.recv_buff = (char*) calloc(task_size + sizeof(size_t), sizeof(char));
	if (handle.recv_buff == NULL)
	{
		LOG_ERROR("[init_cluster_client] alloc recv buffer");
		exit(EXIT_FAILURE);
	}
	handle.bytes_recv = 0;


	for (int i = 0; i < num_threads; i++)
	{
		handle.thread_manager[i].num_cpu     = i % cpu_size;
		handle.thread_manager[i].num_of_task = 0;
		handle.thread_manager[i].line_size   = cache;
		handle.thread_manager[i].data_pack   = handle.task_buffer + i * (task_size + cpu_size);
		handle.thread_manager[i].ret_pack    = handle.ret_buffer + i * (ret_size + cpu_size);
	}

	init_cluster_client(&handle, num_threads, NULL);

	//while(1);

	stop_cluster_client(&handle);

	free(handle.task_buffer);
	free(handle.ret_buffer);
	free(handle.thread_manager);
	free(handle.recv_buff);
}

static void start_thread(struct ClusterClientHandle* handle, size_t num, char* buff)
{
	BUG_ON(handle == NULL, "[start_thread] handle is NULL");
	BUG_ON(buff == NULL, "[start_thread] recv buff is NULL");

	handle->thread_manager[num].num_of_task = *((size_t*)buff);
	LOG("[in_handle] Recv packet # %zu", handle->thread_manager[num].num_of_task);

	buff += sizeof(size_t);
    memcpy(handle->thread_manager[num].data_pack, buff, handle->task_size);

    pthread_attr_t attr;

	int ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (ret < 0)
	{
		LOG_ERROR("[start thread] detache state error");
		exit(EXIT_FAILURE);
	}

	ret = pthread_create(&(handle->thread_manager[num].thread_id), NULL, handle->thread_func, &(handle->thread_manager[num]));
	if (ret < 0)
	{
		LOG_ERROR("[start_thread] Creating thread error");
		exit(EXIT_FAILURE);
	}

    handle->in_process++;
	handle->empty_thread[num] = 0;
}

static void prepare_ret_buff(struct ClusterClientHandle* handle, size_t num, char* buff)
{
	BUG_ON(handle == NULL, "[prepare_ret_buff] handle is NULL");
	BUG_ON(buff == NULL, "[prepare_ret_buff] recv buff is NULL");

	buff[0] = 1;
	buff++;

	(*(size_t*)buff) = handle->thread_manager[num].num_of_task;
	LOG("Send task#%ld reply", handle->thread_manager[num].num_of_task);
	buff += sizeof(size_t);

    memcpy(buff, handle->thread_manager[num].ret_pack, handle->ret_size);

	handle->in_process--;
	handle->empty_thread[num] = 1;
}

static void eventfd_handler(struct ClusterClientHandle* handle, struct epoll_event* pending_events)
{
	BUG_ON(handle == NULL, "[eventfd_handler] handle is NULL");
	BUG_ON(pending_events == NULL, "[eventfd_handler] Bad arg - pending_events");

	for(int j = 0; j < handle->max_threads; j++)
	{
		if (pending_events->data.fd == handle->thread_manager[j].event_fd && pending_events->events & EPOLLIN)
		{
			handle->computations_ready[j] = 1;

			uint64_t out = 0;
			int ret = read(handle->thread_manager[j].event_fd, &out, sizeof(uint64_t));
			if (ret < 0)
			{
				LOG_ERROR("[eventfd_handler] Unable to reduce eventfd");
				exit(EXIT_FAILURE);
			}

			handle->server_conn.can_write = 1;
			update_conn_management(handle);
		}
	}
}

static void in_handler(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[in_handler] handle is NULL");

	size_t RECV_BUFFER_SIZE = sizeof(size_t) + handle->task_size;

	int bytes_read = recv(handle->server_conn.socket_fd, handle->recv_buff + handle->bytes_recv, RECV_BUFFER_SIZE - handle->bytes_recv, 0);
	if (bytes_read == -1)
	{
		LOG_ERROR("[in_handler] Unable to recieve packet from server");
		exit(EXIT_FAILURE);
	}
	handle->bytes_recv += bytes_read;
	if (handle->bytes_recv < RECV_BUFFER_SIZE)
		return;
	handle->bytes_recv = 0;

	for (size_t i = 0; i < handle->max_threads; i++)
	{
		if (handle->empty_thread[i] == 1)
		{
			start_thread(handle, i, handle->recv_buff);
			break;
		}
	}

	handle->server_conn.can_read = 1;
	if (handle->in_process != handle->max_threads)
		handle->server_conn.can_write = 1;

	update_conn_management(handle);
}

static void out_handler(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[out_handler] handle is NULL");

	size_t SEND_BUFFER_SIZE = sizeof(size_t) + 1 + handle->ret_size;

	for(size_t i = 0; i < handle->max_threads; i++)
	{
		if (handle->computations_ready[i] == 1)
		{
			char send_buffer[SEND_BUFFER_SIZE];
			prepare_ret_buff(handle, i, send_buffer);

			int bytes_written = send(handle->server_conn.socket_fd, send_buffer, SEND_BUFFER_SIZE, MSG_NOSIGNAL);
			if (bytes_written != SEND_BUFFER_SIZE)
			{
				LOG_ERROR("[out_handler] Unable to send packet to server");
				exit(EXIT_FAILURE);
			}

			handle->computations_ready[i] = 0;

			LOG("Sent packet to server");
			handle->server_conn.can_read = 1;
			handle->server_conn.can_write = 1;
			update_conn_management(handle);
			return;
		}
	}

	for (size_t i = 0; i < handle->max_threads; ++i)
	{
		if (handle->empty_thread[i] == 1)
		{
			char send_buffer[SEND_BUFFER_SIZE];
			send_buffer[0] = 0;

			int bytes_written = send(handle->server_conn.socket_fd, send_buffer, SEND_BUFFER_SIZE, MSG_NOSIGNAL);
			if (bytes_written != SEND_BUFFER_SIZE)
			{
				LOG_ERROR("[client_eventloop] Unable to request to server");
				exit(EXIT_FAILURE);
			}

			LOG("Sent request to server");
			handle->server_conn.can_read = 1;
			handle->server_conn.can_write = 0;
			update_conn_management(handle);
			return;
		}
	}
}

//------------------
// Client Eventloop
//------------------

static void* client_eventloop(void* arg)
{
	struct ClusterClientHandle* handle = arg;
	BUG_ON(handle == NULL, "[client_eventloop] Nullptr argument");

	static const int MAX_EVENTS       = 16;

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
				LOG("No discovery datagrams recieved in a while. Quitting");
				exit(EXIT_FAILURE);
			}

			// Handle intended connection shutdown or connection timeout:
			if (pending_events[ev].data.fd == handle->server_conn.socket_fd && pending_events[ev].events & EPOLLHUP)
			{
				if (handle->in_process != 0)
				{
					LOG("Connection shutdown observed. Quitting");
					exit(EXIT_FAILURE);
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


static void start_eventfd_managment_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[start_eventfd_managment_routine] Nullptr argument");

	for(int i = 0; i < handle->max_threads; i++)
	{
		handle->thread_manager[i].event_fd  = eventfd(0, EFD_NONBLOCK);
		if (handle->thread_manager[i].event_fd < 0)
		{
			LOG_ERROR("[init_cluster_client] init eventfd");
			exit(EXIT_FAILURE);
		}

		epoll_data_t event_data =
		{
			.fd = handle->thread_manager[i].event_fd
		};
		struct epoll_event event_config =
		{
			.events = EPOLLIN|EPOLLHUP,
			.data   = event_data
		};
		if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->thread_manager[i].event_fd, &event_config) == -1)
		{
			LOG_ERROR("[start_eventfd_managment_routine] eventctl add ");
			exit(EXIT_FAILURE);
		}
	}

}

static void free_eventfd_managment_routine(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[free_eventfd_managment_routine] Nullptr argument");

	for(int i = 0; i < handle->max_threads; i++)
	{
		int ret = close(handle->thread_manager[i].event_fd);
		if (ret < 0)
		{
			LOG_ERROR("[free_eventfd_managment_routine] eventfd close");
			exit(EXIT_FAILURE);
		}
	}
}

//-------------------------------------
// Initialization and deinitialization
//-------------------------------------

void init_cluster_client(struct ClusterClientHandle* handle, size_t max_threads, const char* master_host)
{
	BUG_ON(handle == NULL, "[init_cluster_client] Nullptr argument");

	// Init subroutines:
	init_server_tracking_routine(handle);

	init_connection_management_routine(handle);
	init_task_computing_routine(handle);

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

	// Iniciate fd for ret_value
	start_eventfd_managment_routine(handle);

	// Perform discovery:
	discover_server(handle);
	start_server_tracking_routine(handle);

	// Initiate connection:
	start_connection_management_routine(handle);

	// Log:
	LOG("Cluster-client initialized");
}

void stop_cluster_client(struct ClusterClientHandle* handle)
{
	BUG_ON(handle == NULL, "[stop_cluster_client] Nullptr argument");

	// Stop eventloop:
	/*int err = pthread_cancel(handle->eventloop_thr_id);
	if (err != 0)
	{
		LOG_ERROR("[stop_cluster_client] pthread_cancel() failed with error %d", err);
		exit(EXIT_FAILURE);
	}*/

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
	free_server_tracking_routine      (handle);
	free_task_computing_routine       (handle);
	free_connection_management_routine(handle);
	free_eventfd_managment_routine    (handle);

	// Log:
	LOG("Cluster-client stopped");
}
