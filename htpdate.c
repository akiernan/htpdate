/*
	htpdate v0.2

	Eddy Vervest (Eddy@cleVervest.com)
	http://www.clevervest.com/htp/

	Synchronize local workstation with time offered remote web servers

	Extract date/time stamp from web server response
	This program only works with the timestamps return by web servers,
	formatted as specified in RFC 2616.

	Example usage. 

	# htpdate -P wwwproxy.xs4all.nl www.xs4all.nl www.demon.nl


	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.
	http://www.gnu.org/copyleft/gpl.html

*/

#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>

#define version 		"0.2"
#define	BUFFER			2048

static int doublecomp(const void *p1, const void *p2) {
	double i = *((double *)p1);
	double j = *((double *)p2);

	if (i > j)
		return (1);
	if (i < j)
		return (-1);
	return (0);
}

double dtime( struct timeval t1, struct timeval t2 ) {

	double dt1 = t1.tv_sec + t1.tv_usec*1e-6;
	double dt2 = t2.tv_sec + t2.tv_usec*1e-6;
	return dt2-dt1;
}

double getHTTPdate( char *host, int port, char *proxy, int proxyport ) {
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

	if ( proxy == NULL ) {
		sprintf(out_buf, "HEAD / HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\nCache-Control: Max-age=0\r\n\r\n", version);
		hostinfo = gethostbyname(host);
	} else {
		sprintf(out_buf, "HEAD http://%s:%i HTTP/1.0\r\nUser-Agent: htpdate/%s\r\nPragma: no-cache\r\nCache-Control: Max-age=0\r\n\r\n", host, port, version);
		hostinfo = gethostbyname(proxy);
		port = proxyport;
	}

	if ( hostinfo ) {

	server_s = socket(AF_INET, SOCK_STREAM, 0);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr = *(struct in_addr *)*hostinfo -> h_addr_list;

	if ( connect(server_s, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0 ) {

		/* send HEAD request */
		send(server_s, out_buf, strlen(out_buf), 0);

		// Receive data from the web server
		// The return code from recv() is the number of bytes received
		if ( recv(server_s, in_buf, BUFFER, 0) != -1 ) {

			gettimeofday(&timeofday, NULL);

			if ( (pdate = (char *)strstr(in_buf, "Date: ")) != NULL ) {
				strncpy(remote_time, pdate + 11, 24);

				if ( (char *)strptime( remote_time, "%d %b %Y %H:%M:%S", &tm) != NULL) {
					// web server timestamps are without daylight savings
					tm.tm_isdst = 0;
					timevalue.tv_sec = mktime(&tm);
					timevalue.tv_usec = 500000;
				}
			}

			close(server_s);
		}

	} /* connect  */

	} /* hostinfo */

	return( dtime( timeofday, timevalue) );

}

int setclock( double timedelta, int setmode ) {
	struct timeval		timeofday;


switch (setmode) {

	case 0:						/* no time adjustment, just print time */
		printf ("Time difference %f seconds\n", timedelta );
		printf ("Use -a or -s to correct the time\n" );
		return (0);

	case 1:						/* adjust time smoothly */
		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - (long)timedelta) * 1000000);	

		printf ("Adjusting time %f seconds\n", timedelta );
		return (adjtime(&timeofday, NULL));

	case 2:						/* set time */
		gettimeofday(&timeofday, NULL);
		printf ("Time difference %f seconds\n", timedelta );
		printf ("Current time: %s\n", asctime(localtime(&timeofday.tv_sec)) );

		timedelta = ( timeofday.tv_sec + timeofday.tv_usec*1e-6 ) + timedelta;

		timeofday.tv_sec  = (long)timedelta;	
		timeofday.tv_usec = (long)((timedelta - (long)timedelta) * 1000000);	

		printf ("Set time to: %s\n", asctime(localtime(&timeofday.tv_sec)) );

		return (settimeofday(&timeofday, NULL));

	default:
		fprintf( stderr, "Error setclock" );
}

}


int main( int argc, char **argv[] ) {
	struct hostent		*hostinfo;
	time_t				gmtoffset;
	char				*host = NULL, *proxy = NULL, *portstr = NULL;
	double				timedelta[32] = {0};
	double				timeavg = 0;
	unsigned int		port = 80, proxyport = 8080;
	int					c, index;
	int					setmode = 0, i = 0;

	extern char *optarg;
	extern int optind, optopt;


while ((c = getopt (argc, argv, "aqsp:P:")) != -1)
		switch (c)
		{
			case 'a':			/* adjust time */
				setmode = 1;
				break;
			case 'q':			/* adjust time */
				setmode = 0;
				break;
			case 's':			/* set time */
				setmode = 2;
				break;
			case 'p':
				proxyport = atoi(optarg);
				break;
			case 'P':
				proxy = optarg;
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
		printf("htpdate v%s\n", version);
		printf("Usage: htpdate [-aqs] [-P <proxyserver> [-p <port>]] <host[:port]>...\n");
		printf("                -a  adjust time smoothly\n");
		printf("                -q  query only, no time change (default)\n");
		printf("                -s  set time immediate\n");
		exit(0);
	}

	/* Calculate GMT offset */
	time(&gmtoffset);
	gmtoffset -= mktime(gmtime(&gmtoffset));

	/* loop through the servers */
	for (index = optind; index < argc; index++) {

		/* host:port is stored in argv[index] */
		host = (char *)argv[index];
		portstr = strchr(host, ':');
		if ( portstr != NULL ) {
			portstr[0] = '\0';
			portstr++;
			port = atoi(portstr);
		} else {
			port = 80;
		}

		timedelta[index - optind] = getHTTPdate( host, port, proxy, proxyport );
	}

	/* sort the timedelta results */
	c = index - optind;
	qsort(&timedelta, c, sizeof(timedelta[0]), doublecomp);

	/* filter out the bogus timevalues:
	   - every timevalue which is more than 1 seconde off from mean
	   is considered a 'false ticker'.
	   - offsets larger than a year from the current localtime are ignored
	*/
	for ( index = 0; index < c; index++ ) {
		if ( ( abs(timedelta[index]-timedelta[c/2]) < 1 ) && \
			 ( abs(timedelta[index]) < 31536000 ) ) {
			timeavg += timedelta[index];
			i++;
		}
	}

	/* check if we have at least one valid response */
	if ( i > 0 ) {
		timeavg /= i;
		setclock( timedelta[c/2] + gmtoffset, setmode );
	} else {
		fprintf( stderr, "No suitable web server found for synchronization\n");
	}

}
