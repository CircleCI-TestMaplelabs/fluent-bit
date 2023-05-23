    
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <time.h>
#include <fluent-bit/flb_strptime.h>

#define TABLE_SIZE 1000
#define _GNU_SOURCE
#include <fluent-bit/flb_parser.h>

#define SQL_CREATE_CURSOR                                   \
    "CREATE TABLE IF NOT EXISTS in_s3 ("                    \
    "  object  TEXT,"                              \
    "  size INTEGER,"                                     \
    "  consumed_offset INTEGER,"                                     \
    "  last_modified_time INTEGER"                                     \
    ");"



#define SQL_INSERT_CURSOR                               \
    "INSERT INTO in_s3 (object, size, consumed_offset, last_modified_time)"   \
    "  VALUES ('%s', %d, %d, %d);"

#define SQL_UPDATE_CURSOR                      \
    "UPDATE in_s3 set size='%d',consumed_offset='%d',last_modified_time='%d' where object='%s';"

#define SQL_GET_ALL_CURSOR \
    "SELECT * FROM in_s3;"

#define SQL_GET_FOR_OBJECT_CURSOR \
    "SELECT * FROM in_s3 where object='%s';"


#define SQL_PRAGMA_SYNC                         \
    "PRAGMA synchronous=%i;"

struct query_status {
    int rows;
    flb_sds_t object; 
    int size; 
    int consumed_offset;
    int last_modified_time;
    int populate_state_info;
    int ignore_older;
    struct hash_table_int *file_with_consumed_offset;
    struct hash_table_int *file_with_size;
    struct hash_table_int *file_with_last_modified_time;
};

struct entry_int_val {
    flb_sds_t key;
    int value;
    struct entry_int_val* next;
};

struct hash_table_int {
    struct entry_int_val* table[TABLE_SIZE];
};

struct entry_string_val {
    flb_sds_t key;
    flb_sds_t value;
    struct entry_string_val* next;
};

struct hash_table_string {
    struct entry_string_val* table[TABLE_SIZE];
};

static unsigned int hash(const flb_sds_t key) {
    unsigned int hashval = 0;
    for (int i = 0; key[i] != '\0'; i++) {
        hashval = key[i] + (hashval << 5) - hashval;
    }
    return hashval % TABLE_SIZE;
}

static void hash_table_int_val_insert(struct hash_table_int* ht, const flb_sds_t key, int value) {

    unsigned int index = hash(key);
    struct entry_int_val* new_entry = (struct entry_int_val*) malloc(sizeof(struct entry_int_val));
    new_entry->key = flb_strdup(key);
    new_entry->value = value;
    new_entry->next = NULL;
    if (ht->table[index] == NULL) {
        ht->table[index] = new_entry;
    } else {
        struct entry_int_val* current = ht->table[index];
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_entry;
    }
}

static void hash_table_int_val_update(struct hash_table_int* ht, const flb_sds_t key, int value) {

    unsigned int index = hash(key);

    
    if (ht->table[index] != NULL) {
        struct entry_int_val* current = ht->table[index];
        while (current != NULL) {
            if (strcmp(current->key, key) == 0) {
                current->value = value;
                return;
            }
            current = current->next;
        }
    }
}

static int hash_table_int_val_get(struct hash_table_int* ht, const flb_sds_t key) {
    unsigned int index = hash(key);
    if (ht->table == NULL) {
       return -1;
    }
    struct entry_int_val* current = ht->table[index];
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            return current->value;
        }
        current = current->next;
    }
    return -1;
}

static int hash_table_int_val_delete(struct hash_table_int* ht, const flb_sds_t key) {
    unsigned int index = hash(key);
    struct entry_int_val* current = ht->table[index];
    struct entry_int_val* previous = NULL;

    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            if (previous == NULL) {
                ht->table[index] = current->next;
            } else {
                previous->next = current->next;
            }
            flb_free(current->key);
            current->key = NULL;
            flb_free(current);
            current = NULL;
            return 0;
        }
        previous = current;
        current = current->next;
    }
    return -1;
}

static void hash_table_int_free(struct hash_table_int* ht) {
    if (ht) {
        for (int i = 0; i < TABLE_SIZE ; i++) {
            struct entry_int_val* current = ht->table[i];
            struct entry_int_val* previous = NULL;
            while (current != NULL) {
                flb_free(current->key);
                current->key = NULL;
                previous = current;
                current = current->next;
                if (previous) {
                    flb_free(previous);
                    previous = NULL;
                }
            }
            // if (ht->table[i]){
            //     flb_free(ht->table[i]);
            //     ht->table[i] = NULL;
            // }
        }
        flb_free(ht);
        ht = NULL;
    }
   
}

static void hash_table_string_val_insert(struct hash_table_string* ht, const flb_sds_t key, flb_sds_t value) {

    unsigned int index = hash(key);
    struct entry_string_val* new_entry = (struct entry_string_val*) malloc(sizeof(struct entry_string_val));
    new_entry->key = flb_strdup(key);
    new_entry->value = flb_strdup(value);
    new_entry->next = NULL;
    if (ht->table[index] == NULL) {
        ht->table[index] = new_entry;
    } else {
        struct entry_string_val* current = ht->table[index];
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_entry;
    }
}

static void hash_table_string_val_update(struct hash_table_string* ht, const flb_sds_t key, flb_sds_t value) {

    unsigned int index = hash(key);

    if (ht->table[index] != NULL) {
        struct entry_string_val* current = ht->table[index];
        while (current != NULL) {
            if (strcmp(current->key, key) == 0) {
                if (current->value) {
                    flb_free(current->value);
                }
                current->value = flb_strdup(value);
                return;
            }
            current = current->next;
        }
    }
}

static flb_sds_t hash_table_string_val_get(struct hash_table_string* ht, const flb_sds_t key) {
    unsigned int index = hash(key);
    if (ht->table == NULL) {
       return NULL;
    }
    struct entry_string_val* current = ht->table[index];
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            return flb_strdup(current->value);
        }
        current = current->next;
    }
    return NULL;
}

static void hash_table_string_free(struct hash_table_string* ht) {
    if (ht) {
        for (int i = 0; i < TABLE_SIZE ; i++) {
            struct entry_string_val* current = ht->table[i];
            struct entry_string_val* previous = NULL;
            while (current != NULL) {
                flb_free(current->key);
                current->key = NULL;
                flb_free(current->value);
                current->value = NULL;
                previous = current;
                current = current->next;
                if (previous) {
                    flb_free(previous);
                    previous = NULL;
                }
            }
            // if (ht->table[i]){
            //     flb_free(ht->table[i]);
            //     ht->table[i] = NULL;
            // }
        }
        flb_free(ht);
        ht = NULL;
    }
    
}

static int hash_table_string_val_delete(struct hash_table_string* ht, const flb_sds_t key) {
    unsigned int index = hash(key);
    struct entry_string_val* current = ht->table[index];
    struct entry_string_val* previous = NULL;

    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            if (previous == NULL) {
                ht->table[index] = current->next;
            } else {
                previous->next = current->next;
            }
            if (current->key) {
                flb_free(current->key);
                current->key = NULL;
            }
            if (current->value) {
                flb_free(current->value);
                current->value = NULL;
            }
            flb_free(current);
            current = NULL;
            return 0;
        }
        previous = current;
        current = current->next;
    }
    return -1;
}


static int convert_to_epoch(const char* dateTimeStr) {
    struct tm tm_time;

    flb_strptime(dateTimeStr, "%Y-%m-%dT%H:%M:%S.%LZ", &tm_time);
    time_t time_lookup = timegm(&tm_time);

    return (int)time_lookup;
}

static flb_sds_t epoch_to_custom_s3_time_pattern(int epoch_time) {
    time_t epochTime = epoch_time;
    struct tm* timeInfo;
    char buffer[80];
    timeInfo = localtime(&epochTime);
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", timeInfo);
    return flb_strdup(buffer);
}

static int custom_s3_pattern_to_epoch(const char* dateTimeStr) {
    struct tm tm_time;
    flb_strptime(dateTimeStr, "%a, %d %b %Y %H:%M:%S GMT", &tm_time);
    time_t time_lookup = timegm(&tm_time);
    
    return (int)time_lookup;
}

static bool is_s3_object_modified (struct flb_in_s3_config *ctx, flb_sds_t path,  struct flb_config *config, int last_mod_tm, int *updated_size, int *updated_mod_tm ) {
    bool is_modified = true;
    struct flb_http_client *c = NULL;
    struct flb_aws_header *s3_headers = NULL;

    s3_headers = flb_malloc(sizeof(struct flb_aws_header) * 2);
    if (s3_headers == NULL) {
        flb_errno();
         flb_plg_error(ctx->ins, "s3_headers Memory flb_Allocation failed");
        return -1;
    }
    s3_headers[0].key = "If-Modified-Since";
    s3_headers[0].key_len = 17;    
   
    char *last_mod_time = epoch_to_custom_s3_time_pattern(last_mod_tm);

    s3_headers[0].val = last_mod_time;
    s3_headers[0].val_len = strlen(last_mod_time);



    s3_headers[1].key = "Range";
    s3_headers[1].key_len = 5;    

    char *rangeInBytes = "bytes=1-100";

    s3_headers[1].val = rangeInBytes;
    s3_headers[1].val_len = strlen(rangeInBytes);

    flb_upstream_destroy(ctx->in_s3_client->upstream);
    ctx->in_s3_client->upstream = flb_upstream_create(config,
                                       ctx->endpoint, 443,
                                        FLB_IO_TLS, ctx->client_tls);
    ctx->in_s3_client->upstream->flags &= ~(FLB_IO_ASYNC);

    c = ctx->in_s3_client->client_vtable->request(ctx->in_s3_client, FLB_HTTP_GET,
                                                    path, NULL, 0,
                                                    s3_headers, 2);
    
    if (c) {
        if (c->resp.status == 304) {
                is_modified = false;
        } else if (c->resp.status == 206) {
            flb_sds_t response_headers_mod = flb_strdup(c -> resp.data);
            flb_sds_t response_headers_content_range = flb_strdup(c -> resp.data);
            
            char *last_mod_header, *content_range_header;
            // LAST_MODIFIED_HEADER 
            last_mod_header = strstr(response_headers_mod, LAST_MODIFIED_HEADER_CUSTOM);
            last_mod_header += strlen(LAST_MODIFIED_HEADER_CUSTOM);
            last_mod_header = strtok(last_mod_header, NEW_LINE_DELIMITER);

            *updated_mod_tm =  custom_s3_pattern_to_epoch(last_mod_header);
            
            content_range_header = strstr(response_headers_content_range, CONTENT_RANGE_HEADER_CUSTOM);
            content_range_header += strlen(CONTENT_RANGE_HEADER_CUSTOM);
            content_range_header = strtok(content_range_header, NEW_LINE_DELIMITER);
            *updated_size = atoi(content_range_header);
            flb_free(response_headers_mod);
            flb_free(response_headers_content_range);
        }
        flb_http_client_destroy(c);
    }
    flb_free(last_mod_time);
    flb_free(s3_headers);
    return is_modified;
}
static int initialize_s3_client (struct flb_in_s3_config *ctx,  struct flb_config *config) 
{
    if (ctx -> cred_tls) {
        flb_tls_destroy(ctx -> cred_tls);
        ctx -> cred_tls = NULL;
    }
    if (ctx -> client_tls) {
        flb_tls_destroy(ctx -> client_tls);
        ctx -> client_tls = NULL;
    }
    if (ctx->provider) {
        flb_aws_provider_destroy(ctx->provider);
    }
    if (ctx->in_s3_client) {
        flb_aws_client_destroy(ctx->in_s3_client);
    }


    ctx -> cred_tls = flb_tls_create(FLB_TRUE,
                                   -1,        /* debug */
                              NULL,      /* vhost */
                              NULL,      /* ca_path */
                              NULL,      /* ca_file */
                              NULL,      /* crt_file */
                              NULL,      /* key_file */
                              NULL);     /* key_passwd */

    if (!ctx -> cred_tls) {
        flb_plg_error(ctx->ins, "Failed to create cred_tls context");
        return -1;
    }



    ctx->client_tls = flb_tls_create(FLB_TRUE,  /* verify */
                              -1,        /* debug */
                              NULL,      /* vhost */
                              NULL,      /* ca_path */
                              NULL,      /* ca_file */
                              NULL,      /* crt_file */
                              NULL,      /* key_file */
                              NULL);     /* key_passwd */
    if (!ctx->client_tls) {
        flb_plg_error(ctx->ins, "Failed to create tls context");
        return -1;
    }

    ctx->provider = flb_standard_chain_provider_create(config,
                                                       ctx->cred_tls,
                                                       ctx->region,
                                                       NULL,
                                                       NULL,
                                                       flb_aws_client_generator());

    if (!ctx->provider) {
        flb_plg_error(ctx->ins, "Failed to create AWS Credential Provider");
        return -1;
    }

    struct flb_aws_client_generator *generator;

    generator = flb_aws_client_generator();
    ctx->in_s3_client = generator->create();
    if (!ctx->in_s3_client) {
        return -1;
    }
    ctx->in_s3_client->name = "in_s3_client";
    ctx->in_s3_client->has_auth = FLB_TRUE;
    ctx->in_s3_client->provider = ctx->provider;
    ctx->in_s3_client->region = ctx->region;
    ctx->in_s3_client->service = "s3";
    ctx->in_s3_client->port = 443;
    ctx->in_s3_client->flags = 0;
    ctx->in_s3_client->proxy = NULL;
    ctx->in_s3_client->s3_mode = S3_MODE_SIGNED_PAYLOAD;
   


    ctx->in_s3_client->upstream = flb_upstream_create(config, ctx->endpoint, 443,
                                                   FLB_IO_TLS, ctx->client_tls);
    
    if (!ctx->in_s3_client->upstream) {
        flb_plg_error(ctx->ins, "S3 Connection initialization error");
        return -1;
    }
    ctx->in_s3_client->flags &= ~(FLB_IO_ASYNC);
    ctx->in_s3_client->upstream->net.connect_timeout = 30;
    // ctx->in_s3_client->upstream->net.keepalive_idle_timeout
    ctx->provider->provider_vtable->sync(ctx->provider);
    ctx->provider->provider_vtable->init(ctx->provider);
    return 1;
    
}

static int cb_db_function(void *data, int argc, char **argv, char **cols)
{
    struct query_status *qs = data;

    qs->object = argv[0]; 
    qs->size = atoi(argv[1]);
    qs->consumed_offset = atoi(argv[2]);
    qs->last_modified_time = atoi(argv[3]);
    qs->rows++;

    if (qs->populate_state_info == 1) {


        int current_time = (int)time(NULL);
        int last_mod_time_epoch_with_twice_the_offset = ((qs->last_modified_time/qs->ignore_older)+2) * qs->ignore_older;
        if (current_time >= last_mod_time_epoch_with_twice_the_offset)
        {
            return 0;
        }

        int last_mod_time_epoch = hash_table_int_val_get(qs->file_with_last_modified_time, qs->object);
        if (last_mod_time_epoch == -1)
        {
            hash_table_int_val_insert(qs -> file_with_last_modified_time, qs->object, qs->last_modified_time);
        } else {
            hash_table_int_val_update(qs -> file_with_last_modified_time, qs->object, qs->last_modified_time);
        }
        
     
        flb_debug("DB State => object: %s, size: %d, consumed_offset: %d, last_modified_time: %d, threshold time: %d",   qs->object, qs->size,  qs->consumed_offset, qs->last_modified_time, last_mod_time_epoch_with_twice_the_offset);

        int value = hash_table_int_val_get(qs -> file_with_consumed_offset, qs->object);
        if (value == -1) {
            hash_table_int_val_insert(qs -> file_with_consumed_offset, qs->object, qs->consumed_offset);
        } else {
            hash_table_int_val_update(qs -> file_with_consumed_offset, qs->object, qs->consumed_offset);
        }
        
        value = hash_table_int_val_get(qs -> file_with_size, qs->object);
        if (value == -1) {
            hash_table_int_val_insert(qs -> file_with_size, qs->object, qs->consumed_offset);
        } else {
            hash_table_int_val_update(qs -> file_with_size, qs->object, qs->consumed_offset);
        }

      
    }
    return 0;
}

struct flb_sqldb *flb_s3_db_open(const char *path,
                                      struct flb_input_instance *ins,
                                      struct flb_in_s3_config *ctx,
                                      struct flb_config *config)
{
    int ret;
    char tmp[64];
    struct flb_sqldb *db;

    /* Open/create the database */
    db = flb_sqldb_open(path, ins->name, config);
    if (!db) {
        return NULL;
    }

    /* Create table schema if it don't exists */
    ret = flb_sqldb_query(db, SQL_CREATE_CURSOR, NULL, NULL);
    if (ret != FLB_OK) {
        flb_plg_error(ins, "db: could not create 'cursor' table");
        flb_sqldb_close(db);
        return NULL;
    }

    if (ctx->db_sync >= 0) {
        snprintf(tmp, sizeof(tmp) - 1, SQL_PRAGMA_SYNC,
                 ctx->db_sync);
        ret = flb_sqldb_query(db, tmp, NULL, NULL);
        if (ret != FLB_OK) {
            flb_plg_error(ctx->ins, "db could not set pragma 'sync'");
            flb_sqldb_close(db);
            return NULL;
        }
    }

    return db;
}


int flb_s3_db_close(struct flb_sqldb *db)
{
    flb_sqldb_close(db);
    return 0;
}
static flb_sds_t flb_update_xml_val_in_hash_table(struct flb_in_s3_config *ctx, flb_sds_t response, int max_occurence)
{
    flb_sds_t key_attrib_temp_val = NULL;
    flb_sds_t node_key_attrib_data = NULL;
    flb_sds_t end_key_attrib_data;

    flb_sds_t node_size_attrib = NULL;
    flb_sds_t end_size_attrib;

    flb_sds_t node_last_mod_attrib = NULL;
    flb_sds_t end_last_mod_attrib;

    int len;
    
    char  *xml_key_attrib =   "<Key>";
    char  *xml_size_attrib =   "<Size>";
    char  *xml_last_modified_attrib = "<LastModified>";
    int current_time = (int)time(NULL);

    for (int i=0; i < max_occurence; i ++) {

            int desired_for_monitorting = 1;
            if (key_attrib_temp_val) {
           
                flb_sds_destroy(key_attrib_temp_val);
                key_attrib_temp_val = NULL;
            }
            node_key_attrib_data = strstr(response, xml_key_attrib);
            if (!node_key_attrib_data) {
                flb_plg_debug(ctx->ins, "not anymore '%s' available; returning", xml_key_attrib);
                return NULL;
            }
            
            /* advance to end of tag */
            node_key_attrib_data += strlen(xml_key_attrib);

            end_key_attrib_data = strchr(node_key_attrib_data, '<');
            if (!end_key_attrib_data) {
                flb_plg_error(ctx->ins, "Could not find end of '%s' node in xml", xml_key_attrib);
                return NULL;
            }
           
            len = end_key_attrib_data - node_key_attrib_data;
            key_attrib_temp_val = flb_sds_create_len(node_key_attrib_data, len);
            if (!key_attrib_temp_val) {
                flb_errno();
                return NULL;
            }
            if (key_attrib_temp_val[strlen(key_attrib_temp_val)-1]=='/')
            {
                desired_for_monitorting = -1;
            }
            int is_desired_s3_file = -1;
            for (int i = 0; i < TABLE_SIZE ; i++) {
                struct entry_int_val* current = ctx -> desired_files_for_monitor->table[i];
                while (current != NULL) {
                    is_desired_s3_file = fnmatch(current->key, key_attrib_temp_val , FNM_PATHNAME);
                    if (is_desired_s3_file == 0) {
                        break;
                    }
                    current = current->next;
                }
                if (is_desired_s3_file == 0) {
                    break;
                } 
            }
            int is_not_desired_s3_file = -1;
            
            for (int i = 0; i < TABLE_SIZE ; i++) {
                struct entry_int_val* current = ctx -> exclusion_files->table[i];
                while (current != NULL) {
                    is_not_desired_s3_file = fnmatch(current->key, key_attrib_temp_val , FNM_PATHNAME);
                    if (is_not_desired_s3_file == 0) {
                        break;
                    }
                    current = current->next;
                }
                if (is_not_desired_s3_file == 0) {
                    break;
                }
            }
            if (is_not_desired_s3_file == 0) {
                flb_plg_debug(ctx->ins, "file '%s' is explicitly skipped for monitoring", key_attrib_temp_val);
                desired_for_monitorting = -1;

            }

            if (is_desired_s3_file != 0) {
                flb_plg_debug(ctx->ins, "not a desired S3 file '%s' for monitoring", key_attrib_temp_val);
                desired_for_monitorting = -1;
            }
            if (desired_for_monitorting==1){
                int offset_value = hash_table_int_val_get(ctx -> file_with_consumed_offset, key_attrib_temp_val);
                if (offset_value == -1) {
                    hash_table_int_val_insert(ctx -> file_with_consumed_offset, key_attrib_temp_val, 0);
                }

                offset_value = hash_table_int_val_get(ctx -> file_with_pending_bytes_to_consume, key_attrib_temp_val);
                if (offset_value == -1) {
                    hash_table_int_val_insert(ctx -> file_with_pending_bytes_to_consume, key_attrib_temp_val, 0);
                }
            }
            
            


            response = end_key_attrib_data;

            node_last_mod_attrib = strstr(response, xml_last_modified_attrib);
            if (!node_last_mod_attrib) {
                flb_plg_debug(ctx->ins,"not anymore '%s' available; returning", xml_last_modified_attrib);
                return NULL;
            }
             /* advance to end of tag */
            node_last_mod_attrib += strlen(xml_last_modified_attrib);

            end_last_mod_attrib = strchr(node_last_mod_attrib, '<');
            if (!end_last_mod_attrib) {
                flb_plg_error(ctx->ins,"could not find end of '%s' node in xml", xml_last_modified_attrib);
                return NULL;
            }
            len = end_last_mod_attrib - node_last_mod_attrib;

            flb_sds_t last_mod_temp_val = NULL;

            last_mod_temp_val = flb_sds_create_len(node_last_mod_attrib, len);
            if (!last_mod_temp_val) {
                flb_errno();
                return NULL;
            }
            
            int last_mod_time_epoch = convert_to_epoch(last_mod_temp_val);
            flb_sds_destroy(last_mod_temp_val);
            last_mod_temp_val = NULL;

            int last_mod_time_epoch_with_twice_the_offset = ((last_mod_time_epoch/ctx->ignore_older)+2) * ctx->ignore_older;
            if (current_time >= last_mod_time_epoch_with_twice_the_offset)
            {
                if (desired_for_monitorting == 1)  {
                    hash_table_int_val_delete(ctx -> file_with_consumed_offset, key_attrib_temp_val);
                    hash_table_int_val_delete(ctx -> file_with_pending_bytes_to_consume, key_attrib_temp_val);
                    hash_table_int_val_delete(ctx -> file_with_size, key_attrib_temp_val);
                    hash_table_int_val_delete(ctx -> file_with_last_modified_time, key_attrib_temp_val);
                    hash_table_string_val_delete(ctx -> file_with_last_consumed_line, key_attrib_temp_val);
                    flb_debug("ignoring older object %s with last mod time %d, threshold time %d", key_attrib_temp_val, last_mod_time_epoch, last_mod_time_epoch_with_twice_the_offset);
                }
               
                response = end_last_mod_attrib;
                continue;
            }

            if (desired_for_monitorting == -1)  {
                response = end_last_mod_attrib;
                continue;
            }
           
            
            int mod_time_value = hash_table_int_val_get(ctx -> file_with_last_modified_time, key_attrib_temp_val);
            if (mod_time_value == -1) {
                hash_table_int_val_insert(ctx -> file_with_last_modified_time, key_attrib_temp_val, last_mod_time_epoch);
            } else if (mod_time_value != last_mod_time_epoch) {
                hash_table_int_val_update(ctx -> file_with_last_modified_time, key_attrib_temp_val, last_mod_time_epoch);

            }
            response = end_last_mod_attrib;


            node_size_attrib = strstr(response, xml_size_attrib);
            if (!node_size_attrib) {
                flb_plg_debug(ctx->ins,"not anymore '%s' available; returning", xml_size_attrib);
                return NULL;
            }

            /* advance to end of tag */
            node_size_attrib += strlen(xml_size_attrib);

            end_size_attrib = strchr(node_size_attrib, '<');
            if (!end_size_attrib) {
                flb_plg_error(ctx->ins,"could not find end of '%s' node in xml", xml_size_attrib);
                return NULL;
            }
           
            len = end_size_attrib - node_size_attrib;

            flb_sds_t size_attrib_temp_val = NULL;

            size_attrib_temp_val = flb_sds_create_len(node_size_attrib, len);
            if (!size_attrib_temp_val) {
                flb_errno();
                return NULL;
            }
            int prefix_size =  atoi(size_attrib_temp_val);
            flb_sds_destroy(size_attrib_temp_val);
            size_attrib_temp_val = NULL;

            int value = hash_table_int_val_get(ctx -> file_with_size, key_attrib_temp_val);
            if (value == -1) {
                hash_table_int_val_insert(ctx -> file_with_size, key_attrib_temp_val, prefix_size);
                hash_table_int_val_update(ctx -> file_with_pending_bytes_to_consume, key_attrib_temp_val, prefix_size);
            } else if (prefix_size - value > 0) {
                hash_table_int_val_update(ctx -> file_with_size, key_attrib_temp_val, prefix_size);
                hash_table_int_val_update(ctx -> file_with_pending_bytes_to_consume, key_attrib_temp_val, prefix_size-value);
            } else {
                flb_plg_debug(ctx->ins, " %s object is not modified", key_attrib_temp_val);
                hash_table_int_val_update(ctx -> file_with_pending_bytes_to_consume, key_attrib_temp_val,0);
            }
            response = end_size_attrib;
    }
    if (key_attrib_temp_val) {
        flb_sds_destroy(key_attrib_temp_val);
        return strdup(key_attrib_temp_val);
    }
    return key_attrib_temp_val;
}
