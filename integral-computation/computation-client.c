// No Copyright. Vladislav Aleinik 2020

#include "../cluster-api/ClusterClient.h"

#include <stdlib.h>

typedef struct task_data
{
	double start;
	double end;
	double step;
} task_data;

typedef struct ret_data
{
	double sum;
} ret_data;

int main(int argc, char* argv[])
{
	// Set log file:
	set_log_file("log/CLIENT-LOG.log");

	if (argc != 2)
	{
		LOG_ERROR("[client] num of arguments != 2");
		exit(EXIT_FAILURE);
	}

	long int num_thr = give_num(argv[1]);
    if (num_thr <= 0)
	{
		LOG_ERROR("[client] error number of threads");
		exit(EXIT_FAILURE);
	}

	struct ClusterClientHandle client_handle;
	init_cluster_client(&client_handle);

    client_compute(&client_handle, num_threads, sizeof(task_data), sizeof(ret_data));

	stop_cluster_client(&client_handle);

	return EXIT_SUCCESS;
}

void* integral_thread(void* info)
{
    BUG_ON(info != NULL, "[integral_thread] bad argument");

    cpu_set_t cpu;
    pthread_t thread = pthread_self();
    int num_cpu = ((thread_info*)info)->num_cpu;

    if (num_cpu > 0)
    {
        CPU_ZERO(&cpu);
        CPU_SET(num_cpu, &cpu);

        errno = 0;
        int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpu);
        if (ret < 0)
        {
            LOG_ERROR("[integral_thread] set affinity error");
            exit(EXIT_FAILURE);
        }
    }

    BUG_ON(((thread_info*)info)->data_pack != NULL, "[integral_thread] bad argument");

    ////////////////////////////////////////////////////////////////////////////
    double delta = ((task_data*)(((thread_info*)info)->data_pack))->step;
    double end = ((task_data*)(((thread_info*)info)->data_pack))->end;
    double start = ((task_data*)(((thread_info*)info)->data_pack))->start;
	int cache_size = ((thread_info*)info)->line_size;
	double x = start + delta;

    ret_data* out = (ret_data*) malloc(cache_size * 2); //cache block size
    if (out == NULL)
	{
		LOG_ERROR("[integral_thread] return alloc error");
		exit(EXIT_FAILURE);
	}

	out->sum = 0.0;

    for (; x < end; x += delta)// Check x and delta in asm version
        out->sum += func(x) * delta;

    out->sum += func(start) * delta / 2;
    out->sum += func(end) * delta / 2;
    ////////////////////////////////////////////////////////////////////////////

    errno = 0;
	int sem_fd = ((thread_info*)info)->event_fd;
	uint64_t val = 1u;
	int ret = write(sem_fd, &val, sizeof(uint64_t));
	if (ret < 0)
	{
		LOG_ERROR("[integral_thread] write fd error = %d", errno);
		exit(EXIT_FAILURE);
	}


    return out;
}
