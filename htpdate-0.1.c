/*
getdate v0.1

Extract date/time stamp from web server response
This program works with the most common date/time stamps format specified in
RFC 822 updated by RFC 1123.

Example usage. 
Synchronize local workstation with time offered by remote web server

	~> date -s "`getdate www.microsoft.com`"

*/


#include <stdio.h>			// Needed for printf()
#include <string.h>			// Needed for strstr() and strcpy()
#include <netinet/in.h>		// Needed for internet address structure.
#include <sys/socket.h>		// Needed for socket(), bind(), etc...
#include <netdb.h>
#include <time.h>

#define  BUFFER 		2048	// Allocate buffer for requests and responses
#define  HEAD 			"HEAD / HTTP/1.0\r\nPragma: no-cache\r\nCache-Control: Max-age=0\r\n\r\n"
#define  DATE_LINE		"Date: "

//	Declare function
void PrintErrorAndExit	( char *msg, short code );

int main(int argc, char *argv[]) {

	unsigned int         server_s;			// Server socket descriptor
	struct sockaddr_in   server_addr;		// Server Internet address
	char                 out_buf[BUFFER];	// Output buffer for HEAD request
	char                 in_buf[BUFFER];	// Input buffer for response
	unsigned int         retcode;			// Return code

	char				remote_time[32];
	char				*host, *pdate;
	int					port;
	struct				hostent	*hostinfo;

	if ( argc == 1 ) {
		printf ("Usage: getdate [host] <port>\n");
		exit(0);
	} else {
		host = argv[1];
		hostinfo = gethostbyname(host);
		if (!hostinfo) {
			PrintErrorAndExit( "Host not found", 1 );
		}
		// port argument
		if ( argv[2] ) {
			port = atoi ( argv[2] );
		} else {
			port = 80;
		}
	}

	// Create a socket
	server_s = socket(AF_INET, SOCK_STREAM, 0);

	// The web server socket's address information
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr = *(struct in_addr *)*hostinfo -> h_addr_list;

	// Do a connect (connect() blocks)
	retcode = connect(server_s, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if ( retcode != 0 )
	{
		PrintErrorAndExit( "Connection to host failed", 2 );
	}

	// Send the most simple HEAD request
	strcpy(out_buf, HEAD);
	send(server_s, out_buf, strlen(out_buf), 0);

	// Receive data from the web server
	// The return code from recv() is the number of bytes received
	retcode = recv(server_s, in_buf, BUFFER, 0);
	if ( retcode == -1 ) {
		PrintErrorAndExit("No data received", 3);
	}

	if ( pdate = (char *)strstr(in_buf, DATE_LINE) ) {
		strncpy(remote_time, pdate +  6, 29);
		printf ("%s\n", remote_time);
	} else {
		PrintErrorAndExit("Date couldn't be retreived", 4);
	}

	close(server_s);

}

void PrintErrorAndExit ( char *msg, short code ) {
	fprintf (stderr, "%s\n", msg);
	exit( code );
}
