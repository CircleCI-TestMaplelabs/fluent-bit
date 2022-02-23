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
#include "filter_message_formatter.h"
#define PLUGIN_NAME "filter:apm_message_formatter"

// Define global socketConnectFD socket
int socketConnectFD = 0;
int retrySocketConnectCounter = 0;

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
    if ((socketConnectFD = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        flb_error("[%s] Socket creation error on port %d", PLUGIN_NAME, port);
        return -1;
    }
    if (connect(socketConnectFD, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        flb_error("[%s] Connection Failed on port %d", PLUGIN_NAME, port);
        return -1;
    }
    else
    {
        flb_info("[%s] Connected to port %d", PLUGIN_NAME, port);
    }
    return socketConnectFD;
}

static int configure(struct message_formatter_ctx *ctx, struct flb_filter_instance *f_ins)
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
        return 0;
    }
    return 0;
}

static int cb_modifier_init_apm_message_formatter(struct flb_filter_instance *f_ins,
                                                  struct flb_config *config,
                                                  void *data)
{
    struct message_formatter_ctx *ctx = NULL;
    ctx = flb_malloc(sizeof(struct message_formatter_ctx));
    if (!ctx)
    {
        flb_errno();
        return -1;
    }
    if (configure(ctx, f_ins) < 0)
    {
        flb_free(ctx);
        ctx = NULL;
        return -1;
    }

    flb_filter_set_context(f_ins, ctx);
    return 0;
}

static int populate_formatted_message(char *message, int port, msgpack_packer *packer)
{
    int valread = 0, retry = 0;
    int size_recv, total_size = 0;
    int new_buffer_len_max = strlen(message) + (strlen(message) / 4);
    char new_buffer[new_buffer_len_max];
    char buffer[SOCKET_BUF_SIZE];
    int sockSendStatus = 1;
sockSend:
    if ((sockSendStatus = send(socketConnectFD, message, strlen(message), 0)) == -1)
    {
        flb_error("[%s] Error in sending the agent %s", PLUGIN_NAME, message);
        goto retry;
    }
    flb_debug("Socket data of size %d sent successfully", strlen(message));
    while (1)
    {
        memset(buffer, 0, SOCKET_BUF_SIZE);
        if ((size_recv = recv(socketConnectFD, buffer, SOCKET_BUF_SIZE, 0)) < 0)
        {
        retry:
            while (1)
            {
                flb_info("[%s] Trying to reconnect the socket: retry %d/%d", PLUGIN_NAME, retry, RETRIES);
                if (connect_socket(port) < 0)
                {
                    flb_info("[%s] Unable to reconnect to the socket", PLUGIN_NAME);
                    if (retry++ > RETRIES)
                    {
                        return unable_to_connect;
                    }
                    continue;
                }
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
            flb_debug("Socket data receiving in parts successfully");
            if (total_size == 0)
            {
                strcpy(new_buffer, buffer);
                total_size += size_recv;
            }
            else
            {
                if (total_size + size_recv < new_buffer_len_max)
                {
                    strcat(new_buffer, buffer);
                    total_size += size_recv;
                }
                else
                {
                    flb_error("Buffer Overflow occured = %s ", buffer);
                }
            }
            if (size_recv < SOCKET_BUF_SIZE)
            {
                break;
            }
        }
    }
    if (total_size > 0)
    {
        msgpack_pack_str(packer, MESSAGE_KEY_SIZE);
        msgpack_pack_str_body(packer, MESSAGE, MESSAGE_KEY_SIZE);
        msgpack_pack_str(packer, total_size);
        msgpack_pack_str_body(packer, new_buffer, total_size);
        flb_debug("Socket data received successfully %d", total_size);
    }
    else
    {
        msgpack_pack_str(packer, MESSAGE_KEY_SIZE);
        msgpack_pack_str_body(packer, MESSAGE, MESSAGE_KEY_SIZE);
        msgpack_pack_str(packer, strlen(message));
        msgpack_pack_str_body(packer, message, strlen(message));
    }
    return data_collected;
}

static int cb_modifier_filter_apm_message_formatter(const void *data, size_t bytes,
                                                    const char *tag, int tag_len,
                                                    void **out_buf, size_t *out_size,
                                                    struct flb_filter_instance *f_ins,
                                                    void *context,
                                                    struct flb_config *config)
{
    struct message_formatter_ctx *ctx = context;
    // flb_info("ppm %d",ctx->lookup_key->len);
    size_t off = 0;
    int collection_status = message_field_not_available;
    int map_num = 0;
    struct flb_time tm;
    msgpack_sbuffer sbuffer;
    msgpack_packer packer;
    msgpack_unpacked unpacked;
    msgpack_object *obj, *old_record_key, *old_record_value;
    msgpack_object_kv *kv;
    msgpack_sbuffer_init(&sbuffer);
    msgpack_packer_init(&packer, &sbuffer, msgpack_sbuffer_write);
    msgpack_unpacked_init(&unpacked);
    size_t mesage_len = 0;
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
        msgpack_pack_map(&packer, map_num);
        kv = obj->via.map.ptr;
        int i = 0;
        for (i = 0; i < map_num; i++)
        {
            old_record_key = &(kv + i)->key;
            old_record_value = &(kv + i)->val;
            if (old_record_key->type == MSGPACK_OBJECT_STR && !strncasecmp(old_record_key->via.str.ptr, ctx->lookup_key, ctx->lookup_key_len))
            {
                char *message = flb_strndup(old_record_value->via.str.ptr, old_record_value->via.str.size);
                if (message == NULL)
                {
                    msgpack_pack_object(&packer, (kv + i)->key);
                    msgpack_pack_object(&packer, (kv + i)->val);
                    continue;
                }
                mesage_len = old_record_value->via.str.size;
                char *endln = "\n";
                char *formattedMessage = (char *)flb_malloc(old_record_value->via.str.size + 4);
                if (formattedMessage == NULL)
                {
                    msgpack_pack_object(&packer, (kv + i)->key);
                    msgpack_pack_object(&packer, (kv + i)->val);
                    continue;
                }
                strcpy(formattedMessage, message);
                strncat(formattedMessage, endln, strlen(endln));
                flb_debug("[%s] Sending message for fomatting: %s", PLUGIN_NAME, formattedMessage);
                // populates record map with agent information
                collection_status = populate_formatted_message(formattedMessage, atoi(ctx->port), &packer);
                if (collection_status == unable_to_connect)
                {
                    flb_error("[%s] Unable to establish connection with the socket server: Log retry %d/%d", PLUGIN_NAME, retrySocketConnectCounter, GLOBALRETRIES);
                    msgpack_pack_object(&packer, (kv + i)->key);
                    msgpack_pack_object(&packer, (kv + i)->val);
                    retrySocketConnectCounter++;
                }
                flb_free(message);
                flb_free(formattedMessage);
            }
            else
            {
                msgpack_pack_object(&packer, (kv + i)->key);
                msgpack_pack_object(&packer, (kv + i)->val);
            }
        }
    }
    // flb_error(collection_status);
    msgpack_unpacked_destroy(&unpacked);
    if (collection_status == message_field_not_available)
    {
        flb_error("[%s] Lookup key %s not found in the log record", PLUGIN_NAME, ctx->lookup_key);
        msgpack_sbuffer_destroy(&sbuffer);
        return FLB_FILTER_NOTOUCH;
    }
    *out_buf = sbuffer.data;
    *out_size = sbuffer.size;
    return FLB_FILTER_MODIFIED;
}
static int cb_modifier_exit_apm_message_formatter(void *data, struct flb_config *config)
{
    struct message_formatter_ctx *ctx = data;
    close(socketConnectFD);
    if (ctx != NULL)
    {
        flb_free(ctx->lookup_key);
        ctx->lookup_key = NULL;
        flb_free(ctx->port);
        ctx->port = NULL;
        flb_free(ctx);
        ctx = NULL;
    }
    return 0;
}
struct flb_filter_plugin filter_apm_message_formatter_plugin = {
    .name = "apm_message_formatter",
    .description = "Adds Formatted message",
    .cb_init = cb_modifier_init_apm_message_formatter,
    .cb_filter = cb_modifier_filter_apm_message_formatter,
    .cb_exit = cb_modifier_exit_apm_message_formatter,
    .flags = 0};
