#ifndef _SBUFFER_H_
#define _SBUFFER_H_

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <inttypes.h>
#include "lib/tcpsock.h"
#include "connmgr.h"
#include "config.h"
#include "lib/dplist.h"

#define SBUFFER_FAILURE -1
#define SBUFFER_SUCCESS 0
#define SBUFFER_NO_DATA 1
#define SBUFFER_NO_OTHER_READER 2



typedef struct sbuffer sbuffer_t;

/*
 * All data that can be stored in the sbuffer should be encapsulated in a
 * structure, this structure can then also hold extra info needed for your implementation
 */
typedef struct sbuffer_data sbuffer_data_t;


/*
 * Allocates and initializes a new shared buffer
 * Returns SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
 */
int sbuffer_init(sbuffer_t ** buffer);


/*
 * All allocated resources are freed and cleaned up
 * Returns SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
 */
int sbuffer_free(sbuffer_t ** buffer);


/*
 * Removes the first data in 'buffer' (at the 'head') and returns this data as '*data'  
 * 'data' must point to allocated memory because this functions doesn't allocated memory
 * If 'buffer' is empty, the function doesn't block until new data becomes available but returns SBUFFER_NO_DATA
 * Returns SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
 */
int sbuffer_remove(sbuffer_t * buffer, sensor_data_t * data);


/* 
 * Inserts the data in 'data' at the end of 'buffer' (at the 'tail')
 * Returns SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
 */
int sbuffer_insert(sbuffer_t * buffer, sensor_data_t * data);

void sbuffer_destroy();


#endif  //_SBUFFER_H_

