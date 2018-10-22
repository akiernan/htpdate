/*
	htpdate v0.8.3

	Eddy Vervest <eddy@clevervest.com>
	http://www.clevervest.com/htp

	Synchronize local workstation with time offered by remote web servers

	Extract date/time stamp from web server response
	This program works with the timestamps return by web servers,
	formatted as specified by HTTP/1.1 (RFC 2616, RFC 1123).

	Example usage:

	Debug mode (shows raw timestamps, round trip time (rtt) and
	time difference):

	# htpdate -d www.xs4all.nl www.demon.nl

	Adjust time smoothly:

	# htpdate -a www.xs4all.nl www.demon.nl

	...see manpage for more parameters


	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
	http://www.gnu.org/copyleft/gpl.html

*/

/* needed to avoid implicit warnings from strptime */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <limits.h>

#define version 		"0.8.3"
#define	BUFFER			2048


/* By default we turn off "debug", "daemonize" and "log" mode  */
static char		debug = 0;
static char		daemonize = 0;
static char		logmode = 0;
static time_t	gmtoffset;


static int longcomp(const void *a, const void *b) {

	return ( *(long*)a - *(long*)b );
}


/* Printlog is a slighty modified version used in rdate */
static void printlog(char is_error, char *format, ...) {
	va_list args;
	int n;
	char buf[128];
	va_start(args, format);
	n = vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

    if ( logmode )
		syslog(is_error?LOG_WARNING:LOG_INFO, "%s", buf);
	else
		fprintf(is_error?stderr:stdout, "%s\n", buf);
}


static long getHTTPdate( char *host, int port, char *proxy, int proxyport, char httpversion, unsigned long when ) {
	int					server_s;
	struct sockaddr_in	server_addr;
	struct tm			tm;
	struct timeval		timevalue = {.tv_sec = LONG_MAX};
	struct timeval		timeofday = {.tv_sec = 0};
	unsigned long		rtt = 0;
	struct hostent		*hostinfo;
	char				out_buf[256] = {};
	char				in_buf[BUFFER] = {};
	char				remote_time[25] = {};
	char				url[128] = {};
	char				*pdate = NULL;


	/* Connect to web server via proxy server or directly */
	if ( proxy == NULL ) {
		hostinfo = gethostbyname( host );
	} else {
		sprintf( url, "http://%s:%i", host, port);
		hostinfo = gethostbyname( proxy );
		port = proxyport;
	}

	/* Build the HTTP/1.0 or HTTP/1.1 HEAD request
	   Pragma: no-cache "forces" an HTTP/1.0 (and 1.1) compliant
	   web server to return a fresh timestamp
	   Cache-Control: no-cache "forces" an HTTP/1.1 compliant
	   web server to return a fresh timestamp
	*/
	if ( httpversion ) {
		sprintf(out_buf, "HEAD %s/ HTTP/1.1\r\nHost: %s\r\nUser-Agent: htpdate/%s\r\nCache-Control: no-cache\r\n\r\n", url, host, version);
	} else {
		sprintf(out_buf, "HEAD %s/ HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\n\r\n", url, version);
	}

	/* Was the hostname resolvable? */
	if ( hostinfo ) {

	server_s = socket(AF_INET, SOCK_STREAM, 0);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons( port );
	server_addr.sin_addr = *(struct in_addr *)*hostinfo -> h_addr_list;

	if ( connect(server_s, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0 ) {

		/* Wait till we reach the desired time, "when" */
		gettimeofday(&timeofday, NULL);

		/* Initialize RTT (start of measurement) */
		rtt = timeofday.tv_sec;

		if ( timeofday.tv_usec <= when ) {
			usleep( when - timeofday.tv_usec );
		} else {
			usleep( 1000000 + when - timeofday.tv_usec );
			rtt++;
		}

		/* Send HEAD request */
		if ( send(server_s, out_buf, strlen(out_buf), 0) < 0 )
			printlog( 1, "Error sending" );

		/* Receive data from the web server
		   The return code from recv() is the number of bytes received
		*/
		if ( recv(server_s, in_buf, BUFFER, 0) != -1 ) {

			/* Assuming that network delay (server->htpdate) is neglectable,
			   the received web server time "should" match the local time.

			   From RFC 2616 paragraph 14.18
			   ...
			   It SHOULD represent the best available approximation
			   of the date and time of message generation, unless the
			   implementation has no means of generating a reasonably
			   accurate date and time.
			   ...
			*/

			gettimeofday(&timeofday, NULL);

			/* rtt contains round trip time in micro seconds, now! */
			rtt = ( timeofday.tv_sec - rtt ) * 1000000 + \
				timeofday.tv_usec - when;

			/* Look for the line that contains Date: */
			if ( ( pdate = strstr(in_buf, "Date: ") ) != NULL ) {
				strncpy(remote_time, pdate + 11, 24);

				if ( strptime( remote_time, "%d %b %Y %H:%M:%S", &tm) != NULL) {
					/* Web server timestamps are without daylight saving */
					tm.tm_isdst = 0;
					timevalue.tv_sec = mktime(&tm);
				} else {
					printlog( 1, "%-25s unknown time format", host );
				}

				/* Print host, raw timestamp, round trip time */
				if ( debug )
					printf("%-25s %s (%.3f) => %li\n", host, remote_time, \
					  rtt * 1e-6, timevalue.tv_sec - timeofday.tv_sec \
					  + gmtoffset );

			} else {
				printlog( 1, "%-25s no timestamp", host );
			}

		}						/* bytes received */

		close( server_s );

	} else 						/* connect  */
		printlog( 1, "%-25s connection failed", host );

	} else  					/* hostinfo */
		printlog( 1, "%-25s host not found", host );

	/* Return the time delta between web server time (timevalue)
	   and system time (timeofday)
	*/
	return( timevalue.tv_sec - timeofday.tv_sec + gmtoffset );
			
}


static int setclock( double timedelta, char setmode ) {
	struct timeval		timeofday;

	switch (setmode)
	{
	case 0:						/* no time adjustment, just print time */
		printlog( 0, "Offset %.3f seconds", timedelta );
		return(0);

	case 1:						/* adjust time smoothly */
		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - timeofday.tv_sec) * 1000000);	

		printlog( 0, "Adjusting %.3f seconds", timedelta );

		return( adjtime(&timeofday, NULL) );

	case 2:						/* set time */
		printlog( 0, "Setting %.3f seconds", timedelta );

		gettimeofday( &timeofday, NULL );
		timedelta += ( timeofday.tv_sec + timeofday.tv_usec*1e-6 );

		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - timeofday.tv_sec) * 1000000);	

		printlog( 0, "Set: %s", asctime(localtime(&timeofday.tv_sec)) );

		return( settimeofday(&timeofday, NULL) );

	} /* switch setmode */
	return(-1);
}


/* Display help page */
static void showhelp() {
	printf("htpdate version %s\n", version);
	printf("\
Usage: htpdate [-0|-1] [-a|-q|-s] [-d|-D] [-h|-l|-t] [-i pid file] [-m minpoll]\n\
        [-M maxpoll] [-P <proxyserver>[:port]] <host[:port]> ...\n\n\
  -0    HTTP/1.0 request (default)\n\
  -1    HTTP/1.1 request\n\
  -a    adjust time smoothly\n\
  -q    query only, don't make time changes (default)\n\
  -s    set time\n\
  -d    debug mode\n\
  -D    daemon mode\n\
  -h    help\n\
  -l    use syslog for output\n\
  -t    turn off sanity time check\n\
  -i    pid file\n\
  -m    minimum poll interval (2^m)\n\
  -M    maximum poll interval (2^M)\n\
  -P    proxy server\n\
  host  web server hostname or ip address (maximum of 16)\n\
  port  port number (default 80 and 8080 for proxy server)\n\n");

	return;
}



int main( int argc, char *argv[] ) {
	char				*host = NULL, *proxy = NULL, *portstr;
	int					timedelta[15], timestamp;
	int					port, proxyport = 8080;
	int                 sumtimes, numservers, validtimes, goodtimes, mean, i;
	double				timeavg;
	int					nap = 0, when = 500000;
	char				minsleep = 10, maxsleep = 18, sleeptime = minsleep;
	int					timelimit = 31536000;	/* default 1 year */
	char				setmode = 0, httpversion = 0, try, param;
	char				*pidfile = "/var/run/htpdate.pid";
	FILE				*pid_file;
	pid_t				pid;

	extern char			*optarg;
	extern int			optind;


	/* Parse the command line switches and arguments */
	while ( (param = getopt(argc, argv, "01adhi:lm:qstDM:P:") ) != -1)
	switch (param)
	{
		case '0':			/* HTTP/1.0 */
			break;
		case '1':			/* HTTP/1.1 */
			httpversion = 1;
			break;
		case 'a':			/* adjust time */
			setmode = 1;
			break;
		case 'd':			/* turn debug on */
			debug = 1;
			break;
		case 'h':			/* show help */
			showhelp();
			exit(0);
		case 'i':			/* pid file help */
			pidfile = (char *)optarg;
			break;
		case 'l':			/* pid file help */
			logmode = 1;
			break;
		case 'm':			/* minimum poll interval */
			if ( ( minsleep = atoi(optarg) ) <= 0 ) {
				printlog( 1, "Invalid sleep time" );
				exit(1);
			}
			sleeptime = minsleep;
			break;
		case 'q':			/* query only */
			break;
		case 's':			/* set time */
			setmode = 2;
			break;
		case 't':			/* disable "sanity" time check */
			timelimit = 2100000000;
			break;
		case 'D':			/* run as daemon */
			daemonize = 1;
			logmode = 1;
			break;
		case 'M':			/* maximum poll interval */
			if ( (maxsleep = atoi(optarg) ) <= 0 ) {
				printlog( 1, "Invalid sleep time" );
				exit(1);
			}
			break;
		case 'P':
			proxy = (char *)optarg;
			portstr = strchr(proxy, ':');
			if ( portstr != NULL ) {
				portstr[0] = '\0';
				portstr++;
				if ( (proxyport = atoi(portstr)) <= 0 ) {
					printlog( 1, "Invalid port number" );
					exit(1);
				}
			}
			break;
		case '?':
			return 1;
		default:
			abort ();
	}

	/* Display help page, if no servers are specified */
	if ( argv[optind] == NULL ) {
		showhelp();
		exit(1);
	}

	/* Exit if to many servers are specified */
	numservers = argc - optind;
	if ( numservers > 16 ) {
		printlog( 1, "Too many servers" );
		exit(1);
	}


    /* Calculate GMT offset from local timezone */
    time(&gmtoffset);
    gmtoffset -= mktime(gmtime(&gmtoffset));

	/* Debug overrules daemon mode */
	if ( debug == 1 )
		daemonize = 0;

	/* Run as a daemonize when -D is set and -d isn't */
	if ( daemonize ) {

		/* Check if htpdate is already running (pid exists)*/
		pid_file=fopen(pidfile, "r");
		if (pid_file) {
			printlog( 1, "htpdate already running" );
			exit(1);
		}

		pid=fork();
		if ( pid < 0 ) {
			printlog ( 1, "Forking error" );
			exit(1);
		}

		if ( pid > 0 ) {
			exit(0);
		}

		/* Create a new SID for the child process */
		if ( setsid () < 0 ) {
			exit(1);
		}

		/* Close out the standard file descriptors */
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

		signal(SIGHUP, SIG_IGN);

		/* Change the file mode mask */
		umask(0);

		/* Change the current working directory */
		if ((chdir("/")) < 0) {
			printlog( 1, "Error cd /" );
			exit(1);
		}

		/* Second fork, to become the grandchild */
		pid = fork();

		if ( pid < 0 ) {
			printlog ( 1, "Forking error" );
			exit(1);
		}

		if ( pid > 0 ) {
			/* Write a pid file */
			pid_file=fopen(pidfile, "w");
			if ( !pid_file )
				printlog( 1, "Error creating pid file" );
			else {
				fprintf( pid_file,"%u\n", (unsigned short)pid );
				fclose( pid_file );
			}
			printlog( 0, "htpdate version %s started", version);
			exit(0);
		}

		/* Query only mode doesn't exist in daemon mode */
		if ( setmode == 0 )
			setmode = 1;

	}

	/* In case we have more than one web server defined, we
	   spread the polls equal in time and take a "nap" in between polls.
	*/
	if ( numservers > 1 )
		nap = (unsigned long)(1000000 / (numservers + 1));

	/* Infinite poll cycle loop in daemonize mode */
	do {

	/* Initialize number of received valid timestamps, good timestamps
	   and the average of the good timestamps
	*/
	validtimes = goodtimes = sumtimes = 0;
	when = nap;

	/* Loop through the time sources (web servers); poll cycle */
	for ( i = optind; i < argc; i++ ) {

		/* host:port is stored in argv[i] */
		host = (char *)argv[i];
		portstr = strchr(host, ':');
		if ( portstr != NULL ) {
			portstr[0] = '\0';
			portstr++;
			if ( (port = atoi(portstr)) <= 0 ) {
				printlog( 1, "Invalid port number");
				exit(1);
			}
		} else {
			port = 80;
		}

		/* Retry if first poll shows time offset */
		try = 2;
		do {
			timestamp = getHTTPdate( host, port, proxy, proxyport,\
					httpversion, when );
			try--;
		} while ( timestamp && try && daemonize );

		/* Only include valid responses in timedelta[] */
		if ( ( timestamp > -timelimit ) && ( timestamp < timelimit ) ) {
			timedelta[validtimes] = timestamp;
			validtimes++;
		}

		/* Sleep for a while, unless we detected a time offset */
		if ( daemonize && !timestamp )
			sleep( (1 << sleeptime) / numservers );

		/* Take a nap, to spread polls equally within a second.
		   Example:
		   2 servers => 0.333, 0.666
		   3 servers => 0.250, 0.500, 0.750
		   4 servers => 0.200, 0.400, 0.600, 0.800
		   ...
		   nap = 1000000 / (#servers + 1)
		*/
		when += nap;
	}

	/* Sort the timedelta results */
	qsort( &timedelta, validtimes, sizeof(timedelta[0]), longcomp );

	/* Mean time value */
	mean = timedelta[validtimes/2];

	/* Filter out the bogus timevalues. A timedelta which is more than
	   1 seconde off from mean, is considered a 'false ticker'.
	   NTP synced web servers can never be more off than a second.
	*/
	for ( i = 0; i < validtimes; i++ ) {
		if ( (timedelta[i]-mean <= 1) && (timedelta[i]-mean >= -1) ) {
			sumtimes += timedelta[i];
			goodtimes++;
		}
	}

	/* Check if we have at least one valid response */
	if ( goodtimes ) {

		timeavg = sumtimes/(double)goodtimes;

		if ( debug ) {
			printf("#: %d, mean: %d, average: %.3f\n", goodtimes, \
					mean, timeavg );
			printf("Timezone: %s %s\n", tzname[0], tzname[1] );
		}

		/* Do I really need to change the time?  */
		if ( sumtimes ) {
			if ( setclock( timeavg, setmode ) ) {
				printlog( 1, "Error changing time" );
			};
			/* Decrease polling interval */
			if ( sleeptime > minsleep ) sleeptime--;
			/* Sleep for minsleep, after a time adjust or set */
			if ( daemonize ) sleep( 1 << minsleep );
		} else {
			/* Increase polling interval */
			if ( sleeptime < maxsleep ) sleeptime++;
		}

	} else {
		printlog( 1, "No server suitable for synchronization found" );
	}

	/* Do not step through time, only adjust in daemon mode */
	setmode = 1;

	} while ( daemonize );		/* end of infinite while loop */

	if ( !sumtimes ) {
		setclock( 0, 0 );
	}

	exit(0);

}

/* vi:set ts=4: */
