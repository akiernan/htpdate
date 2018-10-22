/*
	htpdate v0.7.1

	Eddy Vervest <eddy@clevervest.com>
	http://www.clevervest.com/htp

	Synchronize local workstation with time offered by remote web servers

	Extract date/time stamp from web server response
	This program works with the timestamps return by web servers,
	formatted as specified by HTTP/1.1 (RFC 2616, RFC 1123).

	Example usage:

	Debug mode (shows raw timestamps, rtt and time difference):

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
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <math.h>
#include <limits.h>

#define version 		"0.7.1"
#define	BUFFER			2048


/* By default we turn off globally the "debug mode" */
static int debug = 0;
static int daemonize = 0;


static int longcomp(const void *a, const void *b) {

	return ( *(int*)a - *(int*)b );
}


/* Printlog is a slighty adjust version from the orginal from rdate */
static void printlog(int is_error, char *format, ...) {
	va_list args;
	int n;
	char buf[128];
	va_start(args, format);
	n = vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if (n < 1)
		return; /* Error, which we can't report because this _is_ the error
	            reporting mechanism */
    if ( daemonize )
		syslog(is_error?LOG_WARNING:LOG_INFO, buf);
	else
		fprintf(is_error?stderr:stdout, "%s\n", buf);
}


static long getHTTPdate( char *host, int port, char *proxy, int proxyport, unsigned long when ) {
	int					server_s;
	struct sockaddr_in	server_addr;
	struct tm			tm;
	struct timeval		timevalue = {.tv_sec = LONG_MAX};
	struct timeval		timeofday = {.tv_sec = 0};
	struct hostent		*hostinfo;
	double				rtt;
	char				out_buf[BUFFER];	// Output buffer for HEAD request
	char				in_buf[BUFFER];		// Input buffer for HTTP response
	char				remote_time[24];	// holds timestamp RFC1123 format
	char				*pdate = NULL;


	/* Connect to web server via proxy server or directly
	   Pragma: no-cache "forces" an HTTP/1.0 or HTTP/1.1 compliant
	   web server to return a fresh timestamp
	*/
	if ( proxy == NULL ) {
		sprintf(out_buf, "HEAD / HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\n\r\n", version);
		hostinfo = gethostbyname( host );
	} else {
		sprintf(out_buf, "HEAD http://%s:%i HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\n\r\n", host, port, version);
		hostinfo = gethostbyname( proxy );
		port = proxyport;
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

		/* Initialize the RTT (start of measurement) */
		rtt = (double)(timeofday.tv_sec + when*1e-6);

		if ( timeofday.tv_usec <= when ) {
			usleep( when - timeofday.tv_usec );
		} else {
			usleep( 1000000 + when - timeofday.tv_usec );
			rtt++;
		}

		/* Send HEAD request */
		send(server_s, out_buf, strlen(out_buf), 0);

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
			rtt -= (double)(timeofday.tv_sec + timeofday.tv_usec*1e-6);

			/* Look for the line that contains Date: */
			if ( ( pdate = strstr(in_buf, "Date: ") ) != NULL ) {
				strncpy(remote_time, pdate + 11, 24);

				if ( strptime( remote_time, "%d %b %Y %H:%M:%S", &tm) != NULL) {
					/* Web server timestamps are without daylight saving */
					tm.tm_isdst = 0;
					timevalue.tv_sec = mktime(&tm);
				}

				/* Print host, raw timestamp, round trip time */
				if ( debug )
					printf("%-25s %s (%.3f) => %li\n", host, remote_time, \
					  -rtt, timevalue.tv_sec - timeofday.tv_sec - timezone );

			} else {
				printlog( 1, "%-25s response without timestamp", host );
			}

		}						/* bytes received */

	} else 						/* connect  */
		printlog( 1, "%-25s connection failed", host );

	} else  					/* hostinfo */
		printlog( 1, "%-25s host not found", host );

	/* Return the time delta between web server time (timevalue)
	   and system time (timeofday)
	*/
	return( timevalue.tv_sec - timeofday.tv_sec - timezone );
			
}

static int setclock( double timedelta, int setmode ) {
	struct timeval		timeofday;


switch (setmode) {

	case 0:						/* no time adjustment, just print time */
		printlog( 0, "Time correction %.3f seconds", timedelta );
		printlog( 0, "Use -a or -s to correct the time" );
		return(0);

	case 1: case 3:				/* adjust time smoothly */
		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - (long)timedelta) * 1000000);	

		printlog( 0, "Adjusting time %.3f seconds", timedelta );

		return( adjtime(&timeofday, NULL) );

	case 2:						/* set time */
		gettimeofday( &timeofday, NULL );

		printlog( 0, "Setting time %.3f seconds", timedelta );

		timedelta = ( timeofday.tv_sec + timeofday.tv_usec*1e-6 ) + timedelta;

		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - (long)timedelta) * 1000000);	

		printlog( 0, "Set time to: %s", asctime(localtime(&timeofday.tv_sec)) );

		return( settimeofday(&timeofday, NULL) );

	default:
		printlog( 1, "Error setclock routine" );

} /* switch setmode */

		return(-1);
}


/* Display help page */
static void showhelp() {

	printf("htpdate version %s\n", version);
	printf("Usage: htpdate [-adhqsxD] [-i pid file] [-m sec] [-M sec] [-t set threshold]\n");
	printf("         [-P <proxyserver>[:port]] <host[:port]> [host[:port]]...\n\n");

	return;

}


int main( int argc, char *argv[] ) {
	pid_t				pid;
	char				*host = NULL, *proxy = NULL, *portstr = NULL;
	long				timedelta[16], timestamp;
	double				timeavg, threshold = 5;
	int 				numservers;
	int 				i, validtime, goodtime;
	int					port, proxyport = 8080;
	unsigned long		nap = 0, when = 500000;
	unsigned int		minsleep = 10, maxsleep = 18, sleeptime = minsleep;
	int					setmode = 0, param;
	char				*pidfile = "/var/run/htpdate.pid";
	FILE				*pid_file;

	extern char *optarg;
	extern int optind;


	/* Parse the command line switches and arguments */
	while ( (param = getopt(argc, argv, "adhi:m:p:qst:xDM:P:") ) != -1)
	switch (param)
	{
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
		case 'm':			/* minimum poll interval */
			if ( ( minsleep = atoi(optarg) ) <= 0 ) {
				printlog( 1, "Invalid sleep time" );
				exit(1);
			}
			sleeptime = minsleep;
			break;
		case 'q':			/* query only */
			setmode = 0;
			break;
		case 's':			/* set time */
			setmode = 2;
			break;
		case 't':			/* step threshold value in seconds */
			if ( ( threshold = atof(optarg) ) <= 0 ) {
				printlog( 1, "Invalid threshold" );
				exit(1);
			}
			break;
		case 'x':			/* never set time, only slew */
			setmode = 3;
			break;
		case 'D':			/* run as daemon */
			setmode = 1;
			daemonize = 1;
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

	/* Display help page */
	if ( argv[optind] == NULL ) {
		showhelp();
		exit(1);
	}

	/* Run as a daemonize when -D is set and -d isn't */
	if ( daemonize && (!debug) ) {

		/* Check if htpdate is already running (pid exists)*/
		pid_file=fopen(pidfile, "r");
		if (pid_file) {
			printlog( 1, "htpdate is already running" );
			exit(1);
		}

		switch ( (pid=fork()) ) {
		case -1:
			perror ("fork()");
			exit(3);
		case 0:
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
			if (setsid () == -1) {
				exit(4);
			}
			break;
		default:
			/* Write a pid file */
			pid_file=fopen(pidfile, "w");
			if ( !pid_file )
				fprintf( stderr,"Cannot create pid file\n" );
			else {
				fprintf( pid_file,"%u\n", (unsigned short)pid );
				fclose( pid_file );
			}
			printlog( 0, "htpdate version %s started", version);
			return(0);
		}

	}

	/* In case we have more than one web server defined, we
	   spread the polls equal in time and take a "nap" in between polls.
	*/
	numservers = argc - optind;
	if ( numservers > 1 )
		when = nap = (unsigned long)(1000000 / (argc - optind + 1));


	/* Infinite poll cycle loop in daemonize mode,
	   out break out of the loop if daemonize != 1
	*/
	while ( 1 ) {

	/* Initialize number of received valid timestamps, good timestamps
	   and the average of the good timestamps
	*/
	validtime = 0;
	goodtime = 0;
	timeavg = 0;

	/* Loop through the time sources => web servers */
	for ( i = optind; i < argc; i++ ) {

		/* host:port is stored in argv[i] */
		host = (char *)argv[i];
		portstr = strchr(host, ':');
		if ( portstr != NULL ) {
			portstr[0] = '\0';
			portstr++;
			if ( (port = atoi(portstr)) <= 0 ) {
				fprintf( stderr, "Invalid port number\n");
				exit(1);
			}
		} else {
			port = 80;
		}

		timestamp = getHTTPdate( host, port, proxy, proxyport, when );

		/* Only include valid responses in timedelta[], |delta time| < year */
		if ( labs(timestamp) < 31536000 ) {
			timedelta[validtime] = timestamp;
			validtime++;
		}

		/* Sleep for a while, unless we detect a time offset */
		if ( daemonize && (timestamp == 0) )
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
	qsort( &timedelta, validtime, sizeof(timedelta[0]), longcomp );

	/* Filter out the bogus timevalues:
	   - every timevalue which is more than 1 seconde off from mean
	   is considered a 'false ticker'. Ntp sync web server can never
	   be more off than a second, between two polls.
	*/
	for ( i = 0; i < validtime; i++ ) {
		if ( labs(timedelta[i]-timedelta[validtime/2]) <= 1 ) {
			timeavg += timedelta[i];
			goodtime++;
		}
	}

	/* Check if we have at least one valid response */
	if ( goodtime > 0 ) {
		timeavg /= (double)goodtime;

		if ( debug ) {
			printf("#: %d, mean: %li, average: %.3f\n", goodtime, \
					timedelta[validtime/2], timeavg);
			printf("Timezone: %s %s\n", tzname[0], tzname[1] );
		}

		/* If the time offset is bigger than the threshold,
		   use set not adjust, unless -x was set
		*/
		if ( daemonize ) {
			if ( (fabs(timeavg) > threshold) && (setmode != 3) ) {
				setmode = 2;			/* set time */
				sleeptime = minsleep;	/* fallback to minimum sleeptime */
			} else
				setmode = 1;			/* adjust time */
		}

			
		/* Do I really need to change the time?  */
		if ( (timeavg != 0) || (!daemonize) )
			setclock( timeavg, setmode );

	} else {
		printlog( 1, "No suitable server found for time synchronization");
	}

	/* Was the -D daemonize switch set? Break the while loop if it wasn't */
	if ( (!daemonize) || debug )
		break;


	/* Calculate new sleep time, depending on offset */
	if ( timeavg != 0 ) {
		sleeptime--;
		if ( sleeptime < minsleep ) sleeptime = minsleep;
		sleep( 1 << sleeptime );
	} else {
		sleeptime++;
		if ( sleeptime > maxsleep ) sleeptime = maxsleep;
	}	

	} /* end of infinite while loop */

	exit(0);

}
