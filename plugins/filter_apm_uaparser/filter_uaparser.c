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
#include <io.h>
#include <stdlib.h>
#include "filter_uaparser.h"
#define PLUGIN_NAME "filter:apm_uaparser"

// Define global sock socket
int sock = 0;
int retryCounter = 0;

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
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        flb_error("[%s] Socket creation error on port %d",PLUGIN_NAME, port);
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        flb_error("[%s] Connection Failed on port %d", PLUGIN_NAME, port);
        return -1;
    }
    else
    {
        flb_info("[%s] Connected to port %d", PLUGIN_NAME, port);
    }
    return sock;
}

static int configure(struct uaparser_ctx *ctx, struct flb_filter_instance *f_ins)
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

static int cb_modifier_init_apm_ua(struct flb_filter_instance *f_ins,
                            struct flb_config *config,
                            void *data)
{
    struct uaparser_ctx *ctx = NULL;
    ctx = flb_malloc(sizeof(struct uaparser_ctx));
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

static int get_agent_info(char *agent, int port, msgpack_packer *packer)
{
    int valread = 0, retry = 0;
    char buffer[1024] = {0};
    char *entry;
    if (send(sock, agent, strlen(agent), 0) == -1)
    {
        flb_error("[%s] Error in sending the agent %s", PLUGIN_NAME, agent);
        goto retry;
    }
    valread = recv(sock, buffer, 1024, 0);
    if (valread == 0 || valread == -1)
    {
        retry:
        do
        {
            flb_info("[%s] Trying to reconnect the socket: retry %d/%d", PLUGIN_NAME, retry, RETRIES) ;
            if (connect_socket(port) < 0)
            {
                flb_info("[%s] Unable to reconnect the socket", PLUGIN_NAME);
                if (retry++ > RETRIES) {
                    return unable_to_connect;
                }
                continue;
            }
            if (send(sock, agent, strlen(agent), 0) == -1)
            {
                flb_error("[%s] Error in sending the agent %s: retry %d/%d", PLUGIN_NAME, agent,  retry, RETRIES);
                if (retry++ > RETRIES) {
                    return unable_to_connect;
                }
                continue;
            }
            valread = recv(sock, buffer, 1024, 0);
        } while (valread == 0);
    }

    entry = strtok(buffer, "}");
    while (entry != NULL)
    {
        msgpack_pack_str(packer, strlen(entry));
        msgpack_pack_str_body(packer, entry, strlen(entry));
        entry = strtok(NULL, "}");
    }
    return data_collected;
}

static void add_default_ua_fields(msgpack_packer *packer)
{
    flb_trace("[%s] Adding Default agent fields", PLUGIN_NAME);
    //Adding Default Browser
    msgpack_pack_str(packer, BROWSER_NAME_LEN);
    msgpack_pack_str_body(packer, BROWSER_NAME, BROWSER_NAME_LEN);
    msgpack_pack_str(packer, DEFAULT_LEN);
    msgpack_pack_str_body(packer, DEFAULT, DEFAULT_LEN);
    //Adding Default BROWSER_VER
    msgpack_pack_str(packer, BROWSER_VER_LEN);
    msgpack_pack_str_body(packer, BROWSER_VER, BROWSER_VER_LEN);
    msgpack_pack_str(packer, DEFAULT_LEN);
    msgpack_pack_str_body(packer, DEFAULT, DEFAULT_LEN);
    //Adding Default BROWSER
    msgpack_pack_str(packer, BROWSER_LEN);
    msgpack_pack_str_body(packer, BROWSER, BROWSER_LEN);
    msgpack_pack_str(packer, DEFAULT_LEN);
    msgpack_pack_str_body(packer, DEFAULT, DEFAULT_LEN);
    //Adding Default OS_NAME
    msgpack_pack_str(packer, OS_NAME_LEN);
    msgpack_pack_str_body(packer, OS_NAME, OS_NAME_LEN);
    msgpack_pack_str(packer, DEFAULT_LEN);
    msgpack_pack_str_body(packer, DEFAULT, DEFAULT_LEN);
    //Adding Default OS_VERSION
    msgpack_pack_str(packer, OS_VERSION_LEN);
    msgpack_pack_str_body(packer, OS_VERSION, OS_VERSION_LEN);
    msgpack_pack_str(packer, DEFAULT_LEN);
    msgpack_pack_str_body(packer, DEFAULT, DEFAULT_LEN);
    //Adding Default OS
    msgpack_pack_str(packer, OS_LEN);
    msgpack_pack_str_body(packer, OS, OS_LEN);
    msgpack_pack_str(packer, DEFAULT_LEN);
    msgpack_pack_str_body(packer, DEFAULT, DEFAULT_LEN);
    //Adding Default DEVICE
    msgpack_pack_str(packer, DEVICE_LEN);
    msgpack_pack_str_body(packer, DEVICE, DEVICE_LEN);
    msgpack_pack_str(packer, DEFAULT_LEN);
    msgpack_pack_str_body(packer, DEFAULT, DEFAULT_LEN);
    //Adding Default DEVICE_BRAND
    msgpack_pack_str(packer, DEVICE_BRAND_LEN);
    msgpack_pack_str_body(packer, DEVICE_BRAND, DEVICE_BRAND_LEN);
    msgpack_pack_str(packer, DEFAULT_LEN);
    msgpack_pack_str_body(packer, DEFAULT, DEFAULT_LEN);
    //Adding Default DEVICE_MODEL
    msgpack_pack_str(packer, DEVICE_MODEL_LEN);
    msgpack_pack_str_body(packer, DEVICE_MODEL, DEVICE_MODEL_LEN);
    msgpack_pack_str(packer, DEFAULT_LEN);
    msgpack_pack_str_body(packer, DEFAULT, DEFAULT_LEN);
}

static int cb_modifier_filter(const void *data, size_t bytes,
                              const char *tag, int tag_len,
                              void **out_buf, size_t *out_size,
                              struct flb_filter_instance *f_ins,
                              void *context,
                              struct flb_config *config)
{
    struct uaparser_ctx *ctx = context;
    //flb_info("ppm %d",ctx->lookup_key->len);
    size_t off = 0;
    int uaparser_status = agent_not_available;
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
            old_record_value = &(kv + i)->val;
            if (old_record_key->type == MSGPACK_OBJECT_STR && !strncasecmp(old_record_key->via.str.ptr, ctx->lookup_key, ctx->lookup_key_len))
            {
                char *agentString = flb_strndup(old_record_value->via.str.ptr, old_record_value->via.str.size);
                char *endln = "\n" ;
                char *agent = (char *)flb_malloc(old_record_value->via.str.size + 4);
                strcpy(agent, agentString);
                strncat(agent, endln, strlen(endln));
                flb_trace("[%s] Sending agent: %s", PLUGIN_NAME, agentString);
                //populates record map with agent information
                if (retryCounter <= GLOBALRETRIES)
                {
                    uaparser_status = get_agent_info(agent, atoi(ctx->port), &packer);
                }
                else
                {
                    flb_debug("[%s] Max retry limit exceed, skipping agent fields", PLUGIN_NAME);
                    uaparser_status = add_default;
                    // add_default_ua_fields(&packer);
                }
                flb_free(agent);
                flb_free(agentString);
            }
            msgpack_pack_object(&packer, (kv + i)->key);
            msgpack_pack_object(&packer, (kv + i)->val);
        }
    }
    
    msgpack_unpacked_destroy(&unpacked);
    if (uaparser_status == agent_not_available)
    {
        flb_error("[%s] Lookup key %s not found", PLUGIN_NAME, ctx->lookup_key);
        return FLB_FILTER_NOTOUCH;
    }
    else if (uaparser_status == unable_to_connect)
    {
        flb_error("[%s] Unable to establish connection with the socket server: Log retry %d/%d", PLUGIN_NAME, retryCounter, GLOBALRETRIES);
        retryCounter++;
        add_default_ua_fields(&packer);
    }
    *out_buf = sbuffer.data;
    *out_size = sbuffer.size;
    return FLB_FILTER_MODIFIED;
}
static int cb_modifier_exit(void *data, struct flb_config *config)
{
    struct uaparser_ctx *ctx = data;
    close(sock);
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
struct flb_filter_plugin filter_apm_uaparser_plugin = {
    .name = "apm_uaparser",
    .description = "Adds User Agent Information",
    .cb_init = cb_modifier_init_apm_ua,
    .cb_filter = cb_modifier_filter,
    .cb_exit = cb_modifier_exit,
    .flags = 0};
