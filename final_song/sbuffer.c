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
#include <inttypes.h>
#include <time.h>
#include <fcntl.h>
#include <inttypes.h>
#include "lib/tcpsock.h"
#include "connmgr.h"
#include "config.h"
#include "lib/dplist.h"
#include "sbuffer.h"
#include "errmacros.h"
#include <pthread.h>
/*
 * All data that can be stored in the sbuffer should be encapsulated in a
 * structure, this structure can then also hold extra info needed for your implementation
 */

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_barrier_t barrier;

struct sbuffer_data {
    sensor_data_t data;
};

typedef struct sbuffer_node {
  struct sbuffer_node * next;
  sbuffer_data_t element;
  int edit_flag;
} sbuffer_node_t;

struct sbuffer {
  sbuffer_node_t * head;
  sbuffer_node_t * tail;
};	

int sbuffer_init(sbuffer_t ** buffer)
{
  *buffer = malloc(sizeof(sbuffer_t));
  if (*buffer == NULL) return SBUFFER_FAILURE;
  (*buffer)->head = NULL;
  (*buffer)->tail = NULL;
  pthread_barrier_init(&barrier, NULL, 2);
  return SBUFFER_SUCCESS; 
}


int sbuffer_free(sbuffer_t ** buffer)
{
  sbuffer_node_t * dummy;
  if ((buffer==NULL) || (*buffer==NULL)) 
  {
    return SBUFFER_FAILURE;
  } 
  while ( (*buffer)->head )
  {
    dummy = (*buffer)->head;
    (*buffer)->head = (*buffer)->head->next;
    free(dummy);
  }
  free(*buffer);
  *buffer = NULL;
  return SBUFFER_SUCCESS;		
}


int sbuffer_remove(sbuffer_t * buffer,sensor_data_t * data)
{
    pthread_barrier_wait(&barrier);
    pthread_mutex_lock(&mutex);
    sbuffer_node_t * dummy;
    if (buffer == NULL) 
    {
        pthread_mutex_unlock(&mutex);
        return SBUFFER_FAILURE;
    }
        
    if (buffer->head == NULL) 
    {
        printf("SBUFFER_NO_DATA");
        pthread_mutex_unlock(&mutex);
        return SBUFFER_NO_DATA;
    }
    
    *data = buffer->head->element.data;
    dummy = buffer->head;

    if(dummy->edit_flag==1)
    {
        if (buffer->head == buffer->tail) // buffer has only one node
        {
            buffer->head = buffer->tail = NULL; 
        }
        else  // buffer has many nodes empty
        {
            buffer->head = buffer->head->next;
        }
        free(dummy);
    } else
    {
        dummy->edit_flag=1;
    }
    
    decrease_nr_node();
    pthread_mutex_unlock(&mutex);
    return SBUFFER_SUCCESS;
}


int sbuffer_insert(sbuffer_t * buffer, sensor_data_t * data)
{
    pthread_mutex_lock(&mutex);
    //printf("The sensor node with id=%"PRIu16" inserting \n",data->id);
    sbuffer_node_t * dummy;
    if (buffer == NULL) 
    {
        pthread_mutex_unlock(&mutex);
        return SBUFFER_FAILURE;
    }
    dummy = malloc(sizeof(sbuffer_node_t));
    if (dummy == NULL) 
    {
        pthread_mutex_unlock(&mutex);
        return SBUFFER_FAILURE;
    }
    dummy->element.data = *data;
    dummy->edit_flag=0;
    dummy->next = NULL;
    if (buffer->tail == NULL) // buffer empty (buffer->head should also be NULL
    {
        buffer->head = buffer->tail = dummy;
    } 
    else // buffer not empty
    {
        buffer->tail->next = dummy;
        buffer->tail = buffer->tail->next; 
    }
    printf("The sensor node with id=%"PRIu16" success \n",data->id);
    pthread_mutex_unlock(&mutex);
    increase_nr_node();
    return SBUFFER_SUCCESS;
}

void sbuffer_destroy()
{
     pthread_mutex_destroy(&mutex);
     pthread_barrier_destroy(&barrier);
}

