// No Copyright. Vladislav Aleinik 2020

#include "../cluster-api/ClusterServer.h"

#include <stdlib.h>
#include <stdio.h>

const size_t num_tasks   = 24;
const double start_point = 1.0;
const double end_point   = 51.0;
const double diff        = 0.00000001;

struct task_data
{
	double start;
	double end;
	double step;
};

struct ret_data
{
	double ret_val;
};

int main(int argc, char* argv[])
{
	if (argc != 1)
	{
		LOG_ERROR("Usage: computation-client");
		exit(EXIT_FAILURE);
	}

	// Set log file:
	set_log_file("log/SERVER-LOG.log");

	struct task_data* task_buff = (struct task_data*) calloc(num_tasks, sizeof(*task_buff));
	BUG_ON(task_buff == NULL, "[computation-server] Alloc tasks_buffer");
	struct ret_data* ret_buff   = (struct ret_data*)  calloc(num_tasks, sizeof(*ret_buff));
	BUG_ON(ret_buff  == NULL, "[computation-server] Alloc tasks_buffer");

	double diap = (end_point - start_point) / num_tasks;
	for(size_t i = 0; i < num_tasks; i++)
	{
		task_buff[i].start = start_point + i * diap;
		task_buff[i].end   = start_point + (i + 1) * diap;
		task_buff[i].step  = diff;
	}

	int ret = compute_task(num_tasks, task_buff, sizeof(*task_buff), ret_buff, sizeof(*ret_buff));
	if (ret < 0)
	{
		LOG_ERROR("[compute_task] Code error %d", ret);
		exit(EXIT_FAILURE);
	}

	free(task_buff);

	double result = 0.0;
	for (size_t i = 0; i < num_tasks; i++)
	{
		// HOT FIX !!!!
		BUG_ON(ret_buff[i].ret_val != ret_buff[i].ret_val/*!!!! NAN !!!!*/, "[return loop] ret_val of task is NAN");
		result += ret_buff[i].ret_val;
	}
	free(ret_buff);

	LOG("Result = %lg", result);

	return EXIT_SUCCESS;
}
