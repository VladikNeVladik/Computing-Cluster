// No Copyright. Vladislav Aleinik 2020

#include "../cluster-api/ClusterServer.h"

#include <stdlib.h>
#include <stdio.h>

const size_t num_tasks   = 128;
const double start_point = 1.0;
const double end_point   = 33.0;
const double diff        = 0.000000001;

typedef struct task_data
{
	double start;
	double end;
	double step;
} task_data;

typedef struct ret_data
{
	double ret_val;
}

int main()
{
	// Set log file:
	set_log_file("log/SERVER-LOG.log");

	task_data* task_buff = (task_data*) calloc(num_tasks, sizeof(task_data));
	BUG_ON(task_buff != NULL, "[computation-server] alloc tasks_buffer");
	ret_data* ret_buff   = (ret_data*)  calloc(num_tasks, sizeof(ret_data));
	BUG_ON(ret_buff  != NULL, "[computation-server] alloc tasks_buffer");

    double diap = (end_point - start_point) / num_tasks;
	for(size_t i = 0; i < num_tasks; i++)
		task_buff[i] =
		{
			.start = start_point + i * diap;
			.end   = start_point + (i + 1) * diap;
			.step  = diff;
		};

	struct ClusterServerHandle server_handle;
	init_cluster_server(&server_handle);

	int ret = compute_task(&server_handle, num_tasks, task_buff, sizeof(task_data), ret_buff, sizeof(ret_data));
    if (ret < 0)
	{
		LOG_ERROR("[compute_task] code error %d", ret);
		exit(EXIT_FAILURE);
	}
    free(task_buff);

	stop_cluster_server(&server_handle);

    double result = 0.0
	for (size_t i = 0; i < num_tasks; i++)
	{
		BUG_ON(ret_buff[i].ret_val != NAN, "[return loop] ret_val of task is NAN");
		result += ret_buff[i].ret_val;
	}
    free(ret_buff);

	printf("result = %lg\n", result);

	return EXIT_SUCCESS;
}
