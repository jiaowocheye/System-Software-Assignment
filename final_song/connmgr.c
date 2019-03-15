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
#include "sbuffer.h"
#include "errmacros.h"

#ifndef TIMEOUT
    #error "TIMEOUT NOT SET"
#endif


#define MAGIC_COOKIE	(long)(0xA2E1CF37D35)

#define TRUE             1
#define FALSE            0

tcpsock_t *server_socket;
dplist_t *socket_list;

struct socket_alive{
    tcpsock_t *s;
    sensor_ts_t timestamp;
    sensor_id_t sensorid;
};

typedef struct socket_alive socket_alive_t;

//callback function for the dplist
void* element_copy(void * element)
{
    socket_alive_t *new_element= malloc(sizeof(socket_alive_t));
    //check if it malloc successfully
    assert(element != NULL);
    *new_element = *(socket_alive_t *)(element);
    return (void *)new_element;
}

//callback function for the dplist
void element_free(void ** element)
{
    free(*element);
}

//callback function for the dplist
int element_compare(void * x, void * y)
{
    return 0;
}


//reference: https://www.ibm.com/support/knowledgecenter/en/ssw_i5_54/rzab6/poll.htm
void connmgr_listen(int port_number, sbuffer_t ** buffer)
{
    //create server socket using tcp_passive_open()

    //int    len, rc, on = 1;
    int rc=1;
    int server_sd = -1, new_sd = -1;
    struct pollfd fds[200];
    int nfds = 1, current_size = 0; //j; //nfds=number of fds
    char *log;

    //create socket_list
    socket_list=dpl_create(&element_copy,&element_free,&element_compare);
    if(socket_list==NULL)
    {
        printf("socket_list generating process failed\n");
    }

    int result=tcp_passive_open(&server_socket, port_number);
    if(result==TCP_MEMORY_ERROR)
    {
        printf("TCP_MEMORY_ERROR\n");
        exit(EXIT_FAILURE);
    }
    if(result==TCP_SOCKOP_ERROR)
    {
        printf("TCP_SOCKOP_ERROR\n");
        exit(EXIT_FAILURE);
    }
    printf("tcp_passive_open() successfully\n");
    result=tcp_get_sd(server_socket,&server_sd);
    fds[0].fd=server_sd;
    fds[0].events=POLLIN;
    printf("the server's sd is %d\n",server_sd);
    //error handling for the tcp_get_sd()
    fcntl(fds[0].fd,F_SETFL,O_NONBLOCK);

    //loop waiting for incoming connects or incoming data or any of the connect sockets.
    do
    {
        /***********************************************************/
        /* Call poll() and wait 3 seconds for it to complete.      */
        /***********************************************************/
        //printf("waiting on poll()...\n");
        rc=poll(fds, nfds, TIMEOUT*1000);

        /***********************************************************/
        /* Check to see if the poll call failed.                   */
        /***********************************************************/
        if (rc < 0)
        {
            perror("  poll() failed\n");
            break;
        }

        /***********************************************************/
        /* Check to see if the 3 minute time out expired.          */
        /***********************************************************/
        if (rc == 0)
        {
            printf("  poll() timed out.  End program.\n");
            break;
        }

        /***********************************************************/
        /* One or more descriptors are readable.  Need to          */
        /* determine which ones they are.                          */
        /***********************************************************/
        current_size = nfds;
        for(int i=0; i<current_size; i++)
        {
            if(fds[i].revents==POLLHUP)
            {
                close(fds[i].fd);
                    fds[i].fd=-1;
                    nfds--;
                    continue;
            }
            
            
            if(fds[i].revents == POLLIN)
            {
                //printf("there is some socket pollin\n");
                
                       
                if (fds[i].fd == server_sd) //new socket request
                {
                    printf("new request is coming, start to accept\n");
                    tcpsock_t *incoming;
                    tcp_wait_for_connection(server_socket, &incoming);
                    tcp_get_sd(incoming, &new_sd);
                    if (new_sd < 0)
                    {
                        if (errno != EWOULDBLOCK) 
                        {
                            perror("  accept() failed");
                        }
                        break;
                    }
                    
                    /*****************************************************/
                    socket_alive_t *n_s_alive=malloc(sizeof(socket_alive_t));
                    if(n_s_alive==NULL)
                    {
                        fprintf(stderr,"failed to use malloc to create socket_alive_t\n");
                        break;
                    }
                    n_s_alive->s=incoming;
                    time_t ts=time(0);
                    n_s_alive->timestamp=ts;
                    n_s_alive->sensorid=8888;
                    //free(incoming);
                    dpl_insert_at_index(socket_list,n_s_alive,dpl_size(socket_list),false);


                    /*****************************************************/
                    /* Add the new incoming connection to the            */
                    /* pollfd structure                                  */
                    /*****************************************************/
                    printf("  New incoming connection - %d\n", new_sd);
                    fds[nfds].fd = new_sd;
                    fds[nfds].events = POLLIN;
                    nfds++;
                    
                }
                
                else //packet received is data
                {
                    //printf("new data on the socket, start to read\n");
                    sensor_data_t data;
                    int bytes, result;
                    tcpsock_t *heihei;
                    int heiheidesd;
                    dplist_node_t *dummy=dpl_get_first_reference(socket_list);
                    socket_alive_t *dummy_element;

                    while(dummy!=NULL)
                    {
                        dummy_element=dpl_get_element_at_reference(socket_list,dummy);
                        dummy=dpl_get_next_reference(socket_list,dummy);
                        tcp_get_sd(dummy_element->s,&heiheidesd);
                        if(fds[i].fd==heiheidesd)
                        {
                            heihei=dummy_element->s;
                            break;
                        }
                    }
                    
                    if(heihei!=NULL)
                    {
                        bytes = sizeof(data.id);
                        result = tcp_receive(heihei,(void *)&data.id,&bytes);
                        // read temperature
                        bytes = sizeof(data.value);
                        result = tcp_receive(heihei,(void *)&data.value,&bytes);
                        // read timestamp
                        bytes = sizeof(data.ts);
                        result = tcp_receive( heihei, (void *)&data.ts,&bytes);
                        if ((result==TCP_NO_ERROR) && bytes)
                        {
                            if(dummy_element->sensorid==8888)
                            {
                                dummy_element->sensorid=data.id;
                                asprintf(&log, "A sensor node with id=%d has opened a new connection\n", data.id);
                                fifo_write(log);
                                free(log);
                            }
                            
                            sbuffer_insert(*buffer, &data);
                            dummy_element->timestamp=time(0);
                            //printf("inserted\n");
                        }
                    }
                }
            }  /* End of existing connection is readable*/
        }
        
        /*****************************************************/
        /* Check if connection is out of time                */
        /*****************************************************/
        dplist_node_t *list_node=dpl_get_first_reference(socket_list);
        while(list_node!=NULL)
        {
            dplist_node_t *reference=list_node;
            socket_alive_t *element=dpl_get_element_at_reference(socket_list,list_node);
            list_node=dpl_get_next_reference(socket_list,list_node);
            if(time(0)-(element->timestamp)>=TIMEOUT)
            {
                int sd;
                char *newlog;
                tcp_get_sd(element->s,&sd);
                close(sd);
                asprintf(&newlog,"The sensor node with id=%"PRIu16" has closed the connection\n", element->sensorid);
                fifo_write(newlog);
                free(newlog);
                char *ip_address;
                tcp_get_ip_addr(element->s, &ip_address);
                free(ip_address);
                free(element->s);
                dpl_remove_at_reference(socket_list,reference,true);

                nfds--;
            }
        }
        
    } while(1);
     
    status_close();
}

void connmgr_free()
{
    printf("connmgr_free()\n");
    dplist_node_t * list_node=dpl_get_first_reference(socket_list);
    while(list_node!=NULL)
    {
        socket_alive_t * tcpsocket=(socket_alive_t *)dpl_get_element_at_reference(socket_list,list_node);
        char *ip_address;
        tcp_get_ip_addr(tcpsocket->s, &ip_address);
        free(ip_address);
        free(tcpsocket->s);
        char *newlog;
        asprintf(&newlog,"The sensor node with id=%"PRIu16" has closed the connection\n", tcpsocket->sensorid);
        fifo_write(newlog);
        free(newlog);
        dplist_node_t *reference=list_node;
        list_node=dpl_get_next_reference(socket_list,list_node);
        dpl_remove_at_reference(socket_list,reference,true);

    }
    free(socket_list);
    free(server_socket);
}

