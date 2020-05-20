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

	struct ThreadInfo* info = arg;

	cpu_set_t cpu;
	pthread_t thread = pthread_self();
	int num_cpu = info->num_cpu;

	if (num_cpu > 0)
	{
		CPU_ZERO(&cpu);
		CPU_SET(num_cpu, &cpu);

		int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpu);
		if (ret < 0)
		{
			LOG_ERROR("[integral_thread] Set affinity error");
			exit(EXIT_FAILURE);
		}
	}

    // Computation:
    struct task_data* data_pack = info->data_pack;
    BUG_ON(data_pack == NULL, "[integral_thread] Bad argument");


    double delta   = data_pack->step;
    double end     = data_pack->end;
    double start   = data_pack->start;
	int cache_size = info->line_size;
	double x       = start + delta;

    struct ret_data* out = (struct ret_data*) malloc(cache_size * 2); //cache block size
    if (out == NULL)
	{
		LOG_ERROR("[integral_thread] Return alloc error");
		exit(EXIT_FAILURE);
	}

	struct ret_data* ret_data = info->ret_pack;

	ret_data->sum = 0.0;

    for (; x < end; x += delta)// Check x and delta in asm version
        ret_data->sum += func(x) * delta;

    ret_data->sum += func(start) * delta / 2;
    ret_data->sum += func(end) * delta / 2;

    // Return results:
	int sem_fd = info->event_fd;
	uint64_t val = 1u;
	int ret = write(sem_fd, &val, sizeof(uint64_t)); // What the FUCK, Max?
	if (ret < 0)
	{
		LOG_ERROR("[integral_thread] Write fd error");
		exit(EXIT_FAILURE);
	}

    return NULL;
}
