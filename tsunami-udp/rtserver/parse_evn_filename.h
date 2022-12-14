#ifndef PARSE_EVN_FILENAME
#define PARSE_EVN_FILENAME

#include <time.h>
#include <sys/time.h>

struct evn_filename {
	char    *exp_name;
	char    *station_code;
	char    *scan_name;
	char    *data_start_time_ascii;
	double   data_start_time;         /* system tick seconds time_t (integer) or with fractional seconds (double) */
	char   **auxinfo;                 /* pointer to (array of pointers to aux info elements) */
	int      nr_auxinfo;
	char    *file_type;
    char     valid;
};

struct evn_filename *parse_evn_filename(char *filename);

char *get_aux_entry(char *key, char **auxinfo, int nr_auxinfo);

#endif

/*
 * $Log$
 * Revision 1.2  2006/10/25 12:14:07  jwagnerhki
 * added cvs log line
 *
 */ 


