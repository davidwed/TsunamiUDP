/*========================================================================
 * parse_evn_filename.c  --  EVN filename parser routines
 *
 * Accepts filenames not entirely EVN filename format compliant.
 *
 * Accepted formats:
 *   [experimentName]_[stationCode]_[scanName]_[startTimeUTCday].vsi
 *   [experimentName]_[stationCode]_[scanName]_[startTimeUTCday]_[auxinfo1..N=val1..N].vsi
 *
 * Auxinfo:
 *   These auxiliary infos are parsed outside of parse_evn_filename.c,
 *   right now only in protocol.c
 *
 *   Currently used auxinfos are, for example:
 *      sr[n]     - samplerate
 *      sl[n]     - total slots nr in time multiplexing (==how many servers)
 *      sn[n]     - slot nr in time multiplexing
 *      flen[n]   - length of data to send
 *
 * Example filenames:
 *   gre53_ef_scan035_154d12h43m10s.vsi
 *   gre53_ef_scan035_154d12h43m10s_flen=14400000.vsi
 *
 *========================================================================
 */
  
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include "parse_evn_filename.h"

void add_aux_entry(struct evn_filename *ef, char *auxentry) {
	char **p;
	ef->nr_auxinfo++;
	p = realloc(ef->auxinfo, sizeof(char*) * ef->nr_auxinfo);
	assert(p);
	ef->auxinfo = p;
	ef->auxinfo[ef->nr_auxinfo - 1] = auxentry;
}

	
double year_to_utc(int year) {
	struct tm tm;
	/*	memset(&tm, 0, sizeof(struct tm)); */
	tm.tm_year = year - 1900;
        tm.tm_mon = 0;
        tm.tm_mday = 1;
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_isdst = 0;
	return (double)mktime(&tm);
}

int get_current_year() {
	time_t t;
	struct tm *tm;
	time(&t);
	tm = gmtime(&t);
	return tm->tm_year + 1900;
}

double day_to_utc(int day) { return (day-1)*24*60*60; }
double hour_to_utc(int hour) { return hour*60*60; }
double minute_to_utc(int minute) { return minute*60; }

int parse_time(const char *str, double *retval) {
	int yyyy, mm, dd, hh, min, yday, sec;
    double dsec;
	int consumed;

    if (sscanf(str, "%4d-%2d-%2dT%2d:%2d:%lg%n",  /* ISO basic extended */
             &yyyy, &mm, &dd,
             &hh, &min, &dsec, 
             &consumed) == 6 && consumed == strlen(str)) {
        struct tm tt;
        time_t temp;
        // convert to fractional Unix seconds
        tt.tm_sec = (int)floor(dsec);
        tt.tm_min = min;
        tt.tm_hour = hh;
        tt.tm_mday = dd;
        tt.tm_mon = mm - 1;
        tt.tm_year = yyyy - 1900;
        tt.tm_isdst = 0;
        temp = mktime(&tt);
        *retval = (double)temp + (dsec - floor(dsec));
        fprintf(stderr, "Detected time format: ISO basic extended\n");
        return 0;        
    }
	if (sscanf(str, "%4dy%dd%n",
		   &yyyy,
		   &yday,
		   &consumed) == 2 && consumed == strlen(str)) {
		*retval = year_to_utc(yyyy) + day_to_utc(yday);
        fprintf(stderr, "Detected time format: [yyyy]y[dd]d\n");
		return 0;
	}
	if (sscanf(str, "%4d%d%n",
			  &yyyy,
			  &yday,
			  &consumed) == 2 && consumed == strlen(str)) {
		*retval = year_to_utc(yyyy) + day_to_utc(yday);
        fprintf(stderr, "Detected time format: yyyydd\n");
        return 0;
	}
	if (sscanf(str, "%dd%dh%dm%ds%n",
			  &yday,
			  &hh,
			  &mm,
			  &sec,
			  &consumed) == 4 && consumed == strlen(str)) {
		*retval =  year_to_utc(get_current_year()) + day_to_utc(yday)
			+ hour_to_utc(hh) + minute_to_utc(mm) + sec;
                printf("%e %e %e %e %e\n", year_to_utc(get_current_year())
                    ,day_to_utc(yday), hour_to_utc(hh), minute_to_utc(mm),
                    (double)sec);
        fprintf(stderr, "Detected time format: [d]d[h]h[m]m[s]s\n");
		return 0;
	}
    fprintf(stderr, "Warning: string with unknown time format passed to parse_time().\n");
	return 1;
}


char *get_aux_entry(char *key, char **auxinfo, int nr_auxinfo) {
	int i;
	for (i=0; i<nr_auxinfo; i++) {
		
		if(strlen(auxinfo[i]) > strlen(key)
		   &&!strncmp(auxinfo[i], key, strlen(key))
		   && auxinfo[i][strlen(key)] == '=') {
			return strdup(auxinfo[i]+strlen(key)+1);
		}
	}
	return NULL;
}
			
/*
 * Return one token from underscore delimited string.
 * @param pointer to pointer to part of the string that is currently
 * parsed. Function updates the pointer so that it points to the next
 * element.
 * @return element allocated with malloc(), caller must free() it.
 */
char *get_token(char **str) {
	char *p, *retval;
	if (!*str || !*str[0])
		return NULL;
	p = strchr(*str, (int)'_');
	if (!p) {
		p = strchr(*str, (int)'\0');
		if(!p) return NULL; // assert(p);
		retval = strdup(*str);
		*str = p;
	} else {
		retval = (char*)strndup(*str, p - *str);
		*str = p + 1;
	}
	return retval;
}
/* Parse EVN filename
 * @param pointer to filename
 * @return malloc()'d struct that contains malloc()'d filename elements.
 */
struct evn_filename *parse_evn_filename(char *filename) {
	struct evn_filename *ef;
	char *parsebuf, *parseptr;
	
	ef = calloc(sizeof(struct evn_filename), 1);
	assert(ef);

	parseptr = parsebuf = strdup(filename);
	assert(parsebuf);

    ef->data_start_time_ascii = NULL;
    
	/* Extract filetype from parsebuf. Overwrite dot with zero so
	   that filetype does not complicate parsing of other parts.*/
	{
		char *dot, *filetype;
		dot = strrchr(parseptr, (int)'.');
        // assert(dot);
		if (!dot) { fprintf(stderr, "parse_evn_filename: assert(dot)\n"); return ef; }
		filetype = dot + 1;
		ef->file_type = get_token(&filetype);
        // assert(ef->file_type);
		if(!ef->file_type) { fprintf(stderr, "parse_evn_filename: assert(ef->file_type)\n"); return ef; } 
        // assert(strlen(ef->file_type)>=2);
		if(strlen(ef->file_type) < 2) { fprintf(stderr, "parse_evn_filename: assert(strlen(ef->file_type)>=2)\n"); return ef; } 
		*dot = 0;
	}

	ef->exp_name = get_token(&parseptr);
    // assert(ef->exp_name);
    if(!ef->exp_name) { fprintf(stderr, "parse_evn_filename: assert(ef->exp_name)\n"); return ef; }
    // assert(assert(strlen(ef->exp_name) <= 6););
	if(strlen(ef->exp_name) > 6) { fprintf(stderr, "parse_evn_filename: assert(strlen(ef->exp_name) <= 6)\n"); return ef; }

	ef->station_code = get_token(&parseptr);
    // assert(ef->station_code);
    if(!ef->station_code) { fprintf(stderr, "parse_evn_filename: assert(ef->station_code)\n"); return ef; }
    //assert(strlen(ef->station_code) >= 2);
    if(strlen(ef->station_code) < 2) { fprintf(stderr, "parse_evn_filename: assert(strlen(ef->station_code) >= 2)\n"); return ef; }

	ef->scan_name = get_token(&parseptr);
    //assert(ef->scan_name);
    if(!ef->scan_name) { fprintf(stderr, "parse_evn_filename: assert(ef->scan_name)\n"); return ef; }
    //assert(strlen(ef->scan_name) <= 16);
    if(strlen(ef->scan_name) > 16) { fprintf(stderr, "parse_evn_filename: assert(strlen(ef->scan_name) <= 16)\n"); return ef; }

	/* All mandatory elements read. */

	ef->data_start_time_ascii = get_token(&parseptr);
	if (ef->data_start_time_ascii) {
        //assert(strlen(ef->data_start_time_ascii) >= 2);
        if(strlen(ef->data_start_time_ascii) < 2) { 
            ef->data_start_time_ascii=NULL; 
            fprintf(stderr, "parse_evn_filename: assert(strlen(ef->data_start_time_ascii) >= 2)\n");            
            return ef; 
        } 
        if (parse_time(ef->data_start_time_ascii, &ef->data_start_time)) {
            /* Does not look like date, must be auxentry instead. */
            add_aux_entry(ef, ef->data_start_time_ascii);
            ef->data_start_time_ascii = NULL;
		}
	}

	{
		char *auxentry;
		while ((auxentry = get_token(&parseptr)) != NULL)
			add_aux_entry(ef, auxentry);
	}

	free(parsebuf);
	return ef;
}

#ifdef UNIT_TEST
int main(int argc, char **argv) {
	struct evn_filename *ef;
	int i;
    u_int64_t li;
    do {    
	   if (argc < 2) {
		  printf("parsing gre53_ef_scan035_154d12h43m10s.vsi\n");
		  ef = parse_evn_filename("gre53_ef_scan035_154d12h43m10s.vsi");
	   } else {
		  ef = parse_evn_filename(argv[1]);
	   }
	   printf("ef->exp_name = %s\n", ef->exp_name);
	   printf("ef->station_code = %s\n", ef->station_code);
	   printf("ef->scan_name = %s\n", ef->scan_name);
	   printf("ef->data_start_time_ascii = %s\n", ef->data_start_time_ascii);
	   printf("ef->data_start_time = %f\n", ef->data_start_time);
	   for (i=0; i<ef->nr_auxinfo; i++)
		  printf("ef->auxinfo[%d] = %s\n", i, ef->auxinfo[i]);
	   printf("ef->file_type = %s\n", ef->file_type);       
       if (get_aux_entry("flen", ef->auxinfo, ef->nr_auxinfo) != 0) {
            sscanf(get_aux_entry("flen", ef->auxinfo, ef->nr_auxinfo), "%ld", &li);
            fprintf(stderr, "  parsed flen param: %ld\n", li);
       } 
    } while(!(--argc<2));
	return 0;
}
#endif

/*
 * $Log$
 * Revision 1.5  2006/11/21 07:24:30  jwagnerhki
 * auxinfo values set with equal sign
 *
 * Revision 1.4  2006/11/20 14:32:02  jwagnerhki
 * documented the format slightly better, unittest accepts several cmdline filenames
 *
 * Revision 1.3  2006/10/25 15:25:17  jwagnerhki
 * removed heaps of asserts from copied code
 *
 * Revision 1.2  2006/10/25 12:14:06  jwagnerhki
 * added cvs log line
 *
 */


