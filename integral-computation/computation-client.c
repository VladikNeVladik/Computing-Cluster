// No Copyright. Vladislav Aleinik 2020

#include "../cluster-api/ClusterClient.h"

#include <stdlib.h>
#include <errno.h>
#include <limits.h>


struct task_data
{
	double start;
	double end;
	double step;
};

struct ret_data
{
	double sum;
};

void* integral_thread(void* info);
long int give_num(const char* str_num);

int main(int argc, char* argv[])
{
	// Set log file:
	set_log_file("log/CLIENT-LOG.log");

	if (argc != 2)
	{
		LOG_ERROR("Usage: computation-client <number of worker threads> ");
		exit(EXIT_FAILURE);
	}

	char* endptr = argv[1];
	long long num_thr = strtoll(argv[1], &endptr, 10);
	if (*argv[1] == '\0' || *endptr != '\0')
	{
		LOG_ERROR("Usage: Invalid number of worker threads");
	 	exit(EXIT_FAILURE);
	}

	client_compute(num_thr, sizeof(struct task_data), sizeof(struct ret_data), NULL, integral_thread);

	return EXIT_SUCCESS;
}

double func(double x)
{
	return x * x;
}

void* integral_thread(void* arg)
{
	BUG_ON(arg == NULL, "[integral_thread] Bad argument");

	struct ComputeInfo* info = arg;

    // Computation:
    struct task_data* data_pack = info->data_pack;
    BUG_ON(data_pack == NULL, "[integral_thread] Bad argument");


    double delta   = data_pack->step;
    double end     = data_pack->end;
    double start   = data_pack->start;
	double x       = start + delta;

	struct ret_data* ret_data = info->ret_pack;

	ret_data->sum = 0.0;

    for (; x < end; x += delta)// Check x and delta in asm version
        ret_data->sum += func(x) * delta;

    ret_data->sum += func(start) * delta / 2;
    ret_data->sum += func(end) * delta / 2;

	return NULL;
}
