#ifndef FLB_IN_S3_H
#define FLB_IN_S3_H

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_aws_credentials.h>
#include <fluent-bit/flb_aws_util.h>
#include <fluent-bit/flb_sqldb.h>

#define MAX_KEYS_FOR_S3_LIST 30
#define BYTES_PER_CHUNK 10000
#define MAX_CHUNK_SIZE_BUFFER_PERCENT 80
#define NEW_LINE_DELIMITER "\n"
#define DEFAULT_MESSAGE_FIELD_KEY "message"
#define DEFAULT_BUCKET_FIELD_KEY "bucket"
#define DEFAULT_S3_OBJECT_FIELD_KEY "object"

#define LINE_BREAK_WINDOWS "\r\n"
#define LINE_BREAK_UNIX_AND_MAC "\r"
#define CONTENT_RANGE_HEADER_CUSTOM "Content-Range: bytes 1-100/"
#define LAST_MODIFIED_HEADER_CUSTOM "Last-Modified: "
#define FLB_SYSTEMD_BUSY   3
#define MAX_RETRIES 3
#define MAX_RETRIES_ON_INCORRECT_REPORT 1

enum source_machine_based_on_line_breaks {
    unidentified,
    linux_os,
    windows_os  
};

struct flb_in_s3_config {
     /* Config properties */
    int interval_sec;
    
    flb_sds_t bucket_name;

    flb_sds_t prefix;

    flb_sds_t endpoint;

    flb_sds_t files;
    flb_sds_t exclude_files;

    int ignore_older;
    
    int refresh_object_listing;
    int scheduled_object_list_time;

    flb_sds_t region;

    flb_sds_t db_prop_name;
    flb_sds_t db_prop_sync_mode;

    struct flb_sqldb *db;
    sqlite3_stmt *stmt_cursor;
    int db_sync;

    struct flb_tls *cred_tls;
    struct flb_tls *client_tls;

    struct hash_table_int *file_with_consumed_offset;
    struct hash_table_int *file_with_pending_bytes_to_consume;
    struct hash_table_int *file_with_size;
    struct hash_table_int *file_with_last_modified_time;
    struct hash_table_string *file_with_last_consumed_line;

    struct hash_table_int *desired_files_for_monitor;
    struct hash_table_int *exclusion_files;

    struct flb_aws_client *in_s3_client;
    struct flb_aws_provider *provider;

    
    // struct flb_tls *tls_context;

    struct flb_input_instance *ins;
};

#endif