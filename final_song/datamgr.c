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
#include "datamgr.h"

#define sensor_id_INVALID_ERROR "sensor_id is invalid"

#ifndef RUN_AVG_LENGTH
#define RUN_AVG_LENGTH 5
#endif

#ifndef SET_MIN_TEMP
#error MIN TEMP NOT DEFINED
#endif

#ifndef SET_MAX_TEMP
#error MAX TEMP NOT DEFINED
#endif

dplist_t *list=NULL;

typedef uint16_t room_id_t;

typedef struct {
    room_id_t room_id;
    sensor_data_t sensor_data;
    sensor_value_t running_avg;
    sensor_value_t last_modified[RUN_AVG_LENGTH];
    int num_of_data;
}room_sensor_t;

void * data_element_copy(void * element)
{
    room_sensor_t *copy;
    copy = (room_sensor_t *)malloc(sizeof(room_sensor_t));

    *copy= *(room_sensor_t *)(element);
    return (void *)copy;
}
void data_element_free(void ** element)
{
    free(*element);
}

int data_element_compare(void *x, void *y)
{
    return (((*(uint16_t *)x)<(*(uint16_t *)y)) ? -1 : ((*(uint16_t *)x)==((*(uint16_t *)y))? 0 : 1));
}

void datamgr_parse_sensor_files(FILE * fp_sensor_map, FILE * fp_sensor_data)
{
    list=dpl_create(&data_element_copy, &data_element_free, &data_element_compare);

    uint16_t room_id=0;
    uint16_t sensor_id=0;
    room_sensor_t *room_sensor=NULL;
    room_sensor=(room_sensor_t *)malloc(sizeof(room_sensor_t));
    int num_of_data=0;
    
    //loading sensor-room info
    while(fscanf(fp_sensor_map,"%"SCNd16 "%"SCNd16, &room_id, &sensor_id)!=EOF)
    {
        room_sensor->room_id=room_id;
        room_sensor->sensor_data.id=sensor_id;
        room_sensor->num_of_data=0;
        dpl_insert_at_index(list, room_sensor, 0, true);
    }
    
    dplist_node_t *list_node=NULL;
    list_node=dpl_get_first_reference(list);
    room_sensor_t *element=NULL;
    element=dpl_get_element_at_reference(list,list_node);
    
    
    //load sensor data into sensor_value_t value
    while(!feof(fp_sensor_data))
    {
        /*fread(&(room_sensor->sensor_data.id),sizeof(room_sensor->sensor_data.id),1,fp_sensor_data);
        fread(&(room_sensor->sensor_data.value),sizeof(room_sensor->sensor_data.value),1,fp_sensor_data); //temperature
        fread(&(room_sensor->sensor_data.ts),sizeof(room_sensor->sensor_data.ts),1,fp_sensor_data); //timestamp*/
        
        fread(&(room_sensor->sensor_data.id),sizeof(sensor_id_t),1,fp_sensor_data);
        fread(&(room_sensor->sensor_data.value),sizeof(room_sensor->sensor_data.value),1,fp_sensor_data); //temperature
        if(fread(&(room_sensor->sensor_data.ts),sizeof(sensor_ts_t),1,fp_sensor_data)>0) //timestamp
        {
            //traverse all elements in the list, insert @ the right sensor
       
            list_node=dpl_get_first_reference(list);
            element=dpl_get_element_at_reference(list,list_node);
        
            
            while(list_node!=NULL)
            {
                if (element->sensor_data.id==room_sensor->sensor_data.id)
                {
                    element->sensor_data.value=room_sensor->sensor_data.value;
                    element->sensor_data.ts=room_sensor->sensor_data.ts;
                    (element->num_of_data)+=1;
                    int num=element->num_of_data;
                    element->last_modified[num%RUN_AVG_LENGTH]=element->sensor_data.value;
                    
                    num_of_data++;
                    
                    if(num>=RUN_AVG_LENGTH)
                    {
                        sensor_value_t total=0;
                        for(int i=0; i<RUN_AVG_LENGTH; i++)
                        {
                            total+=element->last_modified[i];
                        }

                        element->running_avg=total/RUN_AVG_LENGTH;
                        
                        if(element->running_avg>SET_MAX_TEMP)
                            fprintf(stderr, "ROOM %"SCNd16 " is too hot!\n",element->room_id);
                        if(element->running_avg<SET_MIN_TEMP)
                            fprintf(stderr, "ROOM %"SCNd16 " is too cold!\n",element->room_id);
                    }
                    break;
                }
                list_node=dpl_get_next_reference(list,list_node);
                if(list_node!=NULL)
                    element=dpl_get_element_at_reference(list,list_node);
            }
        }
    }
    
    printf("%d data was added\n", num_of_data);
    free(room_sensor);
}

/*
 * Reads continiously all data from the shared buffer data structure, parse the room_id's
 * and calculate the running avarage for all sensor ids
 * When *buffer becomes NULL the method finishes. This method will NOT automatically free all used memory
 */
void datamgr_parse_sensor_data(FILE * fp_sensor_map, sbuffer_t ** buffer)
{
    int num_of_data=0;
    if(list==NULL)
    {
        list=dpl_create(&data_element_copy, &data_element_free, &data_element_compare);

        uint16_t room_id=0;
        uint16_t sensor_id=0;
        room_sensor_t *room_sensor=NULL;
        room_sensor=(room_sensor_t *)malloc(sizeof(room_sensor_t));
        
        //loading sensor-room info
        while(fscanf(fp_sensor_map,"%"SCNd16 "%"SCNd16, &room_id, &sensor_id)!=EOF)
        {
            room_sensor->room_id=room_id;
            room_sensor->sensor_data.id=sensor_id;
            room_sensor->num_of_data=0;
            dpl_insert_at_index(list, room_sensor, 0, true);
        }
        free(room_sensor);
    }
    

    //load sensor data into sensor_value_t value

    //traverse all elements in the list, insert @ the right sensor
    dplist_node_t *list_node=NULL;
    list_node=dpl_get_first_reference(list);
    room_sensor_t *element=NULL;
    element=dpl_get_element_at_reference(list,list_node);
    sensor_data_t data;

    //int result=
    sbuffer_remove(*buffer, &data);
    //printf("data.id=%"PRIu16"\n", data.id);
    //printf("data: %"PRIu16" %ld %f\n", data.id, data.value, data.ts);
    char *log;
    //int size=dpl_size(list);
    int count=0;
    
    while(list_node!=NULL)
    {
        //printf("hello from while inside datamgr while\n");
        if (element->sensor_data.id==data.id)
        {
            count=1;
            //printf("found data,id=%"PRIu16"\n", data.id);
            element->sensor_data.value=data.value;
            element->sensor_data.ts=data.ts;
            (element->num_of_data)+=1;
            int num=element->num_of_data;
            element->last_modified[num%RUN_AVG_LENGTH]=element->sensor_data.value;
            
            num_of_data++;
            
            if(num>=RUN_AVG_LENGTH)
            {
                sensor_value_t total=0;
                for(int i=0; i<RUN_AVG_LENGTH; i++)
                {
                    total+=element->last_modified[i];
                }

                element->running_avg=total/RUN_AVG_LENGTH;
                
                if(element->running_avg>SET_MAX_TEMP)
                {
                    asprintf(&log, "The sensor node with %"PRIu16" reports it's too hot(running avg temperature=%d)\n",element->sensor_data.id, (int)element->running_avg);
                    fifo_write(log);
                    free(log);
                }
                if(element->running_avg<SET_MIN_TEMP)
                {
                    asprintf(&log, "The sensor node with %"PRIu16" reports it's too cold(running avg temperature=%d)\n",element->sensor_data.id, (int)element->running_avg);
                    fifo_write(log);
                    free(log);
                }
                    
            }
            break;
        }
        list_node=dpl_get_next_reference(list,list_node);
        if(list_node!=NULL)
            element=dpl_get_element_at_reference(list,list_node);
    }
    
    //printf("%d %d\n",count,size);
    
    if(count==0)
    {
        asprintf(&log, "invalid sensor node ID %"PRIu16 " asprintf\n",data.id);
        printf(" from asprintf  %s\n", log);
        fifo_write(log);
        free(log);
    }
    
    //printf("%d data was added\n", num_of_data);
}

/*
 * This method should be called to clean up the datamgr, and to free all used memory.
 * After this, any call to datamgr_get_room_id, datamgr_get_avg, datamgr_get_last_modified or datamgr_get_total_sensors will not return a valid result
 */
void datamgr_free()
{
    dpl_free(&(list),true);
}

/*
 * Gets the room ID for a certain sensor ID
 * Use ERROR_HANDLER() if sensor_id is invalid
 */
uint16_t datamgr_get_room_id(sensor_id_t sensor_id)
{
    ERROR_HANDLER((sensor_id)<0,sensor_id_INVALID_ERROR);

    dplist_node_t *list_node=dpl_get_first_reference(list);
    room_sensor_t *element=dpl_get_element_at_reference(list,list_node);

    while(list_node!=NULL)
    {
        if(element->sensor_data.id==sensor_id)
            return element->room_id;
        list_node=dpl_get_next_reference(list,list_node);
        if(list_node!=NULL)
            element=dpl_get_element_at_reference(list,list_node);
    }
    printf("room not found\n");
    return (uint16_t)0;
}


/*
 * Gets the running AVG of a certain senor ID (if less then RUN_AVG_LENGTH measurements are recorded the avg is 0)
 * Use ERROR_HANDLER() if sensor_id is invalid
 */
sensor_value_t datamgr_get_avg(sensor_id_t sensor_id)
{
    ERROR_HANDLER((sensor_id)<0,sensor_id_INVALID_ERROR);

    dplist_node_t *list_node=dpl_get_first_reference(list);
    room_sensor_t *element=dpl_get_element_at_reference(list,list_node);

    while(list_node!=NULL)
    {
        if(element->sensor_data.id==sensor_id)
            return element->running_avg;

        list_node=dpl_get_next_reference(list,list_node);
        if(list_node!=NULL)
            element=dpl_get_element_at_reference(list,list_node);
    }
    printf("sensor data not found\n");
    return (sensor_value_t)0;
}


/*
 * Returns the time of the last reading for a certain sensor ID
 * Use ERROR_HANDLER() if sensor_id is invalid
 */
time_t datamgr_get_last_modified(sensor_id_t sensor_id)
{
    ERROR_HANDLER((sensor_id)<0,sensor_id_INVALID_ERROR);

    dplist_node_t *list_node=dpl_get_first_reference(list);
    room_sensor_t *element=dpl_get_element_at_reference(list,list_node);

    while(list_node!=NULL)
    {
        if(element->sensor_data.id==sensor_id)
            return element->sensor_data.ts;

        list_node=dpl_get_next_reference(list,list_node);
        if(list_node!=NULL)
            element=dpl_get_element_at_reference(list,list_node);
    }
    printf("last_modified not found\n");
    return (time_t)0;
}


/*
 *  Return the total amount of unique sensor ID's recorded by the datamgr
 */
int datamgr_get_total_sensors()
{
    int num_sensors=0;

    dplist_node_t *list_node=dpl_get_first_reference(list);
    room_sensor_t *element=dpl_get_element_at_reference(list,list_node);

    while(list_node!=NULL)
    {
        if ((element->sensor_data.id)!=0)
            num_sensors++;

        list_node=dpl_get_next_reference(list,list_node);
        if(list_node!=NULL)
            element=dpl_get_element_at_reference(list,list_node);
    }
    return num_sensors;
}