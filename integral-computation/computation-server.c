// No Copyright. Vladislav Aleinik 2020

#include "../cluster-api/ClusterServer.h"

#include <stdlib.h>
#include <stdio.h>

int main()
{
	// Set log file:
	set_log_file("log/SERVER-LOG.log");

	struct ClusterServerHandle server_handle;
	init_cluster_server(&server_handle);

	start_discovery_routine(&server_handle);
	
	while (1)
	{
		char cmd = getchar();
		if (cmd == ' ') break;
	}

	stop_cluster_server(&server_handle);

	return EXIT_SUCCESS;
}