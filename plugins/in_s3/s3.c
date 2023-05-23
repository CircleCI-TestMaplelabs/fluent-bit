
#include <stdlib.h>

#include <msgpack.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_config_map.h>
#include <fluent-bit/flb_error.h>
#include <fluent-bit/flb_aws_util.h>
#include <fluent-bit/flb_signv4.h>
#include <fluent-bit/flb_scheduler.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_pack.h>
#include "s3.h"
#include "utils.c"
#include <stdlib.h>
#include <string.h>



/* Configuration properties map */
static struct flb_config_map config_map[] = {
    {FLB_CONFIG_MAP_INT, "interval_sec", "1",
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, interval_sec),
     "polling interval."},
    {FLB_CONFIG_MAP_STR, "bucket", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, bucket_name),
     "bucket name."},
    {FLB_CONFIG_MAP_STR, "region", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, region),
     "region name."},
    {FLB_CONFIG_MAP_STR, "prefix", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, prefix),
     "prefix (folder in which objects reside)"},
    {FLB_CONFIG_MAP_TIME, "ignore_older", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, ignore_older),
     "ignore_older flag"},
    {FLB_CONFIG_MAP_TIME, "refresh_object_listing", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, refresh_object_listing),
     "refresh_object_listing flag"},
    {FLB_CONFIG_MAP_STR, "db", "s3-log-tracker.db",
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, db_prop_name),
     "Database file"},  
    {FLB_CONFIG_MAP_STR, "db.sync", "normal",
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, db_prop_sync_mode),
     "DB Sync mode"},     
     {FLB_CONFIG_MAP_STR, "files", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, files),
     "object/file name to be retrieved"},
     {FLB_CONFIG_MAP_STR, "exclude_files", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_s3_config, exclude_files),
     "object/file name to be exclude_files"},
    {0}};

static int  append_log_to_fluent_msgpack(struct flb_input_instance *ins, struct flb_in_s3_config *ctx, flb_sds_t log, flb_sds_t object) 
{
    int append_status = 1;
    msgpack_packer mp_pck;
    msgpack_sbuffer mp_sbuf;
    msgpack_sbuffer_init(&mp_sbuf);
    msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);

    msgpack_pack_array(&mp_pck, 2);
    flb_pack_time_now(&mp_pck);
    msgpack_pack_map(&mp_pck, 3);

    msgpack_pack_str(&mp_pck, 7);
    msgpack_pack_str_body(&mp_pck, DEFAULT_MESSAGE_FIELD_KEY, 7);
    msgpack_pack_str(&mp_pck, strlen(log));
    msgpack_pack_str_body(&mp_pck, log, strlen(log));

    msgpack_pack_str(&mp_pck, 6);
    msgpack_pack_str_body(&mp_pck, DEFAULT_BUCKET_FIELD_KEY, 6);
    msgpack_pack_str(&mp_pck, strlen(ctx -> bucket_name));
    msgpack_pack_str_body(&mp_pck, ctx -> bucket_name, strlen(ctx -> bucket_name));

    msgpack_pack_str(&mp_pck, 6);
    msgpack_pack_str_body(&mp_pck, DEFAULT_S3_OBJECT_FIELD_KEY, 6);
    msgpack_pack_str(&mp_pck, strlen(object));
    msgpack_pack_str_body(&mp_pck, object, strlen(object));
    
    
    

    append_status =  flb_input_chunk_append_raw(ins, NULL, 0, mp_sbuf.data, mp_sbuf.size);
    msgpack_sbuffer_destroy(&mp_sbuf);
    return append_status;
}

static int update_state_info_in_db (struct flb_in_s3_config *ctx, flb_sds_t key) {


    int current_offset =  hash_table_int_val_get(ctx -> file_with_consumed_offset, key);

    if (current_offset == -1) {
        return -1;
    }
    int object_size =  hash_table_int_val_get(ctx -> file_with_size, key);
    if (object_size == -1) {
        return -1;
    }

    int last_mod_time =  hash_table_int_val_get(ctx -> file_with_last_modified_time, key);
    if (last_mod_time == -1) {
        return -1;
    } 
    struct query_status qs = {0};
    memset(&qs, '\0', sizeof(qs));
    qs.populate_state_info = 0;
    char select_query[400];
    sprintf(select_query, SQL_GET_FOR_OBJECT_CURSOR, key);
    int ret = flb_sqldb_query(ctx->db,
                select_query, cb_db_function, &qs);

    // flb_plg_debug(ctx->ins,"reported rows in db %d", qs.rows);
    if (ret != FLB_ERROR) {
        if (qs.rows  == 0) {
            char insert_query[400];
            sprintf(insert_query, SQL_INSERT_CURSOR, key, object_size, current_offset, last_mod_time);
            ret = flb_sqldb_query(ctx->db,
                            insert_query, NULL, NULL);
            if (ret == FLB_ERROR) {
                flb_plg_error(ctx -> ins, "error inserting tracking info for object %s in db", key);
                hash_table_int_val_update(ctx -> file_with_pending_bytes_to_consume, key,  0);
                return -1;
            }
        } else {
            char update_query[400];
            sprintf(update_query, SQL_UPDATE_CURSOR, object_size, current_offset, last_mod_time, key);
            ret = flb_sqldb_query(ctx->db,
                            update_query, NULL, NULL);
            if (ret == FLB_ERROR) {
                flb_plg_error(ctx -> ins, "error updating tracking info for object %s in db", key);
                hash_table_int_val_update(ctx -> file_with_pending_bytes_to_consume, key,  0);
                return -1;
            }
        }
    }
    hash_table_int_val_update(ctx -> file_with_pending_bytes_to_consume, key,  0);
    return 0;
}


static void reset_size_and_last_mod_state_info(struct flb_in_s3_config *ctx, int index, flb_sds_t key) {

    struct entry_int_val* pending_byte_to_consume = ctx -> file_with_pending_bytes_to_consume->table[index];
    while (pending_byte_to_consume != NULL) {
        if (key) {
            if (strcmp(pending_byte_to_consume->key, key) == 0) {
                pending_byte_to_consume -> value = 0;

                int object_size =  hash_table_int_val_get(ctx -> file_with_size, pending_byte_to_consume->key);
                if (object_size != -1) {
                    int offset_val =  hash_table_int_val_get(ctx -> file_with_consumed_offset, pending_byte_to_consume->key);
                    if (offset_val != -1) {
                        hash_table_int_val_update(ctx -> file_with_size,  pending_byte_to_consume->key,  offset_val);
                    }
                   
                }
        
                int last_mod_time =  hash_table_int_val_get(ctx -> file_with_last_modified_time, pending_byte_to_consume->key);
                if (last_mod_time != -1) {
                    hash_table_int_val_update(ctx -> file_with_last_modified_time, pending_byte_to_consume->key , last_mod_time - 100);
                }
                int status = update_state_info_in_db(ctx, pending_byte_to_consume->key);
                if (status == -1) {
                    flb_plg_info(ctx -> ins,, "error updating state info");
                }
                pending_byte_to_consume -> value = 0;
                break;
            }
        } else {
            int object_size =  hash_table_int_val_get(ctx -> file_with_size, pending_byte_to_consume->key);
            if (object_size != -1) {
                int offset_val =  hash_table_int_val_get(ctx -> file_with_consumed_offset, pending_byte_to_consume->key);
                if (offset_val != -1) {
                    hash_table_int_val_update(ctx -> file_with_size,  pending_byte_to_consume->key,  offset_val);
                }
            }
    
            int last_mod_time =  hash_table_int_val_get(ctx -> file_with_last_modified_time, pending_byte_to_consume->key);
            if (last_mod_time != -1) {
                hash_table_int_val_update(ctx -> file_with_last_modified_time, pending_byte_to_consume->key , last_mod_time - 100);
            }
            int status = update_state_info_in_db(ctx, pending_byte_to_consume->key);
            if (status == -1) {
                flb_plg_info(ctx -> ins,, "error updating state info");
            }
            pending_byte_to_consume -> value = 0;
        }
        pending_byte_to_consume =pending_byte_to_consume -> next; 
    }
}

static int in_s3_collect(struct flb_input_instance *ins,
                            struct flb_config *config, void *in_context)
{
  
     /* Restricted by mem_buf_limit */
    if (flb_input_buf_paused(ins) == FLB_TRUE) {
        return FLB_SYSTEMD_BUSY;
    }
    struct flb_in_s3_config *ctx = in_context;
    
    flb_sds_t start_after_key = NULL;
    
   
    int current_time = (int)time(NULL);
    if (current_time > ctx -> scheduled_object_list_time) {
        while(true) {
            struct flb_http_client *c = NULL;
            flb_sds_t request_route = NULL;
            if (ctx -> prefix == NULL) {
                // Size Occupancy: 50 chars [(/?list-type=2&max-keys=) + (&start-after=)]
                request_route =  flb_malloc(50  * sizeof(char));
                if (request_route == NULL) {
                    flb_plg_error(ctx->ins, "request_route Memory flb_allocation failed");
                    flb_errno();
                    return -1;
                }
                sprintf(request_route, "/?list-type=2&max-keys=%d",MAX_KEYS_FOR_S3_LIST);
            } else {
                // Size Occupancy: 50 chars [(/?list-type=2&max-keys=) + (&start-after=) + len (prefix)]
                request_route =  flb_malloc((50 + strlen(ctx -> prefix))  * sizeof(char));
                if (request_route == NULL) {
                    flb_plg_error(ctx->ins, "request_route Memory flb_Allocation failed");
                    flb_errno();
                    return -1;
                }
                sprintf(request_route, "/?list-type=2&max-keys=%d&prefix=%s",MAX_KEYS_FOR_S3_LIST, ctx -> prefix);
            }

            if (start_after_key != NULL) {
                
            // Size Occupancy: 250 chars [size_of_request_route +  len(start_after_key)] ] 
                int size  = strlen(start_after_key) + 200;
                if (ctx -> prefix != NULL) {
                    size = 50 + strlen(ctx -> prefix);
                }   
                request_route =  flb_realloc(request_route, (size)  * sizeof(char));
                if (request_route == NULL) {
                    flb_plg_error(ctx->ins, "request_route Memory flb_reallocation failed");
                    if (start_after_key) {
                    flb_free(start_after_key);
                    start_after_key = NULL;
                    }
                    return -1;
                }
                strcat(request_route, "&start-after=");
                strcat(request_route, start_after_key);
            }
            flb_upstream_destroy(ctx->in_s3_client->upstream);
            ctx->in_s3_client->upstream = flb_upstream_create(config,
                                            ctx->endpoint, 443,
                                                FLB_IO_TLS, ctx->client_tls);
            ctx->in_s3_client->upstream->flags &= ~(FLB_IO_ASYNC);
            c = ctx->in_s3_client->client_vtable->request(ctx->in_s3_client, FLB_HTTP_GET,
                                                    request_route, NULL, 0,
                                                    NULL, 0);
        
            flb_free(request_route);
            request_route = NULL;

        
            if (c) {
                flb_plg_info(ctx->ins, "ListObject http status=%d", c->resp.status);
            } else {

                flb_plg_error(ctx->ins, "Unable to list s3 objects");
                if (start_after_key) {
                    flb_free(start_after_key);
                    start_after_key = NULL;
                }
                return 0;
            }
            
            if (start_after_key) {    
                flb_free(start_after_key);
                start_after_key = NULL;
            }
            if (c->resp.data_len == 0) {
                continue;
            }
            
            start_after_key = flb_update_xml_val_in_hash_table(ctx, c->resp.data, MAX_KEYS_FOR_S3_LIST);


            if (c) {
                flb_http_client_destroy(c);
            }
            
            if (start_after_key == NULL) {
                break;
            }
        }
        ctx -> scheduled_object_list_time = ((current_time/ctx -> refresh_object_listing) + 1) * ctx -> refresh_object_listing;
        flb_plg_info(ctx->ins,"Next scheduled object list time %d", ctx -> scheduled_object_list_time );

    } else {
        
        for (int i = 0; i < TABLE_SIZE ; i++) {
            struct entry_int_val* current = ctx -> file_with_last_modified_time->table[i];
            while (current != NULL) {
                flb_sds_t request_route =  flb_malloc((4 + strlen(current->key))  * sizeof(char));
                if (request_route == NULL) {
                    flb_plg_error(ctx->ins, "request_route Memory flb_Allocation failed");
                    flb_errno();
                    return -1;
                }
                sprintf(request_route, "/%s", current->key);
                int updated_size = -1;
                int updated_last_mod_time = -1;
                if (is_s3_object_modified(ctx, request_route, config, current->value, &updated_size, &updated_last_mod_time)) {
                    if ((updated_size != -1) && (updated_last_mod_time != -1)) {
                        int value = hash_table_int_val_get(ctx -> file_with_size, current->key);
                        if (value == -1) {
                            hash_table_int_val_insert(ctx -> file_with_size, current->key, updated_size);
                            hash_table_int_val_update(ctx -> file_with_pending_bytes_to_consume, current->key, updated_size);
                        } else if (updated_size - value > 0) {
                            hash_table_int_val_update(ctx -> file_with_size, current->key, updated_size);
                            hash_table_int_val_update(ctx -> file_with_pending_bytes_to_consume, current->key, updated_size-value);
                        } else {
                            hash_table_int_val_update(ctx -> file_with_pending_bytes_to_consume, current->key,0);
                        }
                        hash_table_int_val_update(ctx -> file_with_last_modified_time, current->key, updated_last_mod_time);
                    }
                }

                flb_free(request_route);
                current = current->next;
            }
        }
    }
  
    for (int i = 0; i < TABLE_SIZE ; i++) {
        if ((ctx -> file_with_pending_bytes_to_consume->table[i]!= NULL)) {
            flb_plg_debug(ctx->ins, "**file_with_pending_bytes_to_consume %s , %d",ctx -> file_with_pending_bytes_to_consume->table[i]->key, ctx -> file_with_pending_bytes_to_consume->table[i]->value);  
        }
        if ((ctx -> file_with_size->table[i]!= NULL)) {
            flb_plg_debug(ctx->ins, "**file_with_size %s , %d",ctx -> file_with_size->table[i]->key, ctx -> file_with_size->table[i]->value);  
        }
    }
    

    struct flb_aws_header *s3_headers = NULL;
    s3_headers = flb_malloc(sizeof(struct flb_aws_header) * 1);
    if (s3_headers == NULL) {
        flb_errno();
         flb_plg_error(ctx->ins, "s3_headers Memory flb_Allocation failed");
        return -1;
    }
    s3_headers -> key = "Range";
    s3_headers -> key_len = 5;    

    // int total_bytes_added_to_chunk = 0;
    for (int index = 0; index < TABLE_SIZE ; index++) {
        struct entry_int_val* current = ctx -> file_with_pending_bytes_to_consume->table[index];
        while (current != NULL) {
            if (current -> value ==0) {
                current = current->next;
                continue;
            }
            flb_plg_debug(ctx->ins, "file_with_pending_bytes_to_consume %s , %d",current->key, current->value);  

            // if (total_bytes_added_to_chunk > ctx -> max_bytes_to_append_to_chunk) {
            //     reset_size_and_last_mod_state_info(ctx, index, current -> key);
            //     current = current->next;
            //     continue;
            // }   

            flb_sds_t partial_message_in_prev_chunk = hash_table_string_val_get(ctx -> file_with_last_consumed_line, current->key);
            if (partial_message_in_prev_chunk) {
                flb_plg_debug(ctx->ins, "partial message in previous chunk %s", partial_message_in_prev_chunk) ;
            }
            
            // hash_table_string_val_inset(ctx -> file_with_last_consumed_line, key_attrib_temp_val, "");

            int source_os_for_line_breaks = unidentified;
            int num_chunks = current->value/BYTES_PER_CHUNK  + 1;
            flb_plg_debug(ctx -> ins, "num_chunks: %d, %d", num_chunks, current->value);
            int chunk_tracker, end_offset;
            int start_offset;
            
            struct entry_int_val* current_offset_table = ctx -> file_with_consumed_offset->table[index];
            while (current_offset_table != NULL) {
                if (strcmp(current_offset_table->key, current->key) == 0) {
                    start_offset = current_offset_table -> value;
                    break;
                }
                current_offset_table = current_offset_table->next;
            }
            
            int  pending_byte_offset = -1;
            
            for (chunk_tracker = 1; chunk_tracker <= num_chunks; chunk_tracker++) {
                
                
                // if (total_bytes_added_to_chunk > ctx -> max_bytes_to_append_to_chunk) {
                //     pending_byte_offset = start_offset;

                //     struct entry_int_val* follow_up_objects_in_same_hash = current->next;
                //     while (follow_up_objects_in_same_hash != NULL) {
                //         if (follow_up_objects_in_same_hash -> value!=0 ) {
                //             reset_size_and_last_mod_state_info(ctx, index, current -> key);
                //         }
                //         follow_up_objects_in_same_hash = follow_up_objects_in_same_hash-> next;
                //     }
                //     for (int iter = index + 1; iter < TABLE_SIZE ; iter++) {
                //         struct entry_int_val*  pending_bytes_to_consume_node= ctx -> file_with_pending_bytes_to_consume->table[iter];
                //         while (pending_bytes_to_consume_node != NULL) {
                //             if (pending_bytes_to_consume_node -> value !=0) {
                //                 reset_size_and_last_mod_state_info(ctx, iter, pending_bytes_to_consume_node -> key);
                //             }
                //             pending_bytes_to_consume_node = pending_bytes_to_consume_node->next;
                //         }
                //     }
                //     break;
                // }
            
                int offset_val =  hash_table_int_val_get(ctx -> file_with_consumed_offset, current->key);
                if (offset_val == -1) {
                    hash_table_int_val_insert(ctx -> file_with_consumed_offset, current->key, 0);
                    offset_val = 0;
                }

                if (chunk_tracker == num_chunks) {
                    
                    end_offset = offset_val + current->value;
                } else {

                    end_offset = offset_val + (chunk_tracker * BYTES_PER_CHUNK);
                }

                if (start_offset >  end_offset) {
                    break;
                }
            
                char rangeInBytes[200];
                sprintf(rangeInBytes,"bytes=%d-%d",start_offset,end_offset);

                s3_headers -> val = rangeInBytes;
                s3_headers -> val_len = strlen(rangeInBytes);
                
                flb_sds_t request_route =  flb_malloc((4 + strlen(current->key))  * sizeof(char));
                if (request_route == NULL) {
                    flb_plg_error(ctx->ins, "request_route Memory flb_Allocation failed");
                    flb_errno();
                    return -1;
                }
                sprintf(request_route, "/%s", current->key);

                struct flb_http_client *c = NULL;
                if (ctx->in_s3_client->upstream) {
                    flb_upstream_destroy(ctx->in_s3_client->upstream);
                }
                ctx->in_s3_client->upstream = flb_upstream_create(config,
                                                ctx->endpoint, 443,
                                                    FLB_IO_TLS, ctx->client_tls);
                ctx->in_s3_client->upstream->flags &= ~(FLB_IO_ASYNC);
                c = ctx->in_s3_client->client_vtable->request(ctx->in_s3_client, FLB_HTTP_GET,
                                                    request_route, NULL, 0,
                                                    s3_headers, 1);
                flb_free(request_route);
                request_route = NULL;

                if (c) {
                    flb_plg_info(ctx->ins, "get Object (bucket=%s, prefix=%s, byte_range=%d-%d) http status=%d", ctx -> bucket_name, current->key,start_offset,end_offset ,c->resp.status);
                    flb_plg_info(ctx->ins, "Content-length: %d, data len %d", c->resp.content_length, c -> resp.data_len);
                    

                    if (c -> resp.data_len > c->resp.content_length) {
                        int file_data_start_index =  c -> resp.data_len - c->resp.content_length;
                        int remaining_chars_iter;
                        for (remaining_chars_iter = 0; remaining_chars_iter < c -> resp.data_len - file_data_start_index; remaining_chars_iter++) {
                            c -> resp.data[remaining_chars_iter] =  c -> resp.data[remaining_chars_iter + file_data_start_index];
                        }
                        c -> resp.data[remaining_chars_iter] = '\0'; 
                    }
                    
                    flb_sds_t individual_message, last_message;
                    individual_message = strtok(c -> resp.data, NEW_LINE_DELIMITER);

                    if (source_os_for_line_breaks == unidentified) {
                        flb_sds_t first_message;
                        first_message = flb_malloc(strlen(individual_message)*sizeof(flb_sds_t ));
                        if (first_message == NULL) {
                            flb_plg_error(ctx->ins, "first_message Memory flb_Allocation failed");
                            flb_errno();
                            return -1;
                        }
                        strcpy(first_message, individual_message);
                        if (first_message[strlen(first_message)-1] == '\r') {
                            source_os_for_line_breaks = windows_os;
                        } else {
                            source_os_for_line_breaks = unix;
                        }
                        flb_free(first_message);
                        first_message = NULL;
                    }
                    flb_plg_debug(ctx->ins, "identified source_os_for_line_breaks %d", source_os_for_line_breaks);
                    int skip_line = 0;
                    if (partial_message_in_prev_chunk != NULL) 
                    {
                        flb_plg_debug(ctx->ins, "partial_message_in_prev_chunk: %s, len %d", partial_message_in_prev_chunk, strlen(partial_message_in_prev_chunk));
                        if (strlen(individual_message) > 0) {
                            if ((individual_message[0] == '\n') || (individual_message[0] == '\r') || (strcmp(individual_message, "\r") == 0)){
                            
                                int update_status =  append_log_to_fluent_msgpack(ins, ctx, partial_message_in_prev_chunk, current-> key);
                                if (update_status == -1){
                                    flb_plg_debug(ctx -> ins ,"unable to append partial record to msgpack");
                                    pending_byte_offset = start_offset;
                                    flb_http_client_destroy(c);
                                    goto chunk_state_update;
                                }
                            } else {
                                flb_sds_t merged_message = flb_malloc((strlen(partial_message_in_prev_chunk) + strlen(individual_message) + 1)  * sizeof(char));
                                if (merged_message == NULL) {
                                    flb_plg_error(ctx->ins, "merged_message Memory flb_reallocation failed");
                                    flb_http_client_destroy(c);
                                    return -1;
                                }
                                strcpy(merged_message, partial_message_in_prev_chunk);
                                strcat(merged_message, individual_message);
                                int update_status =  append_log_to_fluent_msgpack(ins, ctx, merged_message, current -> key);
                                if (update_status == -1){
                                    flb_plg_debug(ctx -> ins ,"unable to append merged record to msgpack");
                                    pending_byte_offset = start_offset;
                                    flb_http_client_destroy(c);
                                    flb_free(merged_message);
                                    goto chunk_state_update;
                                }
                                flb_plg_debug(ctx->ins, "merged partial_message_: %s, len %d", merged_message, strlen(merged_message));
                                flb_free(merged_message);
                                merged_message = NULL;
                                skip_line = -1;
                            }
                        }
                        flb_free(partial_message_in_prev_chunk);
                        partial_message_in_prev_chunk = NULL;
                    }                    

                    while (true) {
                        int message_index = individual_message - c -> resp.data;
                        last_message = individual_message;
                        
                        individual_message = strtok(NULL, NEW_LINE_DELIMITER);
                        if (individual_message == NULL) {
                            if ((strcmp(last_message,"\n") == 0) || (strcmp(last_message,"\r") == 0) || (strcmp(last_message,"") == 0)) {
                                break;
                            }
                            if (source_os_for_line_breaks == windows_os) {
                                    if (last_message[strlen(last_message)-1] != '\r'){
                                        partial_message_in_prev_chunk = flb_strdup(last_message);
                                        break;
                                    }
                            } else {
                                if (c -> resp.data[strlen(c -> resp.data)-1] != '\n') {
                                    partial_message_in_prev_chunk = flb_strdup(last_message);
                                    break;
                                }
                            }
                            // flb_plg_info(ctx->ins, "response PER MESSAGE: %s, LEN(%d)", last_message, strlen(last_message));    
                            int update_status = append_log_to_fluent_msgpack(ins, ctx, last_message, current->key);
                            if (update_status == -1){
                                flb_plg_debug(ctx -> ins ,"unable to append record to msgpack");
                                pending_byte_offset = start_offset + message_index;
                                flb_http_client_destroy(c);
                                goto chunk_state_update;
                            }
                            break;
                        }
                        if (skip_line == 0) {
                            flb_plg_info(ctx->ins, "response PER MESSAGE: %s, LEN(%d)", last_message, strlen(last_message));
                            int update_status = append_log_to_fluent_msgpack(ins, ctx, last_message, current->key);
                            if (update_status == -1){
                                flb_plg_debug(ctx -> ins ,"unable to append current record to msgpack");
                                pending_byte_offset = start_offset + message_index;
                                flb_http_client_destroy(c);
                                goto chunk_state_update;
                            }
                        } else {
                            flb_plg_info(ctx->ins, "response PER MESSAGE: %s, LEN(%d)", last_message, strlen(last_message)); 
                            skip_line = 0;
                        }
                    }

                
                    flb_http_client_destroy(c);

                } else {
                    flb_plg_error(ctx->ins, "Unable to get s3 object (bucket=%s, prefix=%s)", ctx -> bucket_name, current->key);
                    initialize_s3_client(ctx, config);  
                    return -1;
                }

                // total_bytes_added_to_chunk = total_bytes_added_to_chunk + end_offset - start_offset + 1;
                start_offset = end_offset+1;
                
            }

            chunk_state_update:

                if (partial_message_in_prev_chunk) {
                    flb_sds_t partial_message_stored= hash_table_string_val_get(ctx -> file_with_last_consumed_line, current->key);
                    if (partial_message_stored) {
                        hash_table_string_val_update(ctx -> file_with_last_consumed_line,  current->key, partial_message_in_prev_chunk);
                    } else {
                        hash_table_string_val_insert(ctx -> file_with_last_consumed_line,  current->key, partial_message_in_prev_chunk);
                    }
                    flb_free(partial_message_stored);
                    flb_free(partial_message_in_prev_chunk);
                }
                if (pending_byte_offset != -1) {
                    current->value = 0;
                    int object_size =  hash_table_int_val_get(ctx -> file_with_size, current->key);
                    if (object_size != -1) {
                        hash_table_int_val_update(ctx -> file_with_size,  current->key, pending_byte_offset);
                    }
                    int offset =  hash_table_int_val_get(ctx -> file_with_consumed_offset, current->key);
                    if (offset != -1) {
                        hash_table_int_val_update(ctx -> file_with_consumed_offset,  current->key, pending_byte_offset);
                    }
                    int last_mod_time =  hash_table_int_val_get(ctx -> file_with_last_modified_time, current->key);
                    if (last_mod_time != -1) {
                        hash_table_int_val_update(ctx -> file_with_last_modified_time,  current->key, last_mod_time - 100);
                    }
                    struct entry_int_val* follow_up_objects_in_same_hash = current->next;
                    while (follow_up_objects_in_same_hash != NULL) {
                        if (follow_up_objects_in_same_hash -> value!=0 ) {
                            reset_size_and_last_mod_state_info(ctx, index, follow_up_objects_in_same_hash -> key);
                        }
                        follow_up_objects_in_same_hash = follow_up_objects_in_same_hash-> next;
                    }
                    for (int iter = index + 1; iter < TABLE_SIZE ; iter++) {
                        struct entry_int_val* follow_up_table_entry = ctx -> file_with_pending_bytes_to_consume->table[iter];
                        while (follow_up_table_entry != NULL) {
                            if (follow_up_table_entry -> value !=0) {
                                reset_size_and_last_mod_state_info(ctx, iter, follow_up_table_entry->key);
                            }
                            follow_up_table_entry = follow_up_table_entry->next;
                        }
                    }
                    int status = update_state_info_in_db(ctx, current->key);
                    if (status == -1) {
                        flb_plg_info(ctx -> ins,, "error updating state info in db");
                    }
                    flb_free(s3_headers);
                    s3_headers = NULL;  
                    return 0;
                }


            int pending_bytes_to_consume =  hash_table_int_val_get(ctx -> file_with_pending_bytes_to_consume, current->key);
            if (pending_bytes_to_consume == -1) {
                pending_bytes_to_consume = 0;
            }
            int offset =  hash_table_int_val_get(ctx -> file_with_consumed_offset, current->key);
        

            if (offset == -1) {
                offset = 0;
            }
            
            hash_table_int_val_update(ctx -> file_with_consumed_offset, current->key,  offset + pending_bytes_to_consume);

            int status = update_state_info_in_db(ctx, current->key);
            if (status == -1) {
                flb_plg_info(ctx -> ins,, "error updating state info in db");
            }
            
            if (pending_byte_offset != -1) {
                flb_free(s3_headers);
                s3_headers = NULL;
                return 0;
            }

            current = current->next;
        } 
    }
        

    flb_free(s3_headers);
    s3_headers = NULL;
    return 0;
}


static int in_s3_init(struct flb_input_instance *in,
                         struct flb_config *config, void *data)
{
    int ret = -1;
    struct flb_in_s3_config *ctx = NULL;

    /* Allocate space for the configuration */
    ctx = flb_calloc(1,sizeof(struct flb_in_s3_config));
    if (!ctx)
    {
        return -1;
    }


    /* Initialize head config */
    ret = flb_input_config_map_set(in, (void *)ctx);
    if (ret == -1)
    {
        return -1;
    }
    ctx->ins = in;

    flb_plg_info(ctx->ins, "bucket_name=%s prefix=%s files=%s region=%s interval=%d ignore_older=%d",
                 ctx->bucket_name, ctx->prefix, ctx->files, ctx->region, ctx->interval_sec, ctx -> ignore_older);
    if (!ctx->bucket_name)
    {
        flb_plg_error(ctx->ins, "bucket name is not configured");
        return -1;
    }
    if (!ctx->region)
    {
        flb_plg_error(ctx->ins, "region is not configured");
        return -1;
    }
    if (!ctx->files)
    {
        flb_plg_error(ctx->ins, "List of S3 files for consumption is not configured");
        return -1;
    }

    if (!ctx -> db_prop_name) 
    {
        flb_plg_error(ctx->ins, "'db' tracker file is not configured");
        return -1;
    }
    
    if (!ctx -> db_prop_sync_mode) 
    {
        flb_plg_error(ctx->ins, "'db.sync' field is not configured");
        return -1;
    }
   
    // Size Occupancy: 25 chars (_.s3._.amazonaws.com) + len(bucket) + len (region)
    ctx->endpoint = flb_malloc((25+flb_sds_len(ctx-> bucket_name)+flb_sds_len(ctx -> region)) * sizeof(char));
    if (ctx->endpoint == NULL) {
        flb_plg_error(ctx->ins, "endpoint Memory flb_Allocation failed");
        flb_errno();
        return -1;
    }
    sprintf(ctx->endpoint, "%s.s3.%s.amazonaws.com", ctx-> bucket_name, ctx -> region);
    
    if (initialize_s3_client(ctx, config) != 1) {
        return -1;
    }

    if (ctx -> file_with_consumed_offset == NULL) {
        ctx -> file_with_consumed_offset = flb_calloc(1,sizeof(struct hash_table_int));
    }
    if (ctx -> file_with_last_modified_time == NULL) {
        ctx -> file_with_last_modified_time = flb_calloc(1,sizeof(struct hash_table_int));
    }
    if (ctx -> file_with_size == NULL) {
        ctx -> file_with_size = flb_calloc(1,sizeof(struct hash_table_int));
    }
    if (ctx -> file_with_pending_bytes_to_consume == NULL) {
         ctx -> file_with_pending_bytes_to_consume = flb_calloc(1,sizeof(struct hash_table_int));
    }
    if (ctx -> file_with_last_consumed_line == NULL) {
        ctx -> file_with_last_consumed_line = flb_calloc(1,sizeof(struct hash_table_string));
    }
    if (ctx -> desired_files_for_monitor == NULL) {
        ctx -> desired_files_for_monitor = flb_calloc(1,sizeof(struct hash_table_int));
    }
    if (ctx -> exclusion_files == NULL) {
        ctx -> exclusion_files = flb_calloc(1,sizeof(struct hash_table_int));
    }



    if ((ctx -> files != NULL))  {
        flb_sds_t individual_file;
        flb_sds_t delim = ",";

        individual_file = strtok(ctx -> files, delim);

        while (individual_file != NULL) {
            flb_sds_t desired_key_match_pattern;
            if (ctx -> prefix != NULL) {
                desired_key_match_pattern = flb_malloc((strlen(individual_file) + strlen(ctx->prefix) + 2) * sizeof(char));
                if (desired_key_match_pattern == NULL) {
                    flb_plg_error(ctx->ins, "desired_key_match_pattern flb_Allocation failed");
                    flb_errno();
                    return -1;
                }
                sprintf(desired_key_match_pattern, "%s%s", ctx -> prefix, individual_file);
                hash_table_int_val_insert(ctx -> desired_files_for_monitor, desired_key_match_pattern, -1);
            } else {
                desired_key_match_pattern = flb_malloc((strlen(individual_file) + 1) * sizeof(char));
                if (desired_key_match_pattern == NULL) {
                    flb_plg_error(ctx->ins, "desired_key_match_pattern flb_Allocation failed");
                    flb_errno();
                    return -1;
                }
                strcpy(desired_key_match_pattern, individual_file);
                hash_table_int_val_insert(ctx -> desired_files_for_monitor, desired_key_match_pattern, -1);
            }
            flb_free(desired_key_match_pattern);
            desired_key_match_pattern = NULL;
            individual_file = strtok(NULL, delim);
        }
    }

    if ((ctx -> exclude_files != NULL))  {
        flb_sds_t individual_file;
        flb_sds_t delim = ",";

        individual_file = strtok(ctx -> exclude_files, delim);

        while (individual_file != NULL) {
            flb_sds_t desired_key_match_pattern;
            if (ctx -> prefix != NULL) {
                desired_key_match_pattern = flb_malloc((strlen(individual_file) + strlen(ctx->prefix) + 2) * sizeof(char));
                if (desired_key_match_pattern == NULL) {
                    flb_plg_error(ctx->ins, "desired_key_match_pattern flb_Allocation failed");
                    flb_errno();
                    return -1;
                }
                sprintf(desired_key_match_pattern, "%s%s", ctx -> prefix, individual_file);
                hash_table_int_val_insert(ctx -> exclusion_files, desired_key_match_pattern, -1);
            } else {
                desired_key_match_pattern = flb_malloc((strlen(individual_file) + 1) * sizeof(char));
                if (desired_key_match_pattern == NULL) {
                    flb_plg_error(ctx->ins, "desired_key_match_pattern flb_Allocation failed");
                    flb_errno();
                    return -1;
                }
                strcpy(desired_key_match_pattern, individual_file);
                hash_table_int_val_insert(ctx -> exclusion_files, desired_key_match_pattern, -1);
            }
            flb_free(desired_key_match_pattern);
            desired_key_match_pattern = NULL;
            individual_file = strtok(NULL, delim);
        }
    }

    for (int i = 0; i < TABLE_SIZE ; i++) {
        if (ctx -> desired_files_for_monitor->table[i]!= NULL){
            flb_plg_debug(ctx->ins, "desired_file_set_for_monitoring: %s",ctx -> desired_files_for_monitor->table[i]->key);
        }
    }

   


    if (strcasecmp(ctx -> db_prop_sync_mode, "extra") == 0) {
        ctx->db_sync = 3;
    }
    else if (strcasecmp(ctx -> db_prop_sync_mode, "full") == 0) {
        ctx->db_sync = 2;
        }
    else if (strcasecmp(ctx -> db_prop_sync_mode, "normal") == 0) {
        ctx->db_sync = 1;
    }
    else if (strcasecmp(ctx -> db_prop_sync_mode, "off") == 0) {
        ctx->db_sync = 0;
    }
    else {
        flb_plg_error(ctx->ins, "invalid database 'db.sync' value");
    }
    
    flb_input_set_context(in, ctx);

    ctx->db = flb_s3_db_open(ctx->db_prop_name, in, ctx, config);
    if (!ctx->db) {
        flb_plg_error(ctx->ins, "could not open/create database '%s'", ctx->db_prop_name);
    }


    struct query_status qs = {0};
    memset(&qs, '\0', sizeof(qs));

    qs.file_with_consumed_offset = ctx -> file_with_consumed_offset;
    qs.file_with_size = ctx -> file_with_size;
    qs.file_with_last_modified_time = ctx -> file_with_last_modified_time;
    qs.populate_state_info = 1;
    qs.ignore_older = ctx -> ignore_older;

    int ret_status = flb_sqldb_query(ctx->db,
                    SQL_GET_ALL_CURSOR, cb_db_function, &qs);
    if (ret_status != FLB_OK) {
        flb_plg_error(ctx -> ins, "couldn't get s3 object state info from db");
    }
    // ctx->start_key = NULL;
    flb_input_set_context(in, ctx);


    ctx -> scheduled_object_list_time = ((int)time(NULL)/ctx -> scheduled_object_list_time) * ctx -> scheduled_object_list_time;
   
    // ctx -> max_bytes_to_append_to_chunk = (int)((float) in -> mem_buf_limit * ((float)MAX_CHUNK_SIZE_BUFFER_PERCENT / (float)100));
    
    // flb_plg_debug(ctx->ins,"mem buf limit in bytes: %d", ctx -> max_bytes_to_append_to_chunk );
    ret = flb_input_set_collector_time(in,
                                       in_s3_collect,
                                       ctx->interval_sec,
                                       0, config);
    if (ret < 0)
    {
        flb_plg_error(ctx->ins, "could not set collector for s3 input plugin");
        flb_free(ctx);
        return -1;
    }
    return 0;
}

static int in_s3_exit(void *data, struct flb_config *config)
{
    (void)*config;
    struct flb_in_s3_config *ctx = data;
    if (ctx->endpoint)
    {
        flb_free(ctx->endpoint);
    }
    if (ctx->client_tls) {
        flb_tls_destroy(ctx->client_tls);
    }
    if (ctx->cred_tls) {
        flb_tls_destroy(ctx->cred_tls);
    }
    if (ctx->provider) {
        flb_aws_provider_destroy(ctx->provider);
    }
    if (ctx->in_s3_client) {
        flb_aws_client_destroy(ctx->in_s3_client);
    }
    if (ctx->file_with_consumed_offset) {
        hash_table_int_free(ctx->file_with_consumed_offset);
    }
    if (ctx->file_with_pending_bytes_to_consume) {
        hash_table_int_free(ctx->file_with_pending_bytes_to_consume);
    }
    if (ctx->file_with_size) {
        hash_table_int_free(ctx->file_with_size);
    }
    if (ctx->file_with_last_modified_time) {
        hash_table_int_free(ctx->file_with_last_modified_time);
    }
    if (ctx->desired_files_for_monitor) {
        hash_table_int_free(ctx->desired_files_for_monitor);
    }
     if (ctx->exclusion_files) {
        hash_table_int_free(ctx->exclusion_files);
    }
    if (ctx->file_with_last_consumed_line) {
        hash_table_string_free(ctx->file_with_last_consumed_line);
    }
    if (ctx->db) {
        flb_s3_db_close(ctx->db);
    }
    if (ctx) {
        flb_free(ctx);
    }

    return 0;
}


struct flb_input_plugin in_s3_plugin = {
    .name         = "s3",
    .description  = "s3 input plugin",
    .cb_init      = in_s3_init,
    .cb_pre_run   = NULL,
    .cb_flush_buf = NULL,
    .cb_collect   = in_s3_collect,
    .config_map   = config_map,
    .cb_exit      = in_s3_exit
};

