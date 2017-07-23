/* 
 * File: sws.c
 * Author: Alex Brodsky
 * Purpose: This file contains the implementation of a simple web server.
 *          It consists of two functions: main() which contains the main 
 *          loop accept client connections, and serve_client(), which
 *          processes each client request.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>

#include "network.h"
#include "scheduler.h"

#define MAX_HTTP_SIZE 8192                 /* size of buffer to allocate */
#define MAX_REQS 64                        /* maximum number of requests */

static rcb requests[MAX_REQS];             /* request table */
static rcb *free_rcb;
static int next_req = 1;                   /* request counter */

/* This function takes a file handle to a client, reads in the request, 
 *    parses the request, and sends back the requested file.  If the
 *    request is improper or the file is not available, the appropriate
 *    error is sent back.
 * Parameters: 
 *             fd : the file descriptor to the client connection
 * Returns: None
 */
static rcb * serve_client( int fd ) {
  static char *buffer;                              /* request buffer */
  struct stat st;                                   /* struct for file size */
  char *req = NULL;                                 /* ptr to req file */
  char *brk;                                        /* state used by strtok */
  char *tmp;                                        /* error checking ptr */
  FILE *fin;                                        /* input file handle */
  int len = 0;                                      /* length of data read */
  int left = MAX_HTTP_SIZE;                         /* amount of buffer left */
  rcb *r;                                           /* new rcb */

  if( !buffer ) {                                   /* 1st time, alloc buffer */
    buffer = malloc( MAX_HTTP_SIZE );
    if( !buffer ) {                                 /* error check */
      perror( "Error while allocating memory" );
      abort();
    }
  }

  memset( buffer, 0, MAX_HTTP_SIZE );
  for( tmp = buffer; !strchr( tmp, '\n' ); left -= len ) { /* read req line */
    tmp += len;
    len = read( fd, tmp, left );                    /* read req from client */
    if( len <= 0 ) {                                /* if read incomplete */
      perror( "Error while reading request" );      /* no need to go on */
      close( fd );
      return NULL;
    }
  } 

  /* standard requests are of the form
   *   GET /foo/bar/qux.html HTTP/1.1
   * We want the second token (the file path).
   */
  tmp = strtok_r( buffer, " ", &brk );              /* parse request */
  if( tmp && !strcmp( "GET", tmp ) ) {
    req = strtok_r( NULL, " ", &brk );
  }
 
  if( !req ) {                                      /* is req valid? */
    len = sprintf( buffer, "HTTP/1.1 400 Bad request\n\n" );
    write( fd, buffer, len );                       /* if not, send err */
  } else {                                          /* if so, open file */
    req++;                                          /* skip leading / */
    fin = fopen( req, "r" );                        /* open file */
    if( !fin ) {                                    /* check if successful */
      len = sprintf( buffer, "HTTP/1.1 404 File not found\n\n" );  
      write( fd, buffer, len );                     /* if not, send err */
    } else if( !fstat( fileno( fin ), &st ) ) {     /* if so, start request */
      len = sprintf( buffer, "HTTP/1.1 200 OK\n\n" );/* send success code */
      write( fd, buffer, len );

      r = free_rcb;                                 /* allocate RCB */
      assert( r );
      free_rcb = free_rcb->next;
      memset( r, 0, sizeof( rcb ) );

      r->seq = next_req++;                          /* init RCB */
      r->client = fd;
      r->file = fin;
      r->left = st.st_size;
      return r;                                     /* return rcb */
    }
    fclose( fin );
  }
  close( fd );                                     /* close client connectuin*/
  return NULL;                                     /* not a valid request */
}


/* This function takes a file handle to a client, reads in the request, 
 *    parses the request, and sends back the requested file.  If the
 *    request is improper or the file is not available, the appropriate
 *    error is sent back.
 * Parameters: 
 *             fd : the file descriptor to the client connection
 * Returns: None
 */
static int serve( rcb *req ) {
  static char *buffer;                              /* request buffer */
  int len;                                          /* length of data read */
  int n;                                            /* amount to send */

  if( !buffer ) {                                   /* 1st time, alloc buffer */
    buffer = malloc( MAX_HTTP_SIZE );
    if( !buffer ) {                                 /* error check */
      perror( "Error while allocating memory" );
      abort();
    }
  }

  n = req->left;                                     /* compute send amount */
  if( !n ) {                                         /* if 0, we're done */
    return 0;
  } else if( req->max && ( req->max < n ) ) {        /* if there is limit */
    n = req->max;                                    /* send upto the limit */
  }
  req->last = n;                                    /* remember send size */

  do {                                              /* loop, read & send file */
    len = n < MAX_HTTP_SIZE ? n : MAX_HTTP_SIZE;    /* how much to read */
    len = fread( buffer, 1, len, req->file );         /* read file chunk */
    if( len < 1 ) {                                 /* check for errors */
      perror( "Error while reading file" );
      return 0;
    } else if( len > 0 ) {                          /* if none, send chunk */
      len = write( req->client, buffer, len );
      if( len < 1 ) {                               /* check for errors */
        perror( "Error while writing to client" );
        return 0;
      }
      req->left -= len;                              /* reduce what remains */
      n -= len;
    }
  } while( ( n > 0 ) && ( len == MAX_HTTP_SIZE ) );  /* the last chunk < 8192 */
  
  return req->left > 0;                            /* return true if not done */
}


/* This function is where the program starts running.
 *    The function first parses its command line parameters to determine port #
 *    Then, it initializes, the network and enters the main loop.
 *    The main loop waits for a client (1 or more to connect, and then processes
 *    all clients by calling the seve_client() function for each one.
 * Parameters: 
 *             argc : number of command line parameters (including program name
 *             argv : array of pointers to command line parameters
 * Returns: an integer status code, 0 for success, something else for error.
 */
int main( int argc, char **argv ) {
  int port = -1;                                   /* server port # */
  int fd;                                          /* client file descriptor */
  rcb *req;                                        /* next request to process */
  int i;

  /* check for and process parameters 
   */
  if( ( argc < 3 ) || ( sscanf( argv[1], "%d", &port ) < 1 ) ) {
    printf( "usage: sms <port> <scheduler>\n" );
    return 0;
  } 

  scheduler_init( argv[2] );                        /* init scheduler */
  network_init( port );                             /* init network module */

  free_rcb = requests;                             /* create RCB free list */
  for( i = 0; i < MAX_REQS - 1; i++ ) {
    requests[i].next = &requests[i+1];
  }

  for( ;; ) {                                       /* main loop */
    network_wait();                                 /* wait for clients */
    
    do {
      for( fd = network_open(); fd >= 0; fd = network_open() ) { /*get clients*/
        assert( free_rcb );                            /* assume we can serve */
        req =  serve_client( fd );                     /* process each client */
        if( req ) {
          scheduler_submit( req );
        }
      }

      req = scheduler_get_next();
      if( req && serve( req ) ) {
        scheduler_submit( req );
      } else if( req ) {
        req->next = free_rcb;
        free_rcb = req;
        fclose( req->file );
        close( req->client );
        printf( "Request %d completed.\n", req->seq );
        fflush( stdout );
      }
    } while( req );
  }
}
