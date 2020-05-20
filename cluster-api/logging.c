// No Copyright. Vladislav Aleinik 2020
//=====================================
// Logging Utilities
//=====================================
#include "Logging.h"
const char* LOG_FILE = "LOG.log";


void set_log_file(const char* log_file)
{
	if (log_file == NULL)
	{
		time_t cur_time = time(NULL);
		fprintf(stderr, "[ERROR %s\r][set_log_file] Null log file name\n", ctime(&cur_time));
		exit(EXIT_FAILURE);
	}

	LOG_FILE = log_file;

	LOG("Changed log file to %s", log_file);
}