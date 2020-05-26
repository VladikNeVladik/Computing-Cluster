// No Copyright. Vladislav Aleinik && Maxim Davydov 2020
//=======================================================
// Computing Cluster Server Implementation
//=======================================================

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
	// Acquire discovery-in socket:
	int sock_fd = socket(AF_INET, SOCK_DGRAM|SOCK_NONBLOCK, 0);
	if (sock_fd == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to create discovery-in socket");
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

	struct sockaddr_in server_addr =
	{
		.sin_family      = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_BROADCAST),
		.sin_port        = htons(DISCOVERY_SERVER_PORT)
	};

	// Performed to recieve datagrams from clients:
	if (bind(sock_fd, &server_addr, sizeof(server_addr)) == -1)
	{
		LOG_ERROR("[init_discovery_routine] Unable to bind()");
		exit(EXIT_FAILURE);
	}

	handle->broadcast_addr = (struct sockaddr_in)
	{
		.sin_family      = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_BROADCAST),
		.sin_port        = htons(DISCOVERY_CLIENT_PORT)
	};

	handle->discovery_socket_fd = sock_fd;
}

static void free_discovery_routine(struct ClusterServerHandle* handle)
{
	if (close(handle->discovery_socket_fd) == -1)
	{
		LOG_ERROR("[free_discovery_routine] Unable to close socket");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("Discovery routine resources freed");
}

void start_discovery_routine(struct ClusterServerHandle* handle)
{
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
		LOG_ERROR("[start_discovery_routine] Unable to register file descriptor for epoll");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("Discovery routine running");
}

static void answer_clients_discovery_datagram(struct ClusterServerHandle* handle)
{
	char buffer[CLIENTS_DISCOVERY_DATAGRAM_SIZE];

	int bytes_read;
	do
	{
		bytes_read = recv(handle->discovery_socket_fd, buffer, CLIENTS_DISCOVERY_DATAGRAM_SIZE, 0);
		if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			LOG_ERROR("[catch_clients_discovery_datagram] Unable to recieve discovery datagram");
			exit(EXIT_FAILURE);
		}

		buffer[CLIENTS_DISCOVERY_DATAGRAM_SIZE - 1] = '\0';

		if (bytes_read == CLIENTS_DISCOVERY_DATAGRAM_SIZE && strcmp(buffer, CLIENTS_DISCOVERY_DATAGRAM) == 0)
		{
			if (sendto(handle->discovery_socket_fd, SERVERS_DISCOVERY_DATAGRAM, SERVERS_DISCOVERY_DATAGRAM_SIZE,
			           MSG_NOSIGNAL, &handle->broadcast_addr, sizeof(handle->broadcast_addr)) == -1)
			{
				LOG_ERROR("[catch_clients_discovery_datagram] Unable to broadcast discovery datagram");
				exit(EXIT_FAILURE);
			}

			LOG("Replied to discovery with \"Here I Am\" broadcast");

			return;
		}
	}
	while (bytes_read != -1);
}

//-----------------------
// Connection Management
//-----------------------

static void init_connection_management_routine(struct ClusterServerHandle* handle)
{
	handle->max_clients = MAX_SIMULTANEOUS_CONNECTIONS;
	
	// Create connection table:
	handle->client_conns = (struct Connection*) malloc(handle->max_clients * sizeof(struct Connection));
	if (handle->client_conns == NULL)
	{
		LOG_ERROR("[init_connection_management_routine] Unable to allocate memory for connections");
		exit(EXIT_FAILURE);
	}

	for (size_t i = 0; i < handle->max_clients; ++i)
	{
		handle->client_conns[i].recv_buffer = (char*) malloc(sizeof(struct RequestHeader) + handle->ret_size);
		if (handle->client_conns[i].recv_buffer == NULL)
		{
			LOG_ERROR("[init_connection_management_routine] Unable to allocate memory for recv-buffer");
			exit(EXIT_FAILURE);
		}

		handle->client_conns[i].bytes_recieved = 0;

		handle->client_conns[i].task_list = (uint32_t*) malloc(MAX_TASKS_PER_CLIENT * sizeof(uint32_t));
		if (handle->client_conns[i].task_list == NULL)
		{
			LOG_ERROR("[init_connection_management_routine] Unable to allocate memory for task list");
			exit(EXIT_FAILURE);
		}

		for (size_t j = 0; j < MAX_TASKS_PER_CLIENT; ++j)
		{
			handle->client_conns[i].task_list[j] = -1;
		}
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

	// Disable the TIME-WAIT state of a socket:
	int setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to set SO_REUSEADDR socket option");
		exit(EXIT_FAILURE);
	}

	// Ask socket to automatically detect disconnection:
	setsockopt_yes = 1;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, &setsockopt_yes, sizeof(setsockopt_yes)) == -1)
	{
		LOG_ERROR("[accept_incoming_connection_request] Unable to set SO_KEEPALIVE socket option");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPIDLE,
	               &TCP_KEEPALIVE_IDLE_TIME, sizeof(TCP_KEEPALIVE_IDLE_TIME)) == -1)
	{
		LOG_ERROR("[accept_incoming_connection_request] Unable to set TCP_KEEPIDLE socket option");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPINTVL,
	               &TCP_KEEPALIVE_INTERVAL, sizeof(TCP_KEEPALIVE_INTERVAL)) == -1)
	{
		LOG_ERROR("[accept_incoming_connection_request] Unable to set TCP_KEEPINTVL socket option");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPCNT, &TCP_KEEPALIVE_NUM_PROBES, sizeof(TCP_KEEPALIVE_NUM_PROBES)) == -1)
	{
		LOG_ERROR("[accept_incoming_connection_request] Unable to set TCP_KEEPCNT socket option");
		exit(EXIT_FAILURE);
	}

	// Set timeout to wait for unaknowledged sends:
	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &TCP_NO_SEND_ACKS_TIMEOUT, sizeof(TCP_NO_SEND_ACKS_TIMEOUT)) == -1)
	{
		LOG_ERROR("[accept_incoming_connection_request] Unable to set TCP_USER_TIMEOUT socket option");
		exit(EXIT_FAILURE);
	}

	// Disable socket lingering:
	struct linger linger_params =
	{
		.l_onoff  = 1,
		.l_linger = 0
	};
	if (setsockopt(sock_fd, SOL_SOCKET, SO_LINGER, &linger_params, sizeof(linger_params)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to disable SO_LINGER socket option");
		exit(EXIT_FAILURE);
	}

	int setsockopt_arg = 0;
	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_LINGER2, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to disable TCP_LINGER2 socket option");
		exit(EXIT_FAILURE);
	}

	// Disable Nagle's algorithm:
	setsockopt_arg = 0;
	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to disable TCP_NODELAY socket option");
		exit(EXIT_FAILURE);
	}

	// Disable corking:
	setsockopt_arg = 0;
	if (setsockopt(sock_fd, IPPROTO_TCP, TCP_CORK, &setsockopt_arg, sizeof(setsockopt_arg)) == -1)
	{
		LOG_ERROR("[start_connection_management_routine] Unable to disable TCP_CORK socket option");
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
	LOG("Connection management routine initialized");
}

static void free_connection_management_routine(struct ClusterServerHandle* handle)
{
	if (close(handle->accept_socket_fd) == -1)
	{
		LOG_ERROR("[free_connection_management_routine] Unable to close accept socket");
		exit(EXIT_FAILURE);
	}

	for (size_t i = 0; i < handle->max_clients; ++i)
	{
		if (handle->client_conns[i].socket_fd != -1)
		{
			if (shutdown(handle->client_conns[i].socket_fd, SHUT_RDWR) == -1)
			{
				LOG_ERROR("[free_connection_management_routine] Unable to shutdown connection#%zu", i);
				exit(EXIT_FAILURE);
			}

			if (close(handle->client_conns[i].socket_fd))
			{
				LOG_ERROR("[free_connection_management_routine] Unable to close connection#%zu", i);
				exit(EXIT_FAILURE);
			}

			free(handle->client_conns[i].recv_buffer);

			free(handle->client_conns[i].task_list);
		}
	}

	free(handle->client_conns);

	// Log:
	LOG("Connection management routine resources freed");
}

static void start_connection_management_routine(struct ClusterServerHandle* handle)
{
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
	LOG("Connection management running");
}

static void pause_connection_management_routine(struct ClusterServerHandle* handle)
{
	// Delete accept-socket file descriptor from epoll:
	struct epoll_event event_config;
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_DEL, handle->accept_socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[pause_connection_management_routine] Unable to delete file descriptor from epoll");
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("Connection management paused");
}

enum
{
	WRITE_DISABLED,
	WRITE_ENABLED
};

static void update_connection_management(struct ClusterServerHandle* handle, size_t client_index, bool can_write)
{
	// Add socket to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->client_conns[client_index].socket_fd
	};
	struct epoll_event event_config =
	{
		.events = EPOLLHUP|EPOLLIN|(can_write ? EPOLLOUT : 0),
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_MOD, handle->client_conns[client_index].socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[update_connection_management] Unable to update connection#%zu in epoll", client_index);
		exit(EXIT_FAILURE);
	}
}

static int accept_incoming_connection_request(struct ClusterServerHandle* handle)
{
	// Accept the request:
	int client_socket_fd = accept4(handle->accept_socket_fd, NULL, NULL, SOCK_NONBLOCK);
	if (client_socket_fd == -1)
	{
		if (errno == EAGAIN) return -1;

		LOG_ERROR("[accept_incoming_connection_request] Unable to accept4() incoming connection request");
		exit(EXIT_FAILURE);
	}

	// Search for a free cell:
	size_t conn_i = -1;
	for (size_t i = 0; i < handle->max_clients; ++i)
	{
		if (handle->client_conns[i].socket_fd == -1)
		{
			conn_i = i;
			handle->num_clients += 1;
			break;
		}
	}

	// Stop accepting connections if there is no memory to keep track of them:
	if (handle->num_clients == handle->max_clients)
	{
		pause_connection_management_routine(handle);
	}

	BUG_ON(conn_i == -1, "[accept_incoming_connection_request] No free cell in the connection array");

	handle->client_conns[conn_i].socket_fd           = client_socket_fd;
	handle->client_conns[conn_i].bytes_recieved      = 0;
	handle->client_conns[conn_i].now_sending_task_i  = -1;
	handle->client_conns[conn_i].requested_tasks     = 0;

	// Add the socket to epoll:
	epoll_data_t event_data =
	{
		.fd = handle->client_conns[conn_i].socket_fd
	};
	struct epoll_event event_config =
	{
		.events = EPOLLIN|EPOLLOUT|EPOLLHUP|EPOLLRDHUP,
		.data   = event_data
	};
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_ADD, handle->client_conns[conn_i].socket_fd, &event_config) == -1)
	{
		LOG_ERROR("[accept_incoming_connection_request] Unable to add connection#%zu to epoll", conn_i);
		exit(EXIT_FAILURE);
	}

	// Log:
	LOG("Accepted connection request. Connection#%zu now active", conn_i);

	return 0;
}

static void delete_connection(struct ClusterServerHandle* handle, size_t conn_i)
{
	// Start accepting incoming connections:
	if (handle->num_clients == handle->max_clients)
	{
		start_connection_management_routine(handle);
	}

	// Delete connection socket from epoll:
	if (epoll_ctl(handle->epoll_fd, EPOLL_CTL_DEL, handle->client_conns[conn_i].socket_fd, NULL) == -1)
	{
		LOG_ERROR("[delete_connection] Unable to delete connection#%zu from epoll", conn_i);
		exit(EXIT_FAILURE);
	}

	if (close(handle->client_conns[conn_i].socket_fd) == -1)
	{
		LOG_ERROR("[delete_connection] Unable to close connection#%zu socket", conn_i);
		exit(EXIT_FAILURE);
	}

	handle->client_conns[conn_i].socket_fd = -1;

	handle->num_clients -= 1;

	// Log:
	LOG("Deleted connection#%zu", conn_i);
}

static void read_data_on_connection(struct ClusterServerHandle* handle, size_t conn_i, struct RequestHeader* header)
{
	// Manage recv-buffer:
	char* buffer_pos = handle->client_conns[conn_i].recv_buffer + handle->client_conns[conn_i].bytes_recieved;
	
	size_t recv_buffer_size = sizeof(struct RequestHeader) + handle->ret_size;
	size_t bytes_to_read    = recv_buffer_size - handle->client_conns[conn_i].bytes_recieved;

	// Recv:
	int bytes_read = recv(handle->client_conns[conn_i].socket_fd, buffer_pos, bytes_to_read, MSG_WAITALL);
	if (bytes_read == -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			header->cmd = ERR_NOT_READY;
			return;
		}

		if (errno == ECONNRESET)
		{
			header->cmd = ERR_CONN_BROKEN;
			return;
		}

		BUG_ON(1, "[read_data_on_connection] Unable to recv() command from server");
	}
	if (bytes_read == 0)
	{
		header->cmd = ERR_CONN_BROKEN;
		return;
	}

	handle->client_conns[conn_i].bytes_recieved += bytes_read;

	if (handle->client_conns[conn_i].bytes_recieved == recv_buffer_size)
	{
		handle->client_conns[conn_i].bytes_recieved = 0;

		uint32_t* task_id = (uint32_t*) &handle->client_conns[conn_i].recv_buffer[1];

		header->cmd     = handle->client_conns[conn_i].recv_buffer[0];
		header->task_id = be32toh(*task_id);
	}
	else
	{
		header->cmd = ERR_NOT_READY;
	}
}

static void put_data_to_connection(struct ClusterServerHandle* handle, size_t conn_i, size_t task_list_i, struct RequestHeader* header)
{
	BUG_ON(header->cmd != CMD_TASK, "[put_data_to_connection] Wrong command");
	BUG_ON(256 <= sizeof(*header) + handle->task_size, "[put_data_to_connection] Send buffer too small");
	BUG_ON(task_list_i == -1, "[put_data_to_connection] Invalid task index");

	// Fill in the send buffer:
	char send_buffer[256];

	// Put header in the send buffer:
	memcpy(send_buffer, header, sizeof(*header));
	
	// Put payload in the send buffer:
	uint32_t task_global_i = handle->client_conns[conn_i].task_list[task_list_i];
	char* data_to_send = handle->user_tasks + task_global_i * handle->task_size;

	memcpy(send_buffer + sizeof(*header), data_to_send, handle->task_size);

	// Preform send:
	int bytes_written = send(handle->client_conns[conn_i].socket_fd, send_buffer, sizeof(*header) + handle->task_size, MSG_NOSIGNAL);
	if (bytes_written != sizeof(*header) + handle->task_size)
	{
		if (bytes_written == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
			header->cmd = ERR_NOT_READY;
			return;
		}

		if (bytes_written == -1 && errno == EPIPE)
		{
			header->cmd = ERR_CONN_BROKEN;
			return;
		}

		BUG_ON(1, "[put_data_to_connection] Unhandled error in send()");
	}

}

//-------------------------
// Task Management Routine
//-------------------------

static void init_task_management_routine(struct ClusterServerHandle* handle)
{
	handle->task_manager = (struct TaskInfo*) calloc(handle->num_tasks, sizeof(struct TaskInfo));
	if (handle->task_manager == NULL)
	{
		LOG_ERROR("[init_task_management_routine] alloc task manager");
		exit(EXIT_FAILURE);
	}

    for(size_t i = 0; i < handle->num_tasks; i++)
	{
		handle->task_manager[i].task   = handle->user_tasks + i * handle->task_size;
		handle->task_manager[i].ret    = handle->user_rets  + i * handle->ret_size;
		handle->task_manager[i].status = NOT_RESOLVED;
	}

	handle->num_not_resolved      = handle->num_tasks;
	handle->num_completed         = 0;
	handle->min_not_resolved_task = 0;

	// Log:
	LOG("Task management routine initialised");
}

static void free_task_management_routine(struct ClusterServerHandle* handle)
{
	free(handle->task_manager);

	// Log:
	LOG("Task management routine resources freed");
}

static size_t get_task_to_send(struct ClusterServerHandle* handle, size_t conn_i)
{
	// Find a task:
	size_t task_i = handle->min_not_resolved_task;
	for (; task_i < handle->num_tasks; task_i++)
	{
		if (handle->task_manager[task_i].status == NOT_RESOLVED)
		{
			handle->task_manager[task_i].status = RESOLVING;
			handle->num_not_resolved -= 1;

			handle->min_not_resolved_task = task_i + 1;
			break;
		}
	}

	BUG_ON(task_i == handle->num_tasks, "[get_task_to_send] Greedy client detected");

	// Find place in a task list:
	for (size_t task_list_i = 0; task_list_i < MAX_TASKS_PER_CLIENT; task_list_i++)
	{
		if (handle->client_conns[conn_i].task_list[task_list_i] == -1)
		{
			handle->client_conns[conn_i].task_list[task_list_i] = task_i;
			return task_list_i;
		}
	}

	BUG_ON(1, "[get_task_to_send] Asking for a task while there is no room for it");
}


static void push_return_value(struct ClusterServerHandle* handle, size_t conn_i, struct RequestHeader* header)
{
	size_t task_index = header->task_id;

	// Copy return value:
	memcpy(handle->task_manager[task_index].ret, handle->client_conns[conn_i].recv_buffer + sizeof(*header), handle->ret_size);
	
	// Free cell in the task list:
	for (size_t i = 0; i < MAX_TASKS_PER_CLIENT; i++)
	{
		if (handle->client_conns[conn_i].task_list[i] == task_index)
		{
			handle->client_conns[conn_i].task_list[i] = -1;
			break;
		}

		BUG_ON(i + 1 == MAX_TASKS_PER_CLIENT, "[push_return_value] Can't find number of task in the task list");
	}

	// Update global state:
	handle->task_manager[task_index].status = COMPLETED;
	handle->num_completed += 1;

	LOG("Recieved results of task#%ld", task_index);
}

static void drop_resolving_tasks(struct ClusterServerHandle* handle, size_t conn_i)
{
	for (int i = 0; i < MAX_TASKS_PER_CLIENT; i++)
	{
		int task_i = handle->client_conns[conn_i].task_list[i];
		handle->client_conns[conn_i].task_list[i] = -1;

		if (task_i != -1)
		{
			handle->task_manager[task_i].status = NOT_RESOLVED;
			handle->num_not_resolved += 1;

			if (task_i < handle->min_not_resolved_task)
			{
				handle->min_not_resolved_task = task_i;
			}

			LOG("Returning task#%d to task pool", task_i);
		}
	}
}

//------------------
// Server Eventloop
//------------------

static void* server_eventloop(void* arg)
{
	struct ClusterServerHandle* handle = arg;

	const size_t MAX_EVENTS = 16;
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
			// Perform discovery:
			if (pending_events[ev].data.fd == handle->discovery_socket_fd && pending_events[ev].events & EPOLLIN)
			{
				answer_clients_discovery_datagram(handle);
			}

			// Manage connection deaths:
			if (pending_events[ev].events & EPOLLHUP || pending_events[ev].events & EPOLLERR)
			{
				for (size_t conn_i = 0; conn_i < handle->max_clients; ++conn_i)
				{
					if (pending_events[ev].data.fd != handle->client_conns[conn_i].socket_fd) continue;

					LOG("Connection#%zu hangup detected", conn_i);
					
					drop_resolving_tasks(handle, conn_i);
					delete_connection   (handle, conn_i);

					if (handle->num_clients == 0)
					{
						LOG("No workers left. Qutting");
						exit(EXIT_SUCCESS);
					}

					continue;
				}
			}

			// Accept incoming connection request:
			if (pending_events[ev].data.fd == handle->accept_socket_fd)
			{
				while (accept_incoming_connection_request(handle) == 0);
			}

			// Manage recieves:
			if (pending_events[ev].events & EPOLLIN)
			{
				for (size_t conn_i = 0; conn_i < handle->max_clients; ++conn_i)
				{
					if (handle->client_conns[conn_i].socket_fd != pending_events[ev].data.fd) continue;

					struct RequestHeader header; 
					read_data_on_connection(handle, conn_i, &header);

					switch (header.cmd)
					{
						case CMD_REQUEST_FOR_DATA:
						{
							handle->client_conns[conn_i].requested_tasks += 1;
							break;
						}
						case CMD_RESULT:
						{
							push_return_value(handle, conn_i, &header);

							handle->client_conns[conn_i].requested_tasks += 1;
							break;
						}
						case ERR_CONN_BROKEN:
						{
							LOG("Dropping connection#%zu because of recieve error", conn_i);

							drop_resolving_tasks(handle, conn_i);
							delete_connection   (handle, conn_i);
							continue;
						}
						default: // case ERR_NOT_READY:
						{
							BUG_ON(header.cmd != ERR_NOT_READY, "[server_eventloop] Unknow command recieved");
							goto skip_connection_management_enable;
						}
					}

					// Allow write when CMD_TASK can be sent:
					if (handle->num_not_resolved != 0)
					{
						update_connection_management(handle, conn_i, WRITE_ENABLED);
					}

					skip_connection_management_enable:
					continue;
				}
			}

			// Manage sends:
			if (pending_events[ev].events & EPOLLOUT)
			{
				// Manage sends:
				for (size_t conn_i = 0; conn_i < handle->max_clients; ++conn_i)
				{
					if (pending_events[ev].data.fd != handle->client_conns[conn_i].socket_fd) continue;

					// Send all requested tasks of one connection: 
					while (handle->client_conns[conn_i].requested_tasks != 0)
					{
						if (handle->client_conns[conn_i].now_sending_task_i == -1)
						{
							handle->client_conns[conn_i].now_sending_task_i = get_task_to_send(handle, conn_i);
						}

						size_t task_list_i = handle->client_conns[conn_i].now_sending_task_i;

						struct RequestHeader header =
						{
							.cmd     = CMD_TASK,
							.task_id = htobe32(handle->client_conns[conn_i].task_list[task_list_i])
						};

						put_data_to_connection(handle, conn_i, task_list_i, &header);

						// Handle operation results:
						switch (header.cmd)
						{
							case CMD_TASK:
							{
								LOG("Sent task#%u to client on connection#%zu", handle->client_conns[conn_i].task_list[task_list_i], conn_i);
								
								handle->client_conns[conn_i].now_sending_task_i = -1;
								handle->client_conns[conn_i].requested_tasks -= 1;
								
								break;
							}
							case ERR_CONN_BROKEN:
							{
								LOG("Dropping connection#%zu because of send error", conn_i);

								drop_resolving_tasks(handle, conn_i);
								delete_connection   (handle, conn_i);
								goto skip_connection_management_disable;
							}
							default: // case ERR_NOT_READY:
							{
								BUG_ON(header.cmd != ERR_NOT_READY, "[server_eventloop] Unknown command recieved");
								goto skip_connection_management_disable;
							}
						}

						// Disable write:
						update_connection_management(handle, conn_i, WRITE_DISABLED);
						
						skip_connection_management_disable:
						continue;
					}
				}
			}
		}

		// Finish if all the clients left:
		if (handle->num_completed == handle->num_tasks) break;
	}

	return NULL;
}

//-------------------------------------
// Initialization and deinitialization
//-------------------------------------

void init_cluster_server(struct ClusterServerHandle* handle)
{
	// Init subroutines:
	init_discovery_routine            (handle);
	init_connection_management_routine(handle);

	init_task_management_routine(handle);

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
	LOG("Cluster-server initialized");
}

void stop_cluster_server(struct ClusterServerHandle* handle)
{
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

	free_task_management_routine(handle);

	// Log:
	LOG("Cluster-server stopped");
}


int compute_task(size_t num_tasks, void* tasks, size_t task_size, void* rets, size_t ret_size)
{
	BUG_ON(num_tasks == 0, "[compute_task] Error tasks number");
	BUG_ON(tasks == NULL,  "[compute_task] Task buffer is nullptr");
	BUG_ON(task_size == 0, "[compute_task] Invalid task size");
	BUG_ON(rets == NULL,   "[compute_task] Ret buffer is nullptr");
	BUG_ON(ret_size == 0,  "[compute_task] Invalid ret size");

	struct ClusterServerHandle handle;

	handle.num_tasks  = num_tasks;
	handle.user_tasks = tasks;
	handle.task_size  = task_size;
	handle.user_rets  = rets;
	handle.ret_size   = ret_size;

	init_cluster_server(&handle);

	stop_cluster_server(&handle);

	return 0;
}
