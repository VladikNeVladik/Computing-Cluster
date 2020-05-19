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
// Calloc:
#include <stdlib.h>

// Cluster Server API:
#include "ClusterServer.h"

// System Configuration:
#include "Config.h"

//-------------------
// Discovery process
//-------------------

static void init_discovery_routine(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[init_discovery_routine] Nullptr argument");

	// Acquire discovery socket:
	int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_fd == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to create socket");
		exit(EXIT_FAILURE);
	}

	int setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to call setsockopt()");
		exit(EXIT_FAILURE);
	}

	// Disable the TIME-WAIT state of a socket:
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to set SO_REUSEADDR socket option");
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
		.it_interval = {DISCOVERY_REPEAT_TIME, 0},
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

void perform_discovery_send(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[perform_discovery_send] Nullptr argument");

	char send_buffer[DISCOVERY_DATAGRAM_SIZE];
	int bytes_written = write(handle->discovery_socket_fd, send_buffer, DISCOVERY_DATAGRAM_SIZE);
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

	uint64_t timer_expirations = 0;
	if (read(handle->discovery_timer_fd, &timer_expirations, 8) == -1)
	{
		LOG_ERROR("[server_eventloop] Unable to perform read on timer file descriptor");
		exit(EXIT_FAILURE);
	}

	LOG("[CLUSTER-SERVER] Sent discovery datagram");
}

//-----------------------
// Connection Management
//-----------------------

static void init_connection_management_routine(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[init_connection_management_routine] Nullptr argument");

	handle->max_clients = MAX_SIMULTANEOUS_CONNECTIONS;
	
	// Create connection table:
	handle->client_conns = (struct Connection*) calloc(handle->max_clients, sizeof(*handle->client_conns));
	if (handle->client_conns == NULL)
	{
		LOG_ERROR("[init_connection_management_routine] Unable to allocate memory for connections");
		exit(EXIT_FAILURE);
	}

	for (size_t i = 0; i < handle->max_clients; ++i)
	{
		handle->client_conns[i].socket_fd = -1;
	}

	handle->num_clients = 0;

	// Get socket:
	int sock_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, IPPROTO_TCP);
	if (sock_fd == -1)
	{
		LOG_ERROR("[init_connection_management_routine] Unable to call socket()");
		exit(EXIT_FAILURE);
	}

	// Acquire address:
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(CONNECTION_PORT);

	if (bind(sock_fd, &server_addr, sizeof(server_addr)) == -1)
	{
		LOG_ERROR("[init_connection_management_routine] Unable to bind()");
		exit(EXIT_FAILURE);
	}

	// Ask socket to automatically detect disconnection:
	int setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to set TCP_KEEPALIVE socket option");
		exit(EXIT_FAILURE);
	}

	// Disable the TIME-WAIT state of a socket:
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to set SO_REUSEADDR socket option");
		exit(EXIT_FAILURE);
	}

	// Listen for incoming connections:
	if (listen(sock_fd, LISTEN_CONNECTION_BACKLOG) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to listen() on a socket");
		exit(EXIT_FAILURE);
	}

	handle->accept_socket_fd = sock_fd;

	// Log:
	LOG("[CLUSTER-SERVER] Connection management routine initialized");
}

static void free_connection_management_routine(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[free_connection_management_routine] Nullptr argument");

	close(handle->accept_socket_fd);

	for (size_t i = 0; i < handle->max_clients; ++i)
	{
		if (handle->client_conns[i].socket_fd != -1)
		{
			if (close(handle->client_conns[i].socket_fd))
			{
				LOG_ERROR("[free_connection_management_routine] Unable to close connection#%03zu", i);
				exit(EXIT_FAILURE);
			}
		}
	}

	free(handle->client_conns);

	// Log:
	LOG("[CLUSTER-SERVER] Connection management routine resources freed");
}

static void start_connection_management_routine(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[start_connection_management_routine] Nullptr argument");

	// Add accept-socket file descriptor to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->accept_socket_fd
	};
	struct epoll_event event_config =
	{
		.events = EPOLLIN,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->accept_socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to register file descriptor for epoll");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("[CLUSTER-SERVER] Connection management running");
}

static void pause_connection_management_routine(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[start_connection_management_routine] Nullptr argument");

	// Add accept-socket file descriptor to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->accept_socket_fd
	};
	struct epoll_event event_config =
	{
		.events = EPOLLIN,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_DEL, handle->accept_socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to delete file descriptor from epoll");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("[CLUSTER-SERVER] Connection management paused");
}

static void update_connection_management(struct ClusterServerHandle* handle, size_t client_index)
{
	BUG_ON(handle == NULL, "[start_conn_in_management] Nullptr argument");

	// Add socket to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->client_conns[client_index].socket_fd
	};
	struct epoll_event event_config =
	{
		.events = (handle->client_conns[client_index].can_read  ? EPOLLIN  : 0) |
		          (handle->client_conns[client_index].can_write ? EPOLLOUT : 0) | EPOLLHUP,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_MOD, handle->client_conns[client_index].socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to update connection#%03zu in epoll", client_index);
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("[CLUSTER-CLIENT] Updated connection#%03zu: read %s, write %s", client_index,
	    handle->client_conns[client_index].can_read  ? "enabled" : "disabled",
	    handle->client_conns[client_index].can_write ? "enabled" : "disabled");
}

static void delete_connection(struct ClusterServerHandle* handle, size_t client_index)
{
	BUG_ON(handle == NULL, "[delete_connection] Nullptr argument");

	// Start accepting incoming connections:
	if (handle->num_clients == handle->max_clients)
	{
		start_connection_management_routine(handle);
	}

	// Delete socket from epoll:
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_DEL, handle->client_conns[client_index].socket_fd, NULL) == -1)
	{
		LOG_ERROR("[delete_connection] Unable to delete connection#%03zu from epoll", client_index);
		exit(EXIT_FAILURE);
	}

	if (close(handle->client_conns[client_index].socket_fd) == -1)
	{
		LOG_ERROR("[delete_connection] Unable to close socket");
		exit(EXIT_FAILURE);
	}

	handle->client_conns[client_index].socket_fd = -1;

	free(handle->client_conns[client_index].task_list);

	handle->num_clients -= 1;

	// Log:
	LOG("[CLUSTER-CLIENT] Deleted connection#%03zu", client_index);
}

static void accept_incoming_connection_request(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[accept_incoming_connection_request] Nullptr argument");

	// Accept the request:
	int client_socket_fd = accept4(handle->accept_socket_fd, NULL, NULL, SOCK_NONBLOCK);
	if (client_socket_fd == -1)
	{
		if (errno == EAGAIN) return;

		LOG_ERROR("[accept_incoming_connection_request] Unable to accept4() incoming connection request");
		exit(EXIT_FAILURE);
	}

	// Search for a free cell:
	size_t client_index = -1;
	for (size_t i = 0; i < handle->max_clients; ++i)
	{
		if (handle->client_conns[i].socket_fd == -1)
		{
			client_index = i;
			handle->num_clients += 1;
			break;
		}
	}

	// Stop accepting connections if there is no memory to keep track of them:
	if (handle->num_clients == handle->max_clients)
	{
		pause_connection_management_routine(handle);
	}

	BUG_ON(client_index == -1, "[accept_incoming_connection_request] No free cell in the connection array");

	handle->client_conns[client_index] = (struct Connection)
	{
		.socket_fd            = client_socket_fd,
		.can_read             = 1,
		.can_write            = 1,
		.want_task            = 0,
		.returned_task        = 1,
		.active_computations  = 0,
		.num_tasks            = 24
	};

////////////////////////////////////////////////////////////////////////////////
	handle->client_conns[client_index].task_list = (int*) calloc(24, sizeof(int));
	if (handle->client_conns[client_index].task_list == NULL)
	{
		LOG_ERROR("[accept_incoming_connection_request] alloc client task list mem error");
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < 24; i++)
		handle->client_conns[client_index].task_list[i] = -1;
////////////////////////////////////////////////////////////////////////////////

	// Add the socket to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->client_conns[client_index].socket_fd
	};
	struct epoll_event event_config =
	{
		.events = EPOLLIN|EPOLLOUT|EPOLLHUP,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->client_conns[client_index].socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[accept_incoming_connection_request] Unable to add connection#%03zu to epoll", client_index);
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("[CLUSTER-SERVER] Incoming connection request accepted");
	LOG("[CLUSTER-SERVER] Connection#%03zu now active", client_index);
}

int compute_task(size_t num_tasks, void* tasks, size_t size_task, void* rets, size_t size_ret)
{
	BUG_ON(num_tasks == 0, "[compute_task] error tasks number");
	BUG_ON(tasks == NULL, "[compute_task] task buffer is nullptr");
	BUG_ON(size_task == 0, "[compute_task] invalid task size");
	BUG_ON(rets == NULL, "[compute_task] ret buffer is nullptr");
	BUG_ON(size_ret ==  0, "[compute_task] invalid ret size");

	struct ClusterServerHandle handle;

    errno = 0;
	handle.task_manager = (struct task_info*) calloc(num_tasks, sizeof(struct task_info));
	if (handle.task_manager == NULL)
	{
		LOG_ERROR("[compute_task] alloc task manager");
		return errno;
	}

    for(size_t i = 0; i < num_tasks; i++)
	{
		handle.task_manager[i].task   = tasks + i * size_task;
		handle.task_manager[i].ret    = rets + i * size_ret;
		handle.task_manager[i].status = NOT_RESOLVED; // paranoia
	}

	handle.num_unresolved = num_tasks;
	handle.num_tasks      = num_tasks;
	handle.size_task      = size_task;
	handle.size_ret       = size_ret;

	init_cluster_server(&handle);

	//while(1);

	stop_cluster_server(&handle);

	return 0;
}

static void push_ret_val(struct ClusterServerHandle* handle, size_t number, char* buff)
{
	BUG_ON(handle == NULL, "[push_ret_val] in pointer is invalid");
	BUG_ON(buff == NULL, "[push_ret_val] buff pointer is invalid");

    size_t num_ret_packet = *((size_t*)buff);

	LOG("[push_ret_val] Recieve %ld solved task", num_ret_packet);

	buff += sizeof(size_t);

	memcpy(handle->task_manager[num_ret_packet].ret, buff, handle->size_ret);
	handle->task_manager[num_ret_packet].status = COMPLETED;

	(handle->client_conns[number].active_computations)--;
	size_t i = 0;
	for(; i < handle->client_conns[number].num_tasks; i++)
	{
		if (handle->client_conns[number].task_list[i] == num_ret_packet)
		{
			handle->client_conns[number].task_list[i] = -1;
			break;
		}
	}

	BUG_ON(i == handle->client_conns[number].num_tasks, "[push_ret_val] Can't find number of task in task list");

	(handle->num_unresolved)--;
}


static int get_task(struct ClusterServerHandle* handle, size_t number, char* buff)
{
	BUG_ON(handle == NULL, "[get_task] in pointer is invalid");
	BUG_ON(buff == NULL, "[get_task] buff pointer is invalid");

	size_t i = 0;
	for(; i < handle->num_tasks; i++)
	{
		//printf("%d\n", handle->task_manager[i].status);
		if (handle->task_manager[i].status == NOT_RESOLVED)
		{
			handle->task_manager[i].status = RESOLVING;
			*((size_t*)buff) = i;
			memcpy(buff + sizeof(size_t), handle->task_manager[i].task,  handle->size_task);
			handle->client_conns[number].want_task = 0;
			(handle->client_conns[number].active_computations)++;
			int j = 0;
			for(; j < handle->client_conns[number].num_tasks; j++)
			{
				//printf("%d\n", handle->client_conns[number].task_list[j]);
				if (handle->client_conns[number].task_list[j] == -1)
				{
					handle->client_conns[number].task_list[j] = i;
					break;
				}
			}
			BUG_ON(j == handle->client_conns[number].num_tasks, "[get_task] not enough space in task list");
			break;
		}
	}
	if (i == handle->num_tasks)
		return -1;
	return i;
}

static void drop_unresolved(struct ClusterServerHandle* handle, size_t number)
{
	BUG_ON(handle == NULL, "[get_task] in pointer is invalid");

	for(int i = 0; i < handle->client_conns[number].num_tasks; i++)
	{
		if (handle->client_conns[number].task_list[i] > -1 && handle->client_conns[number].task_list[i] < handle->num_tasks)
			handle->task_manager[handle->client_conns[number].task_list[i]].status = NOT_RESOLVED;
		handle->client_conns[number].active_computations = 0;
	}
}

//------------------
// Server Eventloop
//------------------

static void* server_eventloop(void* arg)
{
	struct ClusterServerHandle* handle = arg;
	BUG_ON(handle == NULL, "[server_eventloop] Nullptr argument");

	const int MAX_EVENTS       = 16;
	int RECV_BUFFER_SIZE = handle->size_ret + sizeof(size_t) + sizeof(char);
    int SEND_BUFFER_SIZE = handle->size_task + sizeof(size_t);

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
			// Sendout discovery datagram:
			if (pending_events[ev].data.fd == handle->discovery_timer_fd)
			{
				perform_discovery_send(handle);
			}

			// Accept incoming connection request:
			if (pending_events[ev].data.fd == handle->accept_socket_fd)
			{
				accept_incoming_connection_request(handle);
			}

			// Manage connections:
			for (size_t i = 0; i < handle->max_clients; ++i)
			{
				if (pending_events[ev].data.fd != handle->client_conns[i].socket_fd) continue;
				if (handle->client_conns[i].socket_fd == -1) continue;

				if (pending_events[ev].events & EPOLLHUP)
				{
					LOG("[CLUSTER-CLIENT] Connection#%03zu hangup detected", i);
					drop_unresolved(handle, i);
					delete_connection(handle, i);
					continue;
				}

				if (pending_events[ev].events & EPOLLIN)
				{
					LOG("[CLUSTER-CLIENT] Recieved packet on connection#%03zu", i);

					char recv_buffer[RECV_BUFFER_SIZE];
					int bytes_read = recv(handle->client_conns[i].socket_fd, recv_buffer, RECV_BUFFER_SIZE, 0);
					if (bytes_read == -1 || bytes_read < RECV_BUFFER_SIZE)
					{
						LOG_ERROR("[client_eventloop] Unable to recieve packet from client#%03zu", i);
						exit(EXIT_FAILURE);
					}

					char control_byte = recv_buffer[0];

					if (control_byte == 0)
						handle->client_conns[i].want_task = 1;

					if (control_byte == 1)
						push_ret_val(handle, i, recv_buffer + 1);

					if (handle->client_conns[i].active_computations != handle->client_conns[i].num_tasks && handle->client_conns[i].want_task == 1)
						handle->client_conns[i].can_write = 1;

					update_connection_management(handle, i);
				}

				if (pending_events[ev].events & EPOLLOUT)
				{
					char send_buffer[SEND_BUFFER_SIZE];

                    int ret = get_task(handle, i, send_buffer);
					if (ret >= 0)
					{
						int bytes_written = send(handle->client_conns[i].socket_fd, send_buffer, SEND_BUFFER_SIZE, MSG_NOSIGNAL);
						if (bytes_written != SEND_BUFFER_SIZE)
						{
							LOG_ERROR("[client_eventloop] Unable to send packet to client#%03zu", i);
							exit(EXIT_FAILURE);
						}
					}

					handle->client_conns[i].can_write = 0;
					update_connection_management(handle, i);

					LOG("[CLUSTER-CLIENT] Sent packets through connection#%03zu", i);
				}
			}
		}
		if (handle->num_unresolved == 0)
			break;
	}

	return NULL;
}

//-------------------------------------
// Initialization and deinitialization
//-------------------------------------

void init_cluster_server(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[init_cluster_server] Nullptr argument");

	// Init subroutines:
	init_discovery_routine            (handle);
	init_connection_management_routine(handle);

	// Create epoll instance:
	handle->epoll_fd = epoll_create1(0);
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

	// Start connection management:
	start_connection_management_routine(handle);

	// Start discovery:
	start_discovery_routine(handle);

	// Log:
	LOG("[CLUSTER-SERVER] Cluster-server initialized");
}

void stop_cluster_server(struct ClusterServerHandle* handle)
{
	BUG_ON(handle == NULL, "[stop_cluster_server] Nullptr argument");

	// Stop eventloop:
	/*int err = pthread_cancel(handle->eventloop_thr_id);
	if (err != 0)
	{
		LOG_ERROR("[stop_cluster_server] pthread_cancel() failed with error %d", err);
		exit(EXIT_FAILURE);
	}*/

	int err = pthread_join(handle->eventloop_thr_id, NULL);
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
	free_discovery_routine            (handle);
	free_connection_management_routine(handle);

	// Log:
	LOG("[CLUSTER-SERVER] Cluster-server stopped");
}
