/*
	htpdate v0.5

	Eddy Vervest <Eddy@cleVervest.com>
	http://www.clevervest.com

	Synchronize local workstation with time offered by remote web servers

	Extract date/time stamp from web server response
	This program works with the timestamps return by web servers,
	formatted as specified by HTTP/1.1 (RFC 2616, RFC 1123).

	Example usage. 

	# htpdate -D www.xs4all.nl www.demon.nl

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
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include <limits.h>

#define version 		"0.5"
#define	BUFFER			2048


/* By default we turn off globally the "debug mode" */
static char debug = 0;
static char daemonize = 0;


static int doublecomp(const void *p1, const void *p2) {
	double i = *((double *)p1);
	double j = *((double *)p2);

	if (i > j)
		return(1);
	if (i < j)
		return(-1);
	return(0);
}


static double dtime( struct timeval t1, struct timeval t2 ) {

	double dt1 = t1.tv_sec + t1.tv_usec*1e-6;
	double dt2 = t2.tv_sec + t2.tv_usec*1e-6;
	return dt2-dt1;
}


/* printlog is a slighty adjust version from the orginal from rdate */
static void printlog(char is_error, char *format, ...) {
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


static double getHTTPdate( char *host, unsigned short port, char *proxy, unsigned short proxyport ) {
	unsigned int		server_s;
	struct sockaddr_in	server_addr;
	struct tm			tm;
	struct timeval		timevalue, timeofday;
	struct hostent		*hostinfo;
	char				out_buf[BUFFER];	// Output buffer for HEAD request
	char				in_buf[BUFFER];		// Input buffer for HTTP response
	char				remote_time[24];	// holds timestamp RFC1123 format
	char				*pdate = NULL;


	/* Initialize web server timevalue and current time */
	timevalue.tv_sec = LONG_MAX;
	timevalue.tv_usec = 0;
	timeofday.tv_sec = 0;
	timeofday.tv_usec = 0;

	/* Connect to web server via proxy server or directly
	   Pragma: no-cache "forces" an HTTP/1.0 or HTTP/1.1 compliant
	   web server to return a fresh timestamp
	*/
	if ( proxy == NULL ) {
		sprintf(out_buf, "HEAD / HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\n\r\n", version);
		hostinfo = gethostbyname(host);
	} else {
		sprintf(out_buf, "HEAD http://%s:%i HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\n\r\n", host, port, version);
		hostinfo = gethostbyname(proxy);
		port = proxyport;
	}

	/* Was the hostname resolvable? */
	if (hostinfo) {

	server_s = socket(AF_INET, SOCK_STREAM, 0);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr = *(struct in_addr *)*hostinfo -> h_addr_list;

	if ( connect(server_s, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0 ) {

		/* send HEAD request */
		send(server_s, out_buf, strlen(out_buf), 0);

		/* Receive data from the web server
		   The return code from recv() is the number of bytes received
		*/
		if ( recv(server_s, in_buf, BUFFER, 0) != -1 ) {

			/* Assuming server and (one way) network delay is
			   neglectable.
			*/
			(void)gettimeofday(&timeofday, NULL);

			/* Look for the line that contains Date: */
			if ( ( pdate = (char *)strstr(in_buf, "Date: ") ) != NULL ) {
				strncpy(remote_time, pdate + 11, 24);

				if ( (char *)strptime( remote_time, "%d %b %Y %H:%M:%S", &tm) != NULL) {
					/* web server timestamps are without daylight savings */
					tm.tm_isdst = 0;
					timevalue.tv_sec = mktime(&tm);
					/* on average the time stamp is half second behind */
					timevalue.tv_usec = 500000;
				}
			}

			close(server_s);
		}

	} else 						/* connect  */
		fprintf( stderr, "Connection to %s failed", host );

	} else  					/* hostinfo */
		fprintf( stderr, "Host %s not found\n", host );
				
	if ( debug )
		printf("%s: %s", host, remote_time);

	return( dtime( timeofday, timevalue) );

}

static char setclock( double timedelta, char setmode ) {
	struct timeval		timeofday;


switch (setmode) {

	case 0:						/* no time adjustment, just print time */
		printf( "Time difference %f seconds\n", timedelta );
		printf( "Use -a or -s to correct the time\n" );
		return (0);

	case 1: case 3:				/* adjust time smoothly */
		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - (long)timedelta) * 1000000);	

		printlog( 0, "Adjusting time %f seconds", timedelta );

		return (adjtime(&timeofday, NULL));

	case 2:						/* set time */
		gettimeofday(&timeofday, NULL);

		printlog( 0, "Setting time %f seconds", timedelta );

		timedelta = ( timeofday.tv_sec + timeofday.tv_usec*1e-6 ) + timedelta;

		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - (long)timedelta) * 1000000);	

		printlog( 0, "Set time to: %s", asctime(localtime(&timeofday.tv_sec)) );

		return (settimeofday(&timeofday, NULL));

	default:
		fprintf( stderr, "Error setclock\n" );

} /* switch setmode */

		return (-1);
}


static void showhelp() {

	/* display help page */
	printf("htpdate v%s\n", version);
	printf("Usage: htpdate [-adhqsxD] [-i pid file] [-m sec] [-M sec] [-p precision] [-t set threshold] [-P <proxyserver>[:port]] <host[:port]> [host[:port]]...\n\n");

	return;

}


int main( int argc, char *argv[] ) {
	time_t				gmtoffset;
	char				*host = NULL, *proxy = NULL, *portstr = NULL;
	double				timedelta[32] = {0};
	double				timestamp, timeavg, threshold = 5, precision = 0.5;
	unsigned short		port = 80, proxyport = 8080;
	unsigned long		minsleep=4096, maxsleep=1048576, sleeptime=minsleep, \
						nap = 0;
	pid_t				pid;
	unsigned char		i, validtime, goodtime;
	char				setmode = 0, param;
	char				*pidfile = "/var/run/htpdate.pid";
	FILE				*pid_file;

	extern char *optarg;
	extern int optind, optopt;


/* Parse the command line switches and arguments */
while ((param = getopt (argc, argv, "adhi:m:p:qst:xDM:P:")) != -1)
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
			if ( ( minsleep = atol(optarg) ) <= 0 ) {
				fprintf( stderr, "Invalid sleep time\n" );
				exit(1);
			}
			sleeptime = minsleep;
			break;
		case 'p':			/* precision */
			if ( ( precision = atof(optarg) ) <= 0 ) {
				fprintf( stderr, "Invalid precision\n" );
				exit(1);
			}
			break;
		case 'q':			/* query only */
			setmode = 0;
			break;
		case 's':			/* set time */
			setmode = 2;
			break;
		case 't':			/* step threshold value in seconds */
			if ( ( threshold = atof(optarg) ) <= 0 ) {
				fprintf( stderr, "Invalid threshold\n" );
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
			if ( (maxsleep = atol(optarg) ) <= 0 ) {
				fprintf( stderr, "Invalid sleep time\n" );
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
					fprintf( stderr, "Invalid port number\n" );
					exit(1);
				}
			} else {
				proxyport = 8080;
			}
			break;
		case '?':
			if ( isprint(optopt) )
				fprintf( stderr, "Unknown option `-%c'.\n", optopt );
			else
				fprintf( stderr, "Unknown option character `\\x%x'.\n", optopt );
			return 1;
		default:
			abort ();
	}

	/* Display help page */
	if ( argv[optind] == NULL ) {
		showhelp();
		exit(0);
	}

	/* Calculate GMT offset from local timezone */
	time(&gmtoffset);
	gmtoffset -= mktime(gmtime(&gmtoffset));

	/* Run as a daemonize when -D is set and -d isn't */
	if ( daemonize && (! debug) ) {

		/* Check if htpdate is already running (pid exists)*/
		pid_file=fopen(pidfile, "r");
		if (pid_file) {
			fprintf( stderr,"htpdate is already running\n" );
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
			if (!pid_file)
				fprintf( stderr,"Cannot create pid file\n" );
			else {
				fprintf(pid_file,"%d\n", pid);
				fclose(pid_file);
			}
			syslog( LOG_NOTICE, "htpdate v%s started.\n", version);
			return 0;
		}

	}

	/* In case we have more than one web server defined, we
	   try to spread the polls a little and take a "nap" in between polls.
	   A poll cycle shouldn't finish within a second (accuracy)
	*/
	if ( (argc - optind) > 1 )
		nap = 1000000 / (argc - optind);

	/* infinite poll cycle loop in daemonize mode,
	   out break out of the loop if daemonize != 1
	*/
	while ( 1 ) {

	/* initialize number of received valid timestamps, good timestamps
	   and the average of the good timestamps
	*/
	validtime = 0;
	goodtime = 0;
	timeavg = 0;

	/* loop through the time sources => web servers */
	for (i = optind; i < argc; i++) {

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

		timestamp = getHTTPdate( host, port, proxy, proxyport ) + gmtoffset;

		/* Only include valid response, delta time < year */
		if ( fabs(timestamp) < 31536000 ) {
			timedelta[validtime] = timestamp;
			validtime++;
		}
		if ( debug )
			printf(" => %f\n", timestamp );

		/* Sleep between polls, to spread polls equally within
		   at least a second.
		*/
		usleep( nap );

	}

	/* sort the timedelta results */
	qsort(&timedelta, validtime, sizeof(timedelta[0]), doublecomp);

	/* filter out the bogus timevalues:
	   - every timevalue which is more than 1 seconde off from mean
	   is considered a 'false ticker'. Ntp sync web server can never
	   be more off than a second, between two polls.
	*/
	for ( i = 0; i < validtime; i++ ) {
		if ( ( fabs(timedelta[i]-timedelta[validtime/2]) < 1 ) ) {
			timeavg += timedelta[i];
			goodtime++;
		}
	}

	/* check if we have at least one valid response */
	if ( goodtime > 0 ) {
		timeavg /= goodtime;

		if ( debug )
			printf("mean: %f, average: %f\n", timedelta[validtime/2], timeavg);

		/* if the time offset is bigger than the threshold,
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
		if ( (fabs(timeavg) > precision)  || (! daemonize) ) {
			setclock( timeavg, setmode );
		}
	} else {
		printlog( 1, "No suitable server found for time synchronization");
	}

	/* Was the -D daemonize switch set? Break the while loop if it wasn't */
	if ( (! daemonize) || debug ) {
		break;
	}

	/* Calculate new sleep time, depending on offset */
	if ( fabs(timeavg) > precision ) {
		sleeptime >>= 1;
		if (sleeptime < minsleep ) sleeptime = minsleep;
	} else {
		sleeptime <<= 1;
		if (sleeptime > maxsleep ) sleeptime = maxsleep;
	}	

	/* sleep time till next poll */
	printlog ( 0, "Sleep for %li seconds", sleeptime );
	sleep( sleeptime );

	} /* while */

exit(0);

}
