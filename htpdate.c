/*
	htpdate v0.3

	Eddy Vervest <Eddy@cleVervest.com>
	http://www.clevervest.com

	Synchronize local workstation with time offered by remote web servers

	Extract date/time stamp from web server response
	This program works with the timestamps return by web servers,
	formatted as specified by HTTP/1.1 (RFC 2616).

	Example usage. 

	# htpdate -D www.xs4all.nl www.demon.nl

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
	http://www.gnu.org/copyleft/gpl.html

*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <syslog.h>

#define version 		"0.3"
#define	BUFFER			2048



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


static double getHTTPdate( char *host, unsigned int port, char *proxy, unsigned int proxyport ) {
	unsigned int		server_s;
	struct sockaddr_in	server_addr;
	struct tm			tm;
	struct timeval		timevalue, timeofday;
	struct hostent		*hostinfo;
	char				out_buf[BUFFER];	// Output buffer for HEAD request
	char				in_buf[BUFFER];		// Input buffer for HTTP response
	char				remote_time[25];	// holds timestamp RFC1123 format
	char				*pdate = NULL;


	/* Initialize web server timevalue and current time*/
	timevalue.tv_sec = LONG_MAX;
	timevalue.tv_usec = 0;
	timeofday.tv_sec = 0;
	timeofday.tv_usec = 0;

	/* Connect to web server via proxy server or not */
	if ( proxy == NULL ) {
		sprintf(out_buf, "HEAD / HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\nCache-Control: Max-age=0\r\n\r\n", version);
		hostinfo = gethostbyname(host);
	} else {
		sprintf(out_buf, "HEAD http://%s:%i HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\nCache-Control: Max-age=0\r\n\r\n", host, port, version);
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
			   neglectable... < 20ms
			*/
			gettimeofday(&timeofday, NULL);

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

	} /* connect  */

	} /* hostinfo */

	return( dtime( timeofday, timevalue) );

}

static int setclock( double timedelta, int setmode, int daemon ) {
	struct timeval		timeofday;


switch (setmode) {

	case 0:						/* no time adjustment, just print time */
		printf ("Time difference %f seconds\n", timedelta );
		printf ("Use -a or -s to correct the time\n" );
		return (0);

	case 1: case 3:				/* adjust time smoothly */
		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - (long)timedelta) * 1000000);	
		if ( daemon == 0 ) {
			printf ("Adjusting time %f seconds\n", timedelta );
		} else {
			syslog ( LOG_NOTICE, "Adjusting time %f seconds\n", timedelta );
		}
		return (adjtime(&timeofday, NULL));

	case 2:						/* set time */
		gettimeofday(&timeofday, NULL);

		if ( daemon == 0 ) {
			printf ("Setting time %f seconds\n", timedelta );
		} else {
			syslog ( LOG_NOTICE, "Setting time %f seconds\n", timedelta );
		}

		timedelta = ( timeofday.tv_sec + timeofday.tv_usec*1e-6 ) + timedelta;

		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - (long)timedelta) * 1000000);	

		if ( daemon == 0 ) {
			printf ( "Set time to: %s", asctime(localtime(&timeofday.tv_sec)) );
		} else {
			syslog ( LOG_NOTICE, "Set time to: %s", asctime(localtime(&timeofday.tv_sec)));
		}

		return (settimeofday(&timeofday, NULL));

	default:
		fprintf( stderr, "Error setclock" );

} /* switch setmode */

		return(-1);
}


static void showhelp() {

	/* display help page */
	printf("htpdate v%s\n", version);
	printf("Usage: htpdate [-aqsxD] [-m sec] [-M sec] [-t step threshold]\n");
	printf("         [-P <proxyserver>[:port]] <host[:port]> [host[:port]]...\n\n");
	printf("       -a  adjust time smoothly\n");
	printf("       -h  show this help\n");
	printf("       -m  minimum sleeptime (default 4096 sec)\n");
	printf("       -q  query only, time not set (default)\n");
	printf("       -s  set time immediate\n");
	printf("       -t  threshold time step (default 5 sec)\n");
	printf("       -x  never step time, only adjust\n");
	printf("       -D  run htpdate as a daemon\n");
	printf("       -M  maximum sleeptime (default 12 days)\n");
	printf("       -P  proxy server\n");
	printf("     host  web server\n");
	printf("     port  port number\n\n");

	return;

}


int main( int argc, char *argv[] ) {
	time_t				gmtoffset;
	char				*host = NULL, *proxy = NULL, *portstr = NULL;
	double				timedelta[32] = {0};
	double				timeavg = 0;
	double				threshold = 5.0;
	unsigned int		port = 80, proxyport = 8080;
	unsigned long		minsleep=4096, maxsleep=1048576, sleeptime=minsleep;
	int					c, index;
	int					setmode = 0, i = 0, daemon = 0;

	extern char *optarg;
	extern int optind, optopt;


/* Parse the command line switches and arguments */
while ((c = getopt (argc, argv, "ahm:qst:xDM:P:")) != -1)
		switch (c)
		{
			case 'a':			/* adjust time */
				setmode = 1;
				break;
			case 'h':			/* show help */
				showhelp();
				exit(0);
			case 'm':
				if ( (minsleep = atol(optarg) ) <= 0 ) {
						fprintf( stderr, "Invalid sleep time\n" );
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
				threshold = atof(optarg);
				break;
			case 'x':			/* never set time, only slew */
				setmode = 3;
				break;
			case 'D':			/* run as daemon */
				setmode = 1;
				daemon = 1;
				break;
			case 'M':
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
			if (isprint (optopt))
				fprintf (stderr, "Unknown option `-%c'.\n", optopt);
			else
				fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
			return 1;
			default:
			abort ();
		}

	/* display help page */
	if ( argv[optind] == NULL ) {
		showhelp();
		exit(0);
	}

	/* Calculate GMT offset */
	time(&gmtoffset);
	gmtoffset -= mktime(gmtime(&gmtoffset));

	/* Daemonize the program */
	if ( daemon == 1 ) {
		switch ( fork() ) {
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
			syslog ( LOG_NOTICE, "htpdate v%s started...", version);
			return 0;
		}
	}

	/* infinite loop in daemon mode,
	   out break out of the loop if daemon != 1 */
	while ( 1 ) {

	/* loop through the servers */
	for (index = optind; index < argc; index++) {

		/* host:port is stored in argv[index] */
		host = (char *)argv[index];
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

		timedelta[index - optind] = getHTTPdate( host, port, proxy, proxyport ) + gmtoffset;
	}

	/* sort the timedelta results */
	c = index - optind;
	qsort(&timedelta, c, sizeof(timedelta[0]), doublecomp);

	/* filter out the bogus timevalues:
	   - every timevalue which is more than 1 seconde off from mean
	   is considered a 'false ticker'. Ntp sync web server can never
	   be more off than a second, between two polls.
	   - offsets larger than a year from the current localtime are ignored
	*/
	timeavg = 0;
	i = 0;
	for ( index = 0; index < c; index++ ) {
		if ( ( fabs(timedelta[index]-timedelta[c/2]) < 1 ) && \
			 ( fabs(timedelta[index]) < 31536000 ) ) {
			timeavg += timedelta[index];
			i++;
		}
	}

	/* check if we have at least one valid response */
	if ( i > 0 ) {
		timeavg /= i;

		/* if the time offset is bigger than the threshold,
		   use set not adjust, unless -x was set
		*/
		if ( daemon == 1 ) {
			if ( (fabs(timeavg) > threshold) && (setmode != 3) )
				setmode = 2;		/* set time */
			else
				setmode = 1;		/* adjust time */
		}
			
		/* Do I really need to change the time?
		   No if the difference is less than the accuracy of my measurement
		*/
		if ( (fabs(timeavg) > (0.5/i))  || (daemon == 0) ) {
			setclock( timeavg, setmode, daemon );
		}
	} else if ( daemon == 0 ) {
		fprintf( stderr, "No suitable server found for time synchronization\n");
	} else {
		syslog ( LOG_ERR, "No suitable server found for time synchronization\n" );
	}


	/* Was the -D daemon switch set? Break the while loop if it wasn't */
	if ( daemon == 0 ) {
		break;
	}

	/* Calculate new sleep time, depending on offset */
	if ( fabs(timeavg) > 0.5 ) {
		sleeptime >>= 1;
		if (sleeptime < minsleep ) sleeptime = minsleep;
	} else {
		sleeptime <<= 1;
		if (sleeptime > maxsleep ) sleeptime = maxsleep;
	}	

	/* sleep time till next poll */
	syslog ( LOG_NOTICE, "Sleep for %li seconds\n", sleeptime );
	sleep( sleeptime );

	} /* while */

exit(0);

}
