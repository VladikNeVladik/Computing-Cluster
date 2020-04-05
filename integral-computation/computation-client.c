// No Copyright. Vladislav Aleinik 2020

#include "../cluster-api/ClusterClient.h"

#include <stdlib.h>

int main()
{
	// Set log file:
	set_log_file("log/CLIENT-LOG.log");

	struct ClusterClientHandle client_handle;
	init_cluster_client(&client_handle);

	discover_server(&client_handle);

	stop_cluster_client(&client_handle);

	return EXIT_SUCCESS;
}