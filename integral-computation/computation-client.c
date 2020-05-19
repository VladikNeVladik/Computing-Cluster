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
		LOG_ERROR("[client] num of arguments != 2");
		exit(EXIT_FAILURE);
	}

	long int num_thr = give_num(argv[1]);
	if (num_thr <= 0)
	{
		LOG_ERROR("[client] error number of threads");
	 	exit(EXIT_FAILURE);
	}

	client_compute(num_thr, sizeof(struct task_data), sizeof(struct ret_data), NULL, integral_thread);

	return EXIT_SUCCESS;
}

long int give_num(const char* str_num)
{
    long int in_num = 0;
    char *end_string;

    errno = 0;
    in_num = strtoll(str_num, &end_string, 10);
    if ((errno != 0 && in_num == 0) || (errno == ERANGE && (in_num == LLONG_MAX || in_num == LLONG_MIN))) {
        printf("Bad string");
        return -2;
    }

    if (str_num == end_string) {
        printf("No number");
        return -3;
    }

    if (*end_string != '\0') {
        printf("Garbage after number");
        return -4;
    }

    if (in_num < 0) {
        printf("i want unsigned num");
        return -5;
    }

    return in_num;
}

double func(double x)
{
	return x * x;
}

void* integral_thread(void* info)
{
	BUG_ON(info == NULL, "[integral_thread] bad argument");

	cpu_set_t cpu;
	pthread_t thread = pthread_self();
	int num_cpu = ((struct thread_info*)info)->num_cpu;

	if (num_cpu > 0)
	{
		CPU_ZERO(&cpu);
		CPU_SET(num_cpu, &cpu);

		int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpu);
		if (ret < 0)
		{
			LOG_ERROR("[integral_thread] set affinity error");
			exit(EXIT_FAILURE);
		}
	}

    BUG_ON(((struct thread_info*)info)->data_pack == NULL, "[integral_thread] bad argument");

    ////////////////////////////////////////////////////////////////////////////
    double delta = ((struct task_data*)(((struct thread_info*)info)->data_pack))->step;
    double end = ((struct task_data*)(((struct thread_info*)info)->data_pack))->end;
    double start = ((struct task_data*)(((struct thread_info*)info)->data_pack))->start;
	int cache_size = ((struct thread_info*)info)->line_size;
	double x = start + delta;

    struct ret_data* out = (struct ret_data*) malloc(cache_size * 2); //cache block size
    if (out == NULL)
	{
		LOG_ERROR("[integral_thread] return alloc error");
		exit(EXIT_FAILURE);
	}

	((struct ret_data*)(((struct thread_info*)info)->ret_pack))->sum = 0.0;

    for (; x < end; x += delta)// Check x and delta in asm version
        ((struct ret_data*)(((struct thread_info*)info)->ret_pack))->sum += func(x) * delta;

    ((struct ret_data*)(((struct thread_info*)info)->ret_pack))->sum += func(start) * delta / 2;
    ((struct ret_data*)(((struct thread_info*)info)->ret_pack))->sum += func(end) * delta / 2;
    ////////////////////////////////////////////////////////////////////////////

	int sem_fd = ((struct thread_info*)info)->event_fd;
	uint64_t val = 1u;
	int ret = write(sem_fd, &val, sizeof(uint64_t));
	if (ret < 0)
	{
		LOG_ERROR("[integral_thread] write fd error");
		exit(EXIT_FAILURE);
	}

    return NULL;
}
