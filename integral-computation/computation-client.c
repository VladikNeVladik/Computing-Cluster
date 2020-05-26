// No Copyright. Vladislav Aleinik && Maxim Davydov 2020

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

	if (argc != 2 && argc != 3)
	{
		LOG_ERROR("Usage: computation-client <number of worker threads> [<server hostname>]");
		exit(EXIT_FAILURE);
	}

	char* endptr = argv[1];
	long long num_thr = strtoll(argv[1], &endptr, 10);
	if (*argv[1] == '\0' || *endptr != '\0')
	{
		LOG_ERROR("Usage: Invalid number of worker threads");
	 	exit(EXIT_FAILURE);
	}

	const char* server_hostname = NULL;
	if (argc == 3) server_hostname = argv[2];

	client_compute(num_thr, sizeof(struct task_data), sizeof(struct ret_data), integral_thread, server_hostname);

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

    double start = data_pack->start;
    double end   = data_pack->end;
    double delta = data_pack->step;

	// Fix endianness:
    uint64_t* ptr_start = (uint64_t*) &start;
	uint64_t* ptr_end   = (uint64_t*) &end;
    uint64_t* ptr_delta = (uint64_t*) &delta;

	*ptr_start = htobe64(*ptr_start);
	*ptr_end   = htobe64(*ptr_end  );
	*ptr_delta = htobe64(*ptr_delta);

	double sum = 0.0;
    for (double x = start + delta; x < end; x += delta)
    {
        sum += func(x) * delta;
    }

	sum += func(start) * delta / 2;
    sum += func(  end) * delta / 2;

    // Return results:
	struct ret_data* ret_data = info->ret_pack;
    ret_data->sum = sum;

	return ret_data;
}
