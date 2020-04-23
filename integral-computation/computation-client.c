// No Copyright. Vladislav Aleinik 2020

#include "../cluster-api/ClusterClient.h"

#include <stdlib.h>

int main()
{
	// Set log file:
	set_log_file("log/CLIENT-LOG.log");

	struct ClusterClientHandle client_handle;
	init_cluster_client(&client_handle);

	while (1)
	{
		char cmd = getchar();
		if (cmd == ' ') break;
	}

	stop_cluster_client(&client_handle);

	return EXIT_SUCCESS;
}