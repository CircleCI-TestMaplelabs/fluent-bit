#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_filter.h>
#include <fluent-bit/flb_filter_plugin.h>
#include <fluent-bit/flb_str.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_kv.h>
#include <fluent-bit/flb_time.h>
#include <msgpack.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "filter_airflow.h"
#define PLUGIN_NAME "filter:airflow"

// Define global airflowConnectSocketFD socket
int airflowConnectSocketFD = 0;
int airflowRetryConnectCounter = 0;
int multilineMessageSize = 0;
static int connect_socket(int port)
{
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
        flb_error("[%s] Invalid address/ Address not supported", PLUGIN_NAME);
        return -1;
    }
    if ((airflowConnectSocketFD = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        flb_error("[%s] Socket creation error on port %d",PLUGIN_NAME, port);
        return -1;
    }
    if (connect(airflowConnectSocketFD, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        flb_error("[%s] Connection Failed on port %d", PLUGIN_NAME, port);
        return -1;
    }
    else
    {
        flb_info("[%s] Connected to port %d", PLUGIN_NAME, port);
    }
    return airflowConnectSocketFD;
}

static int emitter_create(struct multiline_collection_ctx *ctx)
{
    int ret;
    struct flb_input_instance *ins;

    ret = flb_input_name_exists(ctx->emitter_name, ctx->config);
    if (ret == FLB_TRUE) {
        flb_error("emitter_name '%s' already exists",
                      ctx->emitter_name);
        return -1;
    }

    ins = flb_input_new(ctx->config, "emitter", NULL, FLB_FALSE);
    if (!ins) {
        flb_error("cannot create emitter instance");
        return -1;
    }

    /* Set the alias name */
    ret = flb_input_set_property(ins, "alias", ctx->emitter_name);
    if (ret == -1) {
        flb_warn("cannot set emitter_name, using fallback name '%s'",
                     ins->name);
    }

    /* Set the emitter_mem_buf_limit */
    if(ctx->emitter_mem_buf_limit > 0) {
        ins->mem_buf_limit = ctx->emitter_mem_buf_limit;
    }

    /* Set the storage type */
    ret = flb_input_set_property(ins, "storage.type",
                                 ctx->emitter_storage_type);
    if (ret == -1) {
        flb_error("cannot set storage.type");
    }

    /* Initialize emitter plugin */
    ret = flb_input_instance_init(ins, ctx->config);
    if (ret == -1) {
        flb_error("cannot initialize emitter instance '%s'",
                      ins->name);
        flb_input_instance_exit(ins, ctx->config);
        flb_input_instance_destroy(ins);
        return -1;
    }


    /* Storage context */
    ret = flb_storage_input_create(ctx->config->cio, ins);
    if (ret == -1) {
        flb_error("cannot initialize storage for stream '%s'",
                      ctx->emitter_name);
        return -1;
    }
    ctx->ins_emitter = ins;
    return 0;
}

static int configure(struct multiline_collection_ctx *ctx, struct flb_config *config, struct flb_filter_instance *f_ins)
{
    struct flb_kv *kv = NULL;
    struct mk_list *head = NULL;
    ctx->lookup_key_check = NOT_AVAILABLE;
    ctx->port_key_check = NOT_AVAILABLE;
    mk_list_foreach(head, &f_ins->properties)
    {
        kv = mk_list_entry(head, struct flb_kv, _head);

        if (!strcasecmp(kv->key, LOOKUPKEY))
        {
            ctx->lookup_key_check = AVAILABLE;
            ctx->lookup_key = flb_strndup(kv->val, flb_sds_len(kv->val));
            ctx->lookup_key_len = flb_sds_len(kv->val);
        }

        if (!strcasecmp(kv->key, PORTKEY))
        {
            ctx->port_key_check = AVAILABLE;
            ctx->port = flb_strndup(kv->val, flb_sds_len(kv->val));
            ctx->port_key_len = flb_sds_len(kv->val);
        }
    }
    if (ctx->lookup_key_check == NOT_AVAILABLE)
    {
        flb_error("[%s] lookup key not found", PLUGIN_NAME);
        return -1;
    }
    if (ctx->port_key_check == NOT_AVAILABLE)
    {
        flb_error("[%s] port key not found", PLUGIN_NAME);
        return -1;
    }
    if (connect_socket(atoi(ctx->port)) < 0)
    {
        int socket_fd = -1;
        int current_retry  = 1;
        do
        {
            socket_fd = connect_socket(atoi(ctx->port));
            current_retry++;
            if (current_retry == RETRIES)
            {
                break;
            }
        } while (socket_fd == -1);
    }
    ctx->config = config;
    flb_sds_t tmp,emitter_name;
    
    emitter_name = flb_sds_create_size(64);
    if (!emitter_name) {
        return -1;
    }

    tmp = flb_sds_printf(&emitter_name, "emitter_for_part_multiline");
    if (!tmp) {
        flb_error("cannot compose emitter_name");
        flb_sds_destroy(emitter_name);
        return -1;
    }
    ctx->emitter_name = emitter_name;
    flb_filter_set_property(f_ins, "emitter_name", emitter_name);
    flb_info("created emitter: %s", emitter_name);
    ctx->emitter_storage_type = "memory";

    flb_filter_set_property(f_ins, "emitter_storage.type", ctx->emitter_storage_type);
    flb_filter_set_context(f_ins, ctx);

    int ret = emitter_create(ctx);
    if (ret == -1) {
        flb_sds_destroy(emitter_name);
        return -1;
    }
    return 0;
}

static int cb_modifier_init_apm_airflow(struct flb_filter_instance *f_ins,
                            struct flb_config *config,
                            void *data)
{
    struct multiline_collection_ctx *ctx = NULL;
    ctx = flb_malloc(sizeof(struct multiline_collection_ctx));
    if (!ctx)
    {
        flb_errno();
        return -1;
    }
    if (configure(ctx, config, f_ins) < 0)
    {
        flb_free(ctx);
        ctx = NULL;
        return -1;
    }

    return 0;
}

int strpos(char *hay, char *needle, int offset)
{
   char haystack[strlen(hay)];
   strncpy(haystack, hay+offset, strlen(hay)-offset);
   char *p = strstr(haystack, needle);
   if (p)
      return p - haystack+offset;
   return -1;
}
void send_message_to_emitter_pipeline(struct multiline_collection_ctx *ctx ,const void *data, size_t bytes, const char *tag, int tag_len, char *message_key, char *message)  
{
    size_t off = 0;
    int map_num = 0;
    // size_t pre = 0;
    struct flb_time tm;
    msgpack_sbuffer sbuffer;
    msgpack_packer packer;
    msgpack_unpacked unpacked;
    msgpack_object *obj, *old_record_key;
    msgpack_object_kv *kv;
    msgpack_sbuffer_init(&sbuffer);
    msgpack_packer_init(&packer, &sbuffer, msgpack_sbuffer_write);
    msgpack_unpacked_init(&unpacked);
    while (msgpack_unpack_next(&unpacked, data, bytes, &off) == MSGPACK_UNPACK_SUCCESS)
    {
        if (unpacked.data.type != MSGPACK_OBJECT_ARRAY)
        {
            continue;
        }
        flb_time_pop_from_msgpack(&tm, &unpacked, &obj);

        if (obj->type == MSGPACK_OBJECT_MAP)
        {
            map_num = obj->via.map.size;
        }
        else
        {
            continue;
        }
        msgpack_pack_array(&packer, 2);
        flb_time_append_to_msgpack(&tm, &packer, 0);
        msgpack_pack_map(&packer, map_num + NEW_ENTRIES);
        kv = obj->via.map.ptr;
        int i = 0;
        for (i = 0; i < map_num; i++)
        {
            old_record_key = &(kv + i)->key;
            if (!(old_record_key->type == MSGPACK_OBJECT_STR && !strncasecmp(old_record_key->via.str.ptr, ctx->lookup_key, ctx->lookup_key_len)))
            {
                msgpack_pack_object(&packer, (kv + i)->key);
                msgpack_pack_object(&packer, (kv + i)->val);
            }
        }
    }
    msgpack_pack_str(&packer, strlen(message_key));
    msgpack_pack_str_body(&packer, message_key, strlen(message_key));
    msgpack_pack_str(&packer, strlen(message));
    msgpack_pack_str_body(&packer, message, strlen(message));
    char *new_tag = (char *)flb_malloc( tag_len + 2);
    if (new_tag == NULL)
    {
        msgpack_unpacked_destroy(&unpacked);
        return;
    }                
    strcpy(new_tag, tag);
    strncat(new_tag, "_1", 2);
    int ret;
    ret = in_emitter_add_record(new_tag, strlen(new_tag),
                                    sbuffer.data, sbuffer.size,
                                    ctx -> ins_emitter);
    if (ret < 0) {
            /* this shouldn't happen in normal execution */
        flb_warn("Couldn't send re-emitterd record of size %zu bytes to in_emitter %s",
                         sbuffer.size, ctx -> emitter_name);
    }
    flb_free(new_tag);
    msgpack_unpacked_destroy(&unpacked);
    msgpack_sbuffer_destroy(&sbuffer);

}
static int populate_multiline_message(struct multiline_collection_ctx *ctx ,char *message, char *file,char *message_key, int message_key_len, int port, const void *fluent_data, size_t fluent_data_bytes, const char *match_tag, int match_tag_len, msgpack_packer *packer)
{
    int retry = 0;
    int size_recv, total_size = 0;
    //512KB of characters to accommodate i.e buffer size
    char message_size_prefix[FACTOR_512KB];
    sprintf(message_size_prefix, "%ld$", strlen(message)+strlen(file)+NUM_1);
    flb_sds_t message_with_filename_and_size_prefix = flb_sds_create_size(strlen(file)+1+strlen(message_size_prefix) + strlen(message));
    if (!message_with_filename_and_size_prefix) {
        return data_collection_failed;
    }

    flb_sds_t message_with_prefix_tmp;
    message_with_prefix_tmp = flb_sds_copy(message_with_filename_and_size_prefix, message_size_prefix, strlen(message_size_prefix));
    if (!message_with_prefix_tmp) 
    {
        flb_sds_destroy(message_with_filename_and_size_prefix);
        return data_collection_failed;
    }
    message_with_filename_and_size_prefix = message_with_prefix_tmp;

    message_with_prefix_tmp = flb_sds_cat(message_with_filename_and_size_prefix, file, strlen(file));
    if (!message_with_prefix_tmp) 
    {
        flb_sds_destroy(message_with_filename_and_size_prefix);
        return data_collection_failed;
    }
    message_with_filename_and_size_prefix = message_with_prefix_tmp;

    message_with_prefix_tmp = flb_sds_cat(message_with_filename_and_size_prefix, LITERAL_HASH, NUM_1);
    if (!message_with_prefix_tmp) 
    {
        flb_sds_destroy(message_with_filename_and_size_prefix);
        return data_collection_failed;
    }
    message_with_filename_and_size_prefix = message_with_prefix_tmp;

    message_with_prefix_tmp = flb_sds_cat(message_with_filename_and_size_prefix, message, strlen(message));
    if (!message_with_prefix_tmp)
    {
        flb_sds_destroy(message_with_filename_and_size_prefix);
        return data_collection_failed;
    }
    message_with_filename_and_size_prefix = message_with_prefix_tmp;

    // Recv buffer len is configured based on prev Multiline stack size along with a 50% increased size
    int recv_buffer_len_max = strlen(message_with_filename_and_size_prefix) + (strlen(message_with_filename_and_size_prefix) / 2) + multilineMessageSize;
    flb_sds_t recv_buffer = flb_sds_create_size(recv_buffer_len_max);
    if (!recv_buffer) {
        return data_collection_failed;
    }
    flb_sds_t tmp;
    char buffer[SOCKET_BUF_SIZE];
    int sockSendStatus = 1;
    flb_debug("Socket Send data %s",  message_with_filename_and_size_prefix);

sockSend:
    if ((sockSendStatus = send(airflowConnectSocketFD, message_with_filename_and_size_prefix, strlen(message_with_filename_and_size_prefix), 0)) == -1)
    {
        flb_error("[%s] Error in sending the agent %s", PLUGIN_NAME, message_with_filename_and_size_prefix);
        goto retry;
    }
    // flb_debug("Socket data of size %d sent successfully", strlen(message_with_prefix));
    while (1)
    {
        memset(buffer, 0, SOCKET_BUF_SIZE);
        if ((size_recv = recv(airflowConnectSocketFD, buffer, SOCKET_BUF_SIZE, 0)) < 0)
        {
        retry:
            while (1)
            {
                flb_info("[%s] Trying to reconnect the socket: retry %d/%d", PLUGIN_NAME, retry, RETRIES);
                if (connect_socket(port) < 0)
                {
                    flb_error("[%s] Unable to reconnect to the socket", PLUGIN_NAME);
                    if (retry++ >= RETRIES)
                    {
                        flb_sds_destroy(recv_buffer);
                        flb_sds_destroy(message_with_filename_and_size_prefix);
                        return unable_to_connect;
                    }
                    continue;
                }
                retry = 0;
                if (sockSendStatus == -1)
                {
                    goto sockSend;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            if (total_size + size_recv <= recv_buffer_len_max)
            {
                tmp = flb_sds_cat(recv_buffer, buffer, size_recv);
                if (!tmp) {
                    flb_sds_destroy(recv_buffer);
                    flb_sds_destroy(message_with_filename_and_size_prefix);
                    return data_collection_failed;
                }
                recv_buffer = tmp;
                total_size += size_recv;
            }
            else
            {
                flb_error("Buffer Overflow occured = %s ", buffer);
                flb_error("Buffer Len = %d ", strlen(buffer));
                flb_error("Configured Recv Buff Len Max = %d ", recv_buffer_len_max);
                flb_error("Current Multiline message stack len = %d ", multilineMessageSize);
                break;
            }
            if (size_recv < SOCKET_BUF_SIZE)
            {
                break;
            }
        }
    }
    
    bool to_discard = (strcmp(recv_buffer, LITERAL_DISCARD) == 0);
    bool to_keep = (strcmp(recv_buffer,  LITERAL_KEEP) == 0);
    flb_debug("Received data of len %d : %s",  strlen(recv_buffer), recv_buffer);
    flb_trace("Socket data received successfully %d", total_size);
    
    //Multiline Log Analysis
    bool messageHavingMultipleMultilines = false;
    int offset=0, pos;
    
    if (!(to_discard) && !(to_keep))
    {
        char *message_stack;
        for(; ;)  {
            
            pos = strpos(recv_buffer, SNAPPY_LOG_SEPERATOR, offset);
            
            if (pos == -1){
                
                if (offset == 0) 
                {
                    break;
                }
                messageHavingMultipleMultilines = true;
                message_stack = malloc(total_size-offset+1);
                if (message_stack == NULL)
                {
                    break;
                }
                strncpy(message_stack,recv_buffer+offset, total_size-offset);
                flb_debug("Final Message stack sent to emitter pipeline: %s", message_stack);
                send_message_to_emitter_pipeline(ctx, fluent_data, fluent_data_bytes, match_tag, match_tag_len, message_key, message_stack);
                free(message_stack);
                break;
            }
            message_stack = malloc(pos-offset+1);
            if (message_stack == NULL)
            {
                break;
            }
            strncpy(message_stack,recv_buffer+offset,pos-offset);
            flb_debug("Message stack sent to emitter pipeline: %s", message_stack);
            send_message_to_emitter_pipeline(ctx, fluent_data, fluent_data_bytes, match_tag, match_tag_len, message_key, message_stack);
            offset = pos+SNAPPY_LOG_SEPERATOR_LEN;
            free(message_stack);
            if (offset >= total_size)
            {
                messageHavingMultipleMultilines = true;
                break;
            }
        }
    }
    msgpack_pack_str(packer, message_key_len);
    msgpack_pack_str_body(packer, message_key, message_key_len);
    
    if (to_discard)
    {
        multilineMessageSize = multilineMessageSize + strlen(message);
        flb_debug("Command received to discarding the log");
        msgpack_pack_str(packer, total_size);
        msgpack_pack_str_body(packer, recv_buffer, total_size);
    }
    else if (to_keep)
    {
        msgpack_pack_str(packer, strlen(message));
        msgpack_pack_str_body(packer, message, strlen(message));
        flb_debug("Command received to promote the original log to output pipeline");
    }
    else if (messageHavingMultipleMultilines) {
        msgpack_pack_str(packer, LITERAL_DISCARD_LEN);
        msgpack_pack_str_body(packer, LITERAL_DISCARD, LITERAL_DISCARD_LEN);
    }
    else
    {    
        multilineMessageSize = 0;
        msgpack_pack_str(packer, total_size);
        msgpack_pack_str_body(packer, recv_buffer, total_size);
        flb_debug("Command received to promote the modified log to output pipeline");
    }
    
    flb_sds_destroy(recv_buffer);
    flb_sds_destroy(message_with_filename_and_size_prefix);

    return data_collection_successful;
}

static int cb_modifier_filter_apm_airflow(const void *data, size_t bytes,
                              const char *tag, int tag_len,
                              void **out_buf, size_t *out_size,
                              struct flb_filter_instance *f_ins,
                              void *context,
                              struct flb_config *config)
{
    struct multiline_collection_ctx *ctx = context;
    size_t off = 0;
    int collection_status = data_collection_in_progress;
    int map_num = 0;
    // size_t pre = 0;
    struct flb_time tm;
    msgpack_sbuffer sbuffer;
    msgpack_packer packer;
    msgpack_unpacked unpacked;
    msgpack_object *obj, *old_record_key, *old_record_value;
    msgpack_object_kv *kv;
    msgpack_sbuffer_init(&sbuffer);
    msgpack_packer_init(&packer, &sbuffer, msgpack_sbuffer_write);
    msgpack_unpacked_init(&unpacked);
    while (msgpack_unpack_next(&unpacked, data, bytes, &off) == MSGPACK_UNPACK_SUCCESS)
    {
        if (unpacked.data.type != MSGPACK_OBJECT_ARRAY)
        {
            continue;
        }
        flb_time_pop_from_msgpack(&tm, &unpacked, &obj);

        if (obj->type == MSGPACK_OBJECT_MAP)
        {
            map_num = obj->via.map.size;
        }
        else
        {
            continue;
        }
        msgpack_pack_array(&packer, 2);
        flb_time_append_to_msgpack(&tm, &packer, 0);
        msgpack_pack_map(&packer, map_num + NEW_ENTRIES);
        kv = obj->via.map.ptr;
        int i = 0;
        char *message;
        char *file;
        for (i = 0; i < map_num; i++)
        {
            old_record_key = &(kv + i)->key;
            old_record_value = &(kv + i)->val;
            if (old_record_key->type == MSGPACK_OBJECT_STR && !strncasecmp(old_record_key->via.str.ptr, ctx->lookup_key, ctx->lookup_key_len))
            {
                message = flb_strndup(old_record_value->via.str.ptr, old_record_value->via.str.size);
                continue;
            }
            if (old_record_key->type == MSGPACK_OBJECT_STR && !strncasecmp(old_record_key->via.str.ptr, FIELD_FILE, FIELD_FILE_LEN))
            {
                file = flb_strndup(old_record_value->via.str.ptr, old_record_value->via.str.size);
            }
            msgpack_pack_object(&packer, (kv + i)->key);
            msgpack_pack_object(&packer, (kv + i)->val);
        }
        if ((message == NULL) || (file == NULL)){
            flb_error("[%s] Lookup key %s and filename key not found",PLUGIN_NAME );
            msgpack_unpacked_destroy(&unpacked);
            msgpack_sbuffer_destroy(&sbuffer);
            return FLB_FILTER_NOTOUCH;
        }
        collection_status = populate_multiline_message(ctx, message, file,ctx->lookup_key, ctx->lookup_key_len,atoi(ctx->port), data, bytes, tag, tag_len,&packer);
        
        if (collection_status == unable_to_connect)
        {
            flb_error("[%s] Unable to establish connection with the socket server", PLUGIN_NAME);
        }

        flb_free(message);
        flb_free(file);
    }

    msgpack_unpacked_destroy(&unpacked);

    if (collection_status == data_collection_in_progress)
    {
        flb_error("[%s] Lookup key %s not found in the log record", PLUGIN_NAME, ctx->lookup_key);
        msgpack_sbuffer_destroy(&sbuffer);
        return FLB_FILTER_NOTOUCH;
    }
    *out_buf = sbuffer.data;
    *out_size = sbuffer.size;
    return FLB_FILTER_MODIFIED;
}
static int cb_modifier_exit_apm_airflow(void *data, struct flb_config *config)
{
    struct multiline_collection_ctx *ctx = data;
    close(airflowConnectSocketFD);
    if (ctx != NULL)
    {
        flb_free(ctx->lookup_key);
        ctx->lookup_key = NULL;
        flb_free(ctx->port);
        ctx->port = NULL;
        flb_input_instance_exit(ctx->ins_emitter, ctx->config);
        flb_input_instance_destroy(ctx->ins_emitter);
        flb_sds_destroy(ctx->emitter_name);
        flb_free(ctx);
        ctx = NULL;

    }
    return 0;
}

struct flb_filter_plugin filter_apm_goals_airflow_plugin = {
    .name = "apm_goals_airflow",
    .description = "Custom multiline parsing plugin for Airflow logs",
    .cb_init = cb_modifier_init_apm_airflow,
    .cb_filter = cb_modifier_filter_apm_airflow,
    .cb_exit = cb_modifier_exit_apm_airflow,
    .flags = 0};