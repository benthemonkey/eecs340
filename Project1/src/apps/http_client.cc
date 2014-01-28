#include "minet_socket.h"
#include <stdlib.h>
#include <iostream>
#include <ctype.h>

using namespace std;

#define BUFSIZE 1024

int write_n_bytes(int fd, char * buf, int count);

int main(int argc, char * argv[]) {
    char * server_name = NULL;
    int server_port = 0;
    char * server_path = NULL;

    int sock = 0;
    int rc = -1;
    int datalen = 0;
    bool ok = true;
    struct sockaddr_in * sa;
    FILE * wheretoprint = stdout;
    struct hostent * site = NULL;
    char * req = NULL;

    char buf[BUFSIZE + 1];
    char * bptr = NULL;
    char * bptr2 = NULL;
    char * endheaders = NULL;
   
    struct timeval timeout;
    fd_set set;

    /*parse args */
    if (argc != 5) {
	fprintf(stderr, "usage: http_client k|u server port path\n");
	exit(-1);
    }

    server_name = argv[2];
    server_port = atoi(argv[3]);
    server_path = argv[4];



    /* initialize minet */
    if (toupper(*(argv[1])) == 'K') { 
	minet_init(MINET_KERNEL);
    } else if (toupper(*(argv[1])) == 'U') { 
	minet_init(MINET_USER);
    } else {
	fprintf(stderr, "First argument must be k or u\n");
	exit(-1);
    }

    /* create socket */
    sock = minet_socket(SOCK_STREAM);

    // Do DNS lookup
    /* Hint: use gethostbyname() */
    site = gethostbyname(server_name);

    /* set address */
    sa = (sockaddr_in *)malloc(sizeof(sockaddr_in));
    memset(sa, 0, sizeof(sockaddr_in));

    sa->sin_family = AF_INET;
    memcpy((char*) &(sa->sin_addr), (char*) site->h_addr, site->h_length);
    //sa->sin_addr = (site->h_addr);
    sa->sin_port = htons(server_port);

    //int x = minet_bind(sock, sa);  
    //cout << x << "\n";
 
    /* connect socket */
    if (minet_connect(sock, sa) != 0){
        cout << "did not connect\n";
        ok = false;
    };

    cout << "1\n";

    /* send request */
    req = (char *)malloc(strlen(server_path) + 15);
    sprintf(req, "GET %s HTTP/1.0\n\n", server_path);
    if (ok && minet_write(sock, req, strlen(req)) < 0)
    {
        cout << "failed to write request\n";
        ok = false;
    }

    cout << "2\n";

    /* wait till socket can be read */
    /* Hint: use select(), and ignore timeout for now. */
    FD_ZERO(&set);
    FD_SET(sock, &set);
    if (ok && !FD_ISSET(sock, &set))
    {
        cout << "socket not set\n";
        ok = false;
    }

    cout << "3\n";
    if (ok && minet_select(sock+1,&set,0,0,&timeout) < 1) {
        cout << "select socket error\n";
        ok = false;
    }

    cout << "4\n";

    /* first read loop -- read headers */
    if (ok && minet_read(sock, buf, BUFSIZE) < 0)
    {
        cout << "failed to read\n";
        ok = false;
    }

    cout << buf << "\n";

    /* examine return code */   
    //Skip "HTTP/1.0"
    //remove the '\0'
    // Normal reply has return code 200

    /* print first part of response */

    /* second read loop -- print out the rest of the response */
    
    /*close socket and deinitialize */
    minet_close(sock);

    if (ok) {
	return 0;
    } else {
	return -1;
    }
}

int write_n_bytes(int fd, char * buf, int count) {
    int rc = 0;
    int totalwritten = 0;

    while ((rc = minet_write(fd, buf + totalwritten, count - totalwritten)) > 0) {
	totalwritten += rc;
    }
    
    if (rc < 0) {
	return -1;
    } else {
	return totalwritten;
    }
}


