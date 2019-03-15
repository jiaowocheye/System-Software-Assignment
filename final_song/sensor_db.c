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
#include "sensor_db.h"

#define SBUFFER_FAILURE -1
#define SBUFFER_SUCCESS 0
#define SBUFFER_NO_DATA 1
#define SBUFFER_NO_OTHER_READER 2

//reference: zetcode.com/db/sqlitec/

/*
 * Make a connection to the database server
 * Create (open) a database with name DB_NAME having 1 table named TABLE_NAME
 * If the table existed, clear up the existing data if clear_up_flag is set to 1
 * Return the connection for success, NULL if an error occurs
 */

int exec_callback(DBCONN *conn, char *query, callback_t f)
{
    char *err_msg=0;
    int rc=sqlite3_exec(conn, query, f, 0, &err_msg);

    if(rc!=SQLITE_OK)
    {
        fprintf(stderr, "Query failed: query=%s, error message=%s\n", query, err_msg);
        sqlite3_free(err_msg);
        return 1;
    }

    printf("Query=%s executed successfully\n", query);
    return 0;
}

/*
 * Reads continiously all data from the shared buffer data structure and stores this into the database
 * When *buffer becomes NULL the method finishes. This method will NOT automatically disconnect from the db
 */
void storagemgr_parse_sensor_data(DBCONN * conn, sbuffer_t ** buffer)
{
    //printf("entered storagemgr\n");
    sensor_data_t data;
    int result=sbuffer_remove(*buffer, &data);
    
    if(result==SBUFFER_FAILURE)
    {
        printf("buffer invalid");
    }
    
    if(result==SBUFFER_SUCCESS)
    {
        //int feedback=
        //printf("insert sensor into database\n");
        insert_sensor(conn, data.id, data.value, data.ts);
    }
    
}

DBCONN * init_connection(char clear_up_flag) 
{
    DBCONN *db;
    char *err_msg=0;
    char *log;

    int rc=sqlite3_open(TO_STRING(DB_NAME), &db);

    if(rc!=SQLITE_OK)
    {
        asprintf(&log, "Can not connect to database\n");
        fifo_write(log);
        free(log);
        fprintf(stderr, "Can not connect to database: %s\n", sqlite3_errmsg(db));
        sqlite3_free(err_msg);
        return NULL;
    } else
    {
        asprintf(&log, "SQL connection established\n");
        fifo_write(log);
         free(log);
    }

    if(clear_up_flag==1)
    {
        char *sql_drop_table="DROP TABLE IF EXISTS "TO_STRING(TABLE_NAME);
        
        rc=sqlite3_exec(db, sql_drop_table, 0, 0, &err_msg);
        if(rc!=SQLITE_OK)
        {
            fprintf(stderr, "Drop table failed: %s\n", err_msg);
            sqlite3_free(err_msg);
            asprintf(&log, "Drop table failed\n");
            fifo_write(log);
            free(log);
            return NULL;
        }
    }
    
    
    char *sql_create_table="CREATE TABLE IF NOT EXISTS "TO_STRING(TABLE_NAME)" (id INTEGER PRIMARY KEY, sensor_id INT, sensor_value DECIMAL(4,2), timestamp TIMESTAMP)";
    rc=sqlite3_exec(db, sql_create_table, 0, 0, &err_msg);
    
    if(rc!=SQLITE_OK)
    {
        fprintf(stderr, "Create table failed: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else
    {
        asprintf(&log, "Table created successfully or already exists\n");
        fifo_write(log);
         free(log);
    }
    return db;
}

/*
 * Disconnect from the database server
 */
void disconnect(DBCONN *conn)
{
    char *log;
    
    sqlite3_close(conn);
    asprintf(&log, "Connection to SQL server lost\n");
    fifo_write(log);
    free(log);
}


/*
 * Write an INSERT query to insert a single sensor measurement
 * Return zero for success, and non-zero if an error occurs
 */
int insert_sensor(DBCONN * conn, sensor_id_t id, sensor_value_t value, sensor_ts_t ts)
{
    //printf("into insert_sensor\n");
    sqlite3_stmt *res;
    int rc=0;

    char *sql_insert="INSERT INTO "TO_STRING(TABLE_NAME)" (sensor_id, sensor_value, timestamp) VALUES (?,?,?)";
    rc=sqlite3_prepare_v2(conn,sql_insert, -1, &res, 0);

    if(rc!=SQLITE_OK)
    {
        fprintf(stderr, "Statement preparation failed: %s\n", sqlite3_errmsg(conn));
        return 1;
    }

    sqlite3_bind_int(res, 1, id);
    sqlite3_bind_double(res, 2, value);
    sqlite3_bind_int(res, 3, ts);

    sqlite3_step(res);

    sqlite3_finalize(res);
    return 0;
}


/*
 * Write an INSERT query to insert all sensor measurements available in the file 'sensor_data'
 * Return zero for success, and non-zero if an error occurs
 */
int insert_sensor_from_file(DBCONN * conn, FILE * sensor_data)
{
    sensor_id_t id;
    sensor_value_t value;
    sensor_ts_t ts;

    while(!feof(sensor_data))
    {
        fread(&id, sizeof(sensor_id_t), 1, sensor_data);
        fread(&value, sizeof(sensor_value_t), 1, sensor_data);
        if(fread(&ts, sizeof(sensor_ts_t), 1, sensor_data)>0)
        {
            insert_sensor(conn, id, value, ts);
        } //check if it's the last row...
    }

    return 0;

}


/*
  * Write a SELECT query to select all sensor measurements in the table
  * The callback function is applied to every row in the result
  * Return zero for success, and non-zero if an error occurs
  */
int find_sensor_all(DBCONN * conn, callback_t f)
{
    char *sql_find_all="SELECT * FROM "TO_STRING(TABLE_NAME);
    int rc=exec_callback(conn, sql_find_all, f);

    if(rc!=0)
    {
        printf("find_sensor_all failed\n");
        return 1;
    }

    return 0;
}


/*
 * Write a SELECT query to return all sensor measurements having a temperature of 'value'
 * The callback function is applied to every row in the result
 * Return zero for success, and non-zero if an error occurs
 */
int find_sensor_by_value(DBCONN * conn, sensor_value_t value, callback_t f)
{
    char *sql="SELECT * FROM "TO_STRING(TABLE_NAME)" WHERE sensor_value=";
    char *sql_find_by_value;

    asprintf(&sql_find_by_value,"%s%lf",sql,value);

    int rc=exec_callback(conn, sql_find_by_value, f);

    if(rc!=0)
    {
        printf("find_sensor_by_value failed\n");
        return 1;
    }
    free(sql_find_by_value);
    return 0;
}


/*
 * Write a SELECT query to return all sensor measurements of which the temperature exceeds 'value'
 * The callback function is applied to every row in the result
 * Return zero for success, and non-zero if an error occurs
 */
int find_sensor_exceed_value(DBCONN * conn, sensor_value_t value, callback_t f)
{
    char *sql="SELECT * FROM "TO_STRING(TABLE_NAME)" WHERE sensor_value>";
    char *sql_find_exceed_value;

    asprintf(&sql_find_exceed_value,"%s%lf",sql,value);

    int rc=exec_callback(conn, sql_find_exceed_value, f);

    if(rc!=0)
    {
        printf("find_sensor_exceed_value failed\n");
        return 1;
    }
    free(sql_find_exceed_value);
    return 0;
}


/*
 * Write a SELECT query to return all sensor measurements having a timestamp 'ts'
 * The callback function is applied to every row in the result
 * Return zero for success, and non-zero if an error occurs
 */
int find_sensor_by_timestamp(DBCONN * conn, sensor_ts_t ts, callback_t f)
{
    char *sql="SELECT * FROM "TO_STRING(TABLE_NAME)" WHERE timestamp=";
    char *sql_find_by_timestamp;

    asprintf(&sql_find_by_timestamp,"%s%ld",sql,ts);

    int rc=exec_callback(conn, sql_find_by_timestamp, f);

    if(rc!=0)
    {
        printf("find_sensor_by_timestamp failed\n");
        return 1;
    }
    free(sql_find_by_timestamp);
    return 0;
}


/*
 * Write a SELECT query to return all sensor measurements recorded after timestamp 'ts'
 * The callback function is applied to every row in the result
 * return zero for success, and non-zero if an error occurs
 */
int find_sensor_after_timestamp(DBCONN * conn, sensor_ts_t ts, callback_t f)
{
    char *sql="SELECT * FROM "TO_STRING(TABLE_NAME)" WHERE timestamp>";
    char *sql_find_after_timestamp;

    asprintf(&sql_find_after_timestamp,"%s%ld",sql,ts);

    int rc=exec_callback(conn, sql_find_after_timestamp, f);

    if(rc!=0)
    {
        printf("find_sensor_by_value failed\n");
        return 1;
    }
    free(sql_find_after_timestamp);
    return 0;
}







