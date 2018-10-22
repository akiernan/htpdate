/*
	htpdate v0.8.6

	Eddy Vervest <eddy@clevervest.com>
	http://www.clevervest.com/htp

	Synchronize local workstation with time offered by remote web servers

	Extract date/time stamp from web server response
	This program works with the timestamps return by web servers,
	formatted as specified by HTTP/1.1 (RFC 2616, RFC 1123).

	Example usage:

	Debug mode (shows raw timestamps, round trip time (rtt) and
	time difference):

	~# htpdate -d www.xs4all.nl www.demon.nl

	Adjust time smoothly:

	~# htpdate -a www.xs4all.nl www.demon.nl

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

#define VERSION 				"0.8.6"
#define	MAX_HTTP_HOSTS			15				/* 16 web servers */
#define	DEFAULT_HTTP_PORT		"80"
#define	DEFAULT_PROXY_PORT		"8080"
#define	DEFAULT_IP_VERSION		PF_UNSPEC		/* IPv6 and IPv4 */
#define	DEFAULT_HTTP_VERSION	0				/* HTTP/1.0 */
#define	DEFAULT_TIME_LIMIT		31536000		/* 1 year */
#define	DEFAULT_MIN_SLEEP		10				/* 2^10 => 17 minutes */
#define	DEFAULT_MAX_SLEEP		18				/* 2^18 => 72 hours */
#define	RETRY					2				/* Poll attempts */
#define	DEFAULT_PID_FILE		"/var/run/htpdate.pid"
#define	BUFFER					2048


/* By default we turn off "debug", "daemonize" and "log" mode  */
static char		debug = 0;
static char		daemonize = 0;
static char		logmode = 0;
static time_t	gmtoffset;


static int compare( const void *a, const void *b ) {

	if ( *(int *)(a) < *(int *)(b) )
		return -1;
	else
		return 1;
}

/* Split argument in hostname/IP-address and TCP port
   Includes support for IPv6 literal addresses, RFC 2732.
*/
static void splithostport( char **host, char **port ) {
	char    *rb, *rc, *lb, *lc;

	lb = strchr( *host, '[');
	rb = strrchr( *host, ']');
	lc = strchr( *host, ':');
	rc = strrchr( *host, ':');

	/* A (litteral) IPv6 address with portnumber */
	if ( ( rb < rc ) && ( lb != NULL ) && ( rb != NULL ) ) {
		rb[0] = '\0';
		rc++;
	    *port = rc;
		lb++;
		*host = lb;
		return;
	}

    /* A (litteral) IPv6 address without portnumber */
	if ( ( rb != NULL ) && ( lb != NULL ) ) {
		rb[0] = '\0';
		lb++;
		*host = lb;
		return;
	}

	/* A IPv4 address or hostname with portnumber */
	if ( ( rc != NULL ) && ( lc == rc ) ) {
		rc[0] = '\0';
		rc++;
		*port = rc;
		return;
	}

	return;
}


/* Printlog is a slighty modified version used in rdate */
static void printlog( char is_error, char *format, ... ) {
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


static long getHTTPdate( char *host, char *port, char *proxy, char *proxyport, char httpversion, char ipversion, unsigned long when ) {
	int					server_s;
	int					rc;
	struct addrinfo		hints, *res, *res0;
	struct tm			tm;
	struct timeval		timevalue = {.tv_sec = LONG_MAX};
	struct timeval		timeofday = {.tv_sec = 0};
	unsigned long		rtt = 0;
	char				out_buf[256] = {};
	char				in_buf[BUFFER] = {};
	char				remote_time[25] = {};
	char				url[128] = {};
	char				*pdate = NULL;


	/* Connect to web server via proxy server or directly */
	memset( &hints, 0, sizeof (hints) );
	switch( ipversion ) {
		case 4:					/* IPv4 only */
			hints.ai_family = AF_INET;
			break;
		case 6:					/* IPv6 only */
			hints.ai_family = AF_INET6;
			break;
		default:				/* Support IPv6 and IPv4 name resolution */
			hints.ai_family = DEFAULT_IP_VERSION;
	}
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	if ( proxy == NULL ) {
		rc = getaddrinfo( host, port, &hints, &res0 );
	} else {
		sprintf( url, "http://%s:%s", host, port);
		rc = getaddrinfo( proxy, proxyport, &hints, &res0 );
	}

	/* Was the hostname and service resolvable? */
	if ( rc ) {
		printlog( 1, "%-25s host or service unavailable", host );
		return( LONG_MAX );
	}

	/* Build a combined HTTP/1.0 and 1.1 HEAD request
	   Pragma: no-cache "forces" an HTTP/1.0 (and 1.1) compliant
	   web server to return a fresh timestamp
	   Cache-Control: no-cache "forces" an HTTP/1.1 compliant
	   web server to return a fresh timestamp
	*/
	if ( httpversion ) {
		sprintf(out_buf, "HEAD %s/ HTTP/1.1\r\nHost: %s\r\nUser-Agent: htpdate/%s\r\nCache-Control: no-cache\r\n\r\n", url, host, VERSION);
	} else {
		sprintf(out_buf, "HEAD %s/ HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\n\r\n", url, VERSION);
	}

	/* Loop through the available canonical names */
	res = res0;
	do {
		server_s = socket( res->ai_family, res->ai_socktype, res->ai_protocol );
		if ( server_s < 0 ) {
			continue;
		}

		rc = connect( server_s, res->ai_addr, res->ai_addrlen );
		if ( rc ) {
			close( server_s);
			server_s = -1;
			continue;
		}

		break;
	} while ( ( res = res->ai_next ) );


	if ( rc ) {
		printlog( 1, "%-25s connection failed", host );
		return( LONG_MAX );
	}

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

	/* Return the time delta between web server time (timevalue)
	   and system time (timeofday)
	*/
	return( timevalue.tv_sec - timeofday.tv_sec + gmtoffset );
			
}


static int setclock( double timedelta, char setmode ) {
	struct timeval		timeofday;

	switch ( setmode ) {

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

	}							/* switch setmode */

	return(-1);
}


/* Display help page */
static void showhelp() {
	printf("htpdate version %s\n", VERSION);
	printf("\
Usage: htpdate [-4|-6] [-a|-q|-s] [-d|-D] [-1|-h|-l|-t] [-i pid file]\n\
         [-m minpoll] [-M maxpoll] [-p precision] [-P <proxyserver>[:port]]\n\
         <host[:port]> ...\n\n\
  -1    HTTP/1.1 request (default HTTP/1.0)\n\
  -4    Force IPv4 name resolution only\n\
  -6    Force IPv6 name resolution only\n\
  -a    adjust time smoothly\n\
  -q    query only, don't make time changes (default)\n\
  -s    set time\n\
  -d    debug mode\n\
  -D    daemon mode\n\
  -h    help\n\
  -l    use syslog for output\n\
  -t    turn off sanity time check\n\
  -i    pid file\n\
  -m    minimum poll interval (2^m sec)\n\
  -M    maximum poll interval (2^M sec)\n\
  -P    precision (usec)\n\
  -P    proxy server\n\
  host  web server hostname or ip address (maximum of 16)\n\
  port  port number (default 80 and 8080 for proxy server)\n\n");

	return;
}



int main( int argc, char *argv[] ) {
	char				*host = NULL, *proxy = NULL;
	char				*port, *proxyport;
	int					timedelta[MAX_HTTP_HOSTS], timestamp;
	int                 sumtimes, numservers, validtimes, goodtimes, mean, i;
	double				timeavg;
	int					nap = 0, when = 500000;
	char				minsleep = DEFAULT_MIN_SLEEP;
	char				maxsleep = DEFAULT_MAX_SLEEP;
	char				sleeptime = minsleep;
	int					timelimit = DEFAULT_TIME_LIMIT;
	int					precision = 0;
	char				setmode = 0, try;
	int					param;
	char				httpversion = DEFAULT_HTTP_VERSION;
	char				ipversion = DEFAULT_IP_VERSION;
	char				*pidfile = DEFAULT_PID_FILE;
	FILE				*pid_file;
	pid_t				pid;

	extern char			*optarg;
	extern int			optind;


	/* Parse the command line switches and arguments */
	while ( (param = getopt(argc, argv, "146adhi:lm:p:qstDM:P:") ) != -1)
	switch( param ) {

		case '1':			/* HTTP/1.1 */
			httpversion = 1;
			break;
		case '4':			/* IPv4 only */
			ipversion = 4;
			break;
		case '6':			/* IPv6 only */
			ipversion = 6;
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
		case 'p':			/* precision */
			precision = atoi(optarg) ;
			if ( (precision <= 0) || (precision >= 500000) ) {
				printlog( 1, "Invalid precision" );
				exit(1);
			}
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
			if ( ( maxsleep = atoi(optarg) ) <= 0 ) {
				printlog( 1, "Invalid sleep time" );
				exit(1);
			}
			break;
		case 'P':
			proxy = (char *)optarg;
			proxyport = DEFAULT_PROXY_PORT;
			splithostport( &proxy, &proxyport );
			break;
		case '?':
			return 1;
		default:
			abort();
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

	/* Run as a daemonize when -D is set */
	if ( daemonize ) {

		/* Check if htpdate is already running (pid exists)*/
		pid_file=fopen(pidfile, "r");
		if ( pid_file ) {
			printlog( 1, "htpdate already running" );
			exit(1);
		}

		pid = fork();
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
		close( STDIN_FILENO );
		close( STDOUT_FILENO );
		close( STDERR_FILENO );

		signal(SIGHUP, SIG_IGN);

		/* Change the file mode mask */
		umask(0);

		/* Change the current working directory */
		if ( (chdir("/")) < 0 ) {
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
			pid_file=fopen( pidfile, "w" );
			if ( !pid_file )
				printlog( 1, "Error creating pid file" );
			else {
				fprintf( pid_file,"%u\n", (unsigned short)pid );
				fclose( pid_file );
			}
			printlog( 0, "htpdate version %s started", VERSION );
			exit(0);
		}

		/* Query only mode doesn't exist in daemon mode */
		if ( ! setmode ) setmode = 1;

	}

	/* In case we have more than one web server defined, we
	   spread the polls equal within a second and take a "nap" in between.
	*/
	if ( numservers > 1 )
		nap = (unsigned long)(1000000 / (numservers + 1));

	/* Infinite poll cycle loop in daemonize mode */
	do {

	/* Initialize number of received valid timestamps, good timestamps
	   and the average of the good timestamps
	*/
	validtimes = goodtimes = sumtimes = 0;
	if ( precision )
		when = precision;
	else
		when = nap;

	/* Loop through the time sources (web servers); poll cycle */
	for ( i = optind; i < argc; i++ ) {

		/* host:port is stored in argv[i] */
		host = (char *)argv[i];
		port = DEFAULT_HTTP_PORT;
		splithostport( &host, &port );

		/* Retry if first poll shows time offset */
		try = RETRY;
		do {
			timestamp = getHTTPdate( host, port, proxy, proxyport,\
					httpversion, ipversion, when );
			try--;
		} while ( timestamp && try && daemonize );

		/* Only include valid responses in timedelta[] */
		if ( abs( timestamp )  < timelimit ) {
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

		   or when "precision" is specified, use a different algorithme
		   Example with a precision of 200000:
		   2 servers => 0.200, 0.800
		   3 servers => 0.200, 0.800, 0.200
		   4 servers => 0.200, 0.800, 0.200, 0.800
		   ...
		*/
		if ( precision ) {
				if ( when > 500000 )
					when = precision;
				else
					when = 1000000 - precision;
		} else {
			when += nap;
		}
	}

	/* Sort the timedelta results */
	qsort( &timedelta, validtimes, sizeof(timedelta[0]), compare );

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
			printf("Timezone: GMT%+li (%s,%s)\n", gmtoffset / 3600, tzname[0], tzname[1] );
		}

		/* Do I really need to change the time?  */
		if ( sumtimes ) {
			/* If a precision was specified and the time offset is small
			   (< +-0.5 seconds), recalculate the timeavg using the precision
			*/
			printf("precision %d timeavg %f \n", precision, timeavg);
			if ( precision && ( (timeavg >= -0.5) && (timeavg <= 0.5) ) )
				timeavg = (double)precision / 1000000;

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
		/* Sleep for minsleep to avoid flooding */
		if ( daemonize ) sleep( 1 << minsleep );
	}

	/* After first poll cycle do not step through time, only adjust */
	setmode = 1;

	} while ( daemonize );		/* end of infinite while loop */

	if ( ! sumtimes ) {
		setclock( 0, 0 );
	}

	exit(0);

}

/* vi:set ts=4: */
