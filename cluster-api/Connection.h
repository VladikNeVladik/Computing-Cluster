// No Copyright. Vladislav Aleinik 2020
//==============================================
// Computing Cluster Server
//==============================================
// - Performs time-to-time client node discovery
// - Gives out computational tasks
// - Aggregates computation results
//==============================================
#ifndef COMPUTING_CLUSTER_CONNECTION_HPP_INCLUDED
#define COMPUTING_CLUSTER_CONNECTION_HPP_INCLUDED

static const unsigned CONN_BUFFER_SIZE = 1024;
struct Connection
{
	int sock_fd;

	char buffer[CONN_BUFFER_SIZE];
	unsigned head;
	unsigned tail;
};

void send_to_conn(char* buf, unsigned size)
{
	// Will block until send is possible.
}

void recv_from_conn(char* buf, unsigned size)
{
	// Will block until there is enough data.
}

#endif  // COMPUTING_CLUSTER_CONNECTION_HPP_INCLUDED