/* DETAILED "SUMMARY" OF MY "ONE SBUFFER" SOLUTION
 * (I know this "SUMMARY" is pretty long... Sorry for that T_T)
 * 
 * (For checking the TCP connection, I used poll() function.)
 * 
 * 1. method used to deal with multi-threads
 * 
 *      I used mutex+barrier for sbuffer to synchronize main process threads. 
 * 
 *      "Reader threads" (datamgr and storagemgr) use sbuffer_remove() function to get data from sbuffer, so I set the barrier in 
 *      sbuffer_remove() function to avoid the situation in which a sbuffer node is visited twice by one thread.  
 *      
 *      After reader threads are synchronized (both reached the next barrier), the barrier will release, reader threads start to rush for 
 *      mutex. The one who get it successfully will lock the mutex and execute the following operations. There's an edit_flag inside 
 *      the sbuffer node to tell reader threads whether this node has been read by someone. If there is no data or the data has been 
 *      read/edited successfully, the thread will unlock the mutex, reach the next barrier and wait for the other thread.
 * 
 *      Barrier is initialized when buffer is created, and is destroyed when sbuffer is destroyed.
 *  
 * 2. flag used to indicate there is something in the sbuffer
 *      
 *      I set a flag "nr_node". 
 * 
 *      The value of the "nr_node" will be changed in sbuffer_insert() and sbuffer_remove() functions.
 * 
 *      sbuffer_insert() function will increase the value of nr_node if connmgr successfully used this method to pass a node into the sbuffer.      *      If the sbuffer_remove() has read a node successfully, it will delete the node from the buffer and decrease the value of nr_node.
 * 
 *      Reader threads will check the value of the flag. If nr_node>0, the reader threads will start executing "parse" methods, if not, 
 *      they will wait(sleep(1)) for sometime and check if there is new data coming in by check again the value of nr_node. 
 * 
 * 3. flag used to indicate there is TCP connection
 * 
 *      I set a flag "conn_status".
 * 
 *      The flag will be set/unset inside connmgr_listen() function.
 * 
 *      If the TCP connection is established successfully, the "conn_status" flag will be set. Reader threads will check the flag, 
 *      and execute following step if it is set.
 */

#define _GNU_SOURCE
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include "lib/dplist.h"
#include "lib/tcpsock.h"
#include "connmgr.h"
#include "sensor_db.h"
#include "datamgr.h"
#include "sbuffer.h"
#include "errmacros.h"
#include <wait.h>

#define FIFO_NAME "logFifo"             
#define EXIT_FAILURE_FORK  -1
#define MAX_SIZE      200
#define LOG_FILE_NAME  "gateway.log"
#define MAP "room_sensor.map"
#define DEFAULT_PORT    1234

/* global variables */
    int sequence_number=0;            //mark the sequence number of the log message
    sbuffer_t * sbuffer;              //shared buffer for threads in main processes
    int result;                       //check the result of mkfifo
    char *str_result;                 //check if process has fetched some contents from fifo
    char recv_buf[MAX_SIZE];          //a buffer to store log message send from threads into fifo
    char send_buf[MAX_SIZE];          //a buffer to fetch log message received from fifo
    pid_t child_pid;                  //child process id
    int port=DEFAULT_PORT;            //default port, make sure there's a port defined
    volatile  pid_t log_pid;          //store child process id, make it visitable for every process
    DBCONN * connection;              //database connection
    int conn_status=1;                //a flag to indecate whether there is TCP connection
    int nr_node=0;                    //a flag to tell threaeds whether there is something in the sbuffer
    
    //lock the process when there is a thread writing something into fifo, avoid conflict and clog
    pthread_mutex_t log_mutex=PTHREAD_MUTEX_INITIALIZER;
    
    pthread_t thread_datamgr, thread_sensor_db,thread_connmgr;
    
    FILE *fp_log, *fifo_r, *fifo_w, *fp_map;

/* Function declareations */

//for child process, read from fifo + write to log file
int read_from_fifo();

//for all threads created by parent process, write messages into fifo and pass to child process
void fifo_write(char *logmsg);

void signal_handler(int sig);

//for threads
void * run_connmgr();

void * run_datamgr();

void * run_sensor_db();


int read_from_fifo()
{
    fifo_r = fopen(FIFO_NAME, "r"); 
    int bytes=0;
    fp_log=fopen(LOG_FILE_NAME,"a");
    
    do 
    {
        str_result = fgets(recv_buf, MAX_SIZE, fifo_r);
        bytes=sizeof(str_result);
        if ( str_result != NULL )
        {   
            char *log;
            asprintf(&log,"%d %ld %s", ++sequence_number, time(0), recv_buf); 
            printf ("Message receive: this %s\n",recv_buf);
            if(fp_log!=NULL)
            {  
                fseek(fp_log,0,2);
                fprintf(fp_log, "%s\n",log);
                fflush(fp_log);
            }
            free(log);
        }
    } while ( str_result != NULL ); 
    
    free(str_result);
    fclose(fp_log);
    fclose(fifo_r);
    return bytes;
}


void fifo_write(char *logmsg)
{
    pthread_mutex_lock(&log_mutex);
    
    char * send_buf;
    fifo_w = fopen(FIFO_NAME, "w"); 
    
    asprintf(&send_buf,"%s",logmsg);
    fputs( send_buf, fifo_w );
    FFLUSH_ERROR(fflush(fifo_w));
    printf("Message send: %s", send_buf); 
    
    free(send_buf);
    fclose(fifo_w);
    
    pthread_mutex_unlock(&log_mutex);
}


//for log process
void signal_handler(int sig)
{
    if(sig==SIGINT)
    {
        printf("closing  fifo\n");
        //fclose(fifo_r);
        exit (0);
    }
}

/*
 * 
 */
void * run_connmgr( )
{
        printf("this is connmgr thread id= %lu\n",pthread_self());
        connmgr_listen(port,&sbuffer);
        fprintf(stderr,"timeout reached,cleaning up and exiting\n ");
        return (void *)0;
}

//called by sbuffer_insert()
void increase_nr_node()
{
    nr_node++;
}

//called by sbuffer_remove()
void decrease_nr_node()
{
    nr_node--;
}

//called by connmgr_listen()
void status_close()
{
    conn_status=0;
    printf("conn_status==close\n");
}

/*
 * 
 */
void * run_datamgr()
{
    printf("this is datamgr thread id= %lu\n",pthread_self());
    FILE *fp_map=fopen(MAP,"r");
    if(fp_map!=NULL)printf("open room sensor map\n");
    
    while(conn_status==1)
    {
        if(nr_node>0)
        {
            datamgr_parse_sensor_data(fp_map, &sbuffer);
            sleep(1);
        }
        if(nr_node==0)
            sleep(1);
    }
    
    fclose(fp_map);
    return (void*)0;
}
/*
 * 
 */
void * run_sensor_db(void *id)
{
    printf("this is sensor_db thread id= %lu\n",pthread_self());
    
    while(conn_status==1)
    {
        if(nr_node>0)
        {
            storagemgr_parse_sensor_data(connection, &sbuffer);
            sleep(1);
        }
        if(nr_node==0)
            sleep(1);
    }

    return (void *)0;
}

int main(int argc, char *argv[])
{
    //receive arguments from terminal
    if(argc==2)
    {
        port=atoi(argv[1]);
    }
    
    //make fifo avaliable for main and child process
    result = mkfifo(FIFO_NAME, 0666);
    CHECK_MKFIFO(result); 
    
    //create child process
    child_pid=fork();
    
    if(child_pid==0) //if fork succeeded and this is child process
    {
        printf("I'm child process, pid= %d\n",getpid());
        signal(SIGINT,signal_handler);
        //keep fifo open until it receives "close" signal
        while (1)
            read_from_fifo();
           
    } else //it's main process
    {
        int child_exit_status;
        log_pid=child_pid;
        printf("I'm main process, pid= %d \n",getpid());

        sbuffer_init(&sbuffer);
        //initialize the database connection
        connection=init_connection(1);
        
        if(connection!=NULL)
        {
            printf("Database connection ok\n");
        }
	int id_datamgr, id_sensor_db,id_connmgr;
        
	id_datamgr = 1;	
        pthread_create( &thread_datamgr, NULL, &run_datamgr, &id_datamgr );
	
	id_sensor_db = 2;	
        pthread_create( &thread_sensor_db, NULL, &run_sensor_db, &id_sensor_db );
	
        id_connmgr = 3;	
        pthread_create( &thread_connmgr, NULL, &run_connmgr, &id_connmgr );
	
	// important: don't forget to join, otherwise main thread exists and destroys the mutex
	pthread_join(thread_datamgr, NULL);       
        pthread_join(thread_sensor_db, NULL);
        pthread_join(thread_connmgr, NULL);
        
        /*clean up and exit*/
        if(fifo_r!=NULL)
            fclose(fifo_r);
        
        if(fp_log!=NULL)
             fclose(fp_log);
        
        disconnect(connection);
        connmgr_free();
        datamgr_free();
        sbuffer_free(&sbuffer);
        sbuffer_destroy();
        kill(log_pid,SIGINT);
    
        
        SYSCALL_ERROR( waitpid(log_pid, &child_exit_status,0) );
        if ( WIFEXITED(child_exit_status) )
        {
            printf("Child %d terminated with exit status %d\n", log_pid, WEXITSTATUS(child_exit_status));
        }
        else
        {
            printf("Child %d terminated abnormally\n", log_pid);
        }

    }
    
    return 0;
}


