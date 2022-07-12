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
#include "filter_kubernetes_labels.h"
#define PLUGIN_NAME "filter:apm_kubernetes_labels"



static int configure(struct kubernetes_labels_ctx *ctx, struct flb_filter_instance *f_ins)
{
    char *data_in;
    char *msgpack_out_buf;
    size_t msgpack_out_buf_size;
    int type;
    int ret;
    size_t len_in;
    char *file_path = NULL;
    struct mk_list *head = NULL;
    struct flb_kv *kv = NULL;

    mk_list_foreach(head, &f_ins->properties)
    {
        kv = mk_list_entry(head, struct flb_kv, _head);

        if (!strcasecmp(kv->key, LOOKUP_KEY_PATH))
        {
            file_path= flb_strndup(kv->val, flb_sds_len(kv->val));
        }
    }
    if (file_path == NULL)
    {
        flb_error("Lookup key mapping_path not found in plugin configuration");
        return -1;
    }
    //read file to buffer
    data_in = mk_file_to_buffer(file_path);
    len_in = strlen(data_in);
    if (data_in == NULL) {
        flb_error("Error reading json file %s", file_path);
        return -1;
    }
    //json to msgpack
    ret = flb_pack_json(data_in, len_in, &msgpack_out_buf, &msgpack_out_buf_size, &type);
    if (ret != 0) {
        flb_error("failed packaging JSON to msgpack");
        flb_free(data_in);
        return -1;
    }
    ctx -> pod_label_details_as_msgpack = flb_strndup(msgpack_out_buf, strlen(msgpack_out_buf));
    ctx -> len_of_pod_label_details_as_msgpack = msgpack_out_buf_size;
    
    char* proj_name_label = getenv(SFAPM_PROJECTNAME_LABEL);
    if(proj_name_label)
        ctx -> projname_labe1 = proj_name_label;
    else
        ctx -> projname_labe1 = DEFAULT_PROJECTNAME_LABEL;


    char* app_name_label = getenv(SFAPM_APPNAME_LABEL);
    if(app_name_label)
        ctx -> appname_labe1 = app_name_label;
    else
        ctx -> appname_labe1 = DEFAULT_APPNAME_LABEL;
    flb_free(data_in);
    flb_free(msgpack_out_buf);
    flb_free(file_path);

    return 0;
}

static int cb_modifier_init_apm_kubernetes_labels(struct flb_filter_instance *f_ins,
                            struct flb_config *config,
                            void *data)
{
    struct kubernetes_labels_ctx *ctx = NULL;
    ctx = flb_malloc(sizeof(struct kubernetes_labels_ctx));
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


static int cb_modifier_filter_apm_kubernetes_labels(const void *data, size_t bytes,
                              const char *tag, int tag_len,
                              void **out_buf, size_t *out_size,
                              struct flb_filter_instance *f_ins,
                              void *context,
                              struct flb_config *config)
{
    struct kubernetes_labels_ctx *ctx = context;
    size_t off = 0;
    int map_num = 0, i=0;
    struct flb_time tm;
    msgpack_sbuffer sbuffer;
    msgpack_packer packer;
    msgpack_unpacked unpacked;
    msgpack_object *obj, *old_record_key, *old_record_value;
    msgpack_object_kv *kv;
    msgpack_sbuffer_init(&sbuffer);
    msgpack_packer_init(&packer, &sbuffer, msgpack_sbuffer_write);
    msgpack_unpacked_init(&unpacked);


    int ok = MSGPACK_UNPACK_SUCCESS;   
    int num_pods, num_labels, iter_var_1, iter_var_2;

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

        size_t label_mapper_off = 0;
        msgpack_unpacked label_mapper_result;
        msgpack_unpacked_init(&label_mapper_result);
        msgpack_object label_mapper_root;
        msgpack_object *pod_name_key, *pod_details, *label_name_key, *label_val;
        int snappyflow_labels_configured_directly_tracker = 0;

        char *snappyflow_labels_configured_key_store[6];
        char *snappyflow_labels_configured_val_store[6];
        int snappyflow_labels_configured_index_store[6];

        for (i=0; i< 6 ; i++) {
            snappyflow_labels_configured_key_store[i] = NULL;
            snappyflow_labels_configured_val_store[i] = NULL;
            snappyflow_labels_configured_index_store[i] = -1;
        }

        char *pod_name_populated;

        

        msgpack_pack_array(&packer, 2);
        flb_time_append_to_msgpack(&tm, &packer, 0);
        kv = obj->via.map.ptr;
        for (i = 0; i < map_num; i++)
        {
            old_record_key = &(kv + i)->key;
            old_record_value = &(kv + i)->val;
            if (old_record_key->type == MSGPACK_OBJECT_STR)
            {
                if ((!strncasecmp(old_record_key->via.str.ptr, DEFAULT_PROJECTNAME_LABEL, DEFAULT_PROJECTNAME_LABEL_LEN)) || 
                    (!strncasecmp(old_record_key->via.str.ptr, DEFAULT_APPNAME_LABEL, DEFAULT_APPNAME_LABEL_LEN)) ||
                    (!strncasecmp(old_record_key->via.str.ptr, COMPONENT_NAME_LABEL, COMPONENT_NAME_LABEL_LEN)) ||
                    (!strncasecmp(old_record_key->via.str.ptr, UA_PARSER_LABEL, UA_PARSER_LABEL_LEN)) ||
                    (!strncasecmp(old_record_key->via.str.ptr, GEO_INFO_LABEL, GEO_INFO_LABEL_LEN)) ||
                    (!strncasecmp(old_record_key->via.str.ptr, EXCLUDE_CONTAINER_LABEL, EXCLUDE_CONTAINER_LABEL_LEN)))
                {
                    snappyflow_labels_configured_key_store[snappyflow_labels_configured_directly_tracker] = flb_strndup(old_record_key->via.str.ptr, old_record_key->via.str.size);
                    snappyflow_labels_configured_val_store[snappyflow_labels_configured_directly_tracker] = flb_strndup(old_record_value->via.str.ptr, old_record_value->via.str.size);
                    snappyflow_labels_configured_index_store[snappyflow_labels_configured_directly_tracker] = i;
                    snappyflow_labels_configured_directly_tracker = snappyflow_labels_configured_directly_tracker + 1;
                    continue;
                }
                if (!strncasecmp(old_record_key->via.str.ptr, POD_NAME_IDENT_KEY, POD_NAME_IDENT_KEY_LEN))
                {
                    pod_name_populated = flb_strndup(old_record_value->via.str.ptr, old_record_value->via.str.size);

                }
            }
        }

        if ((pod_name_populated != NULL) && (pod_name_populated[0] == '\0')) {
            msgpack_unpacked_destroy(&unpacked);
            msgpack_sbuffer_destroy(&sbuffer);
            flb_error("Pod name not available in log record");
            return FLB_FILTER_NOTOUCH;
        }
        int key_store_iter;
        int new_fields_to_add = 0;
        int new_fields_starting_index = snappyflow_labels_configured_directly_tracker;
        
        
        while (msgpack_unpack_next(&label_mapper_result, ctx->pod_label_details_as_msgpack, ctx->len_of_pod_label_details_as_msgpack, &label_mapper_off) == ok) {

            label_mapper_root = label_mapper_result.data;
            if (label_mapper_root.type != MSGPACK_OBJECT_MAP) {
                flb_error("Input json data is not a map kind: Identified type: %d",label_mapper_root.type );
                continue;
            }
            
            num_pods = label_mapper_root.via.map.size;
            for (iter_var_1 = 0; iter_var_1 < num_pods; iter_var_1++) {
                pod_name_key = &label_mapper_root.via.map.ptr[iter_var_1].key;
                pod_details = &label_mapper_root.via.map.ptr[iter_var_1].val;
                if ((pod_name_key->type == MSGPACK_OBJECT_STR ) && (!strncasecmp(pod_name_key->via.str.ptr, pod_name_populated, strlen(pod_name_populated))))
                {
                    char *pod_name = pod_name_key->via.str.ptr;
                    if (pod_details->type != MSGPACK_OBJECT_MAP) {
                        flb_error("pod %s details are not represented in map: Identified type: %d; exiting",pod_name, pod_details->type);
                        continue;
                    } 
                    num_labels = pod_details->via.map.size;
                    for (iter_var_2 = 0; iter_var_2 < num_labels; iter_var_2++) 
                    {
                        label_name_key = &pod_details->via.map.ptr[iter_var_2].key;
                        label_val = &pod_details->via.map.ptr[iter_var_2].val;
                        if (label_name_key->type == MSGPACK_OBJECT_STR ) {
                            bool new_label_identified = true;
                            for (key_store_iter=0; key_store_iter < strlen(snappyflow_labels_configured_key_store); key_store_iter++)
                            {

                                if ((snappyflow_labels_configured_key_store[key_store_iter]!=NULL) && (!strncasecmp(label_name_key->via.str.ptr, snappyflow_labels_configured_key_store[key_store_iter], label_name_key->via.str.size)))
                                {
                                    char* tmp = flb_realloc(snappyflow_labels_configured_val_store[key_store_iter], label_val->via.str.size);
                                    if (!tmp) {
                                        flb_error("Error resizing existing buffer");
                                        continue;
                                    }
                                    snappyflow_labels_configured_val_store[key_store_iter] = tmp;
                                    memcpy(snappyflow_labels_configured_val_store[key_store_iter], label_val->via.str.ptr, label_val->via.str.size);
                                    snappyflow_labels_configured_val_store[key_store_iter][label_val->via.str.size] = '\0';
                                    new_label_identified = false;
                                    break;
                                }
                            }
                            if (new_label_identified)
                            {
                                snappyflow_labels_configured_key_store[snappyflow_labels_configured_directly_tracker] = flb_strndup(label_name_key->via.str.ptr, label_name_key->via.str.size);
                                snappyflow_labels_configured_val_store[snappyflow_labels_configured_directly_tracker] = flb_strndup(label_val->via.str.ptr, label_val->via.str.size);
                                snappyflow_labels_configured_directly_tracker = snappyflow_labels_configured_directly_tracker + 1;
                                new_fields_to_add = new_fields_to_add + 1;
                            }
                        }
                    }
                    break;
                }
            }
        }
        msgpack_unpacked_destroy(&label_mapper_result);
        
        msgpack_pack_map(&packer, map_num + new_fields_to_add);
        for (i = 0 ; i<strlen(snappyflow_labels_configured_key_store); i++)
        {
            if (snappyflow_labels_configured_key_store[i]!=NULL) {   
                msgpack_pack_str(&packer, strlen(snappyflow_labels_configured_key_store[i]));
                msgpack_pack_str_body(&packer, snappyflow_labels_configured_key_store[i], strlen(snappyflow_labels_configured_key_store[i]));
                msgpack_pack_str(&packer, strlen(snappyflow_labels_configured_val_store[i]));
                msgpack_pack_str_body(&packer, snappyflow_labels_configured_val_store[i], strlen(snappyflow_labels_configured_val_store[i]));
                flb_free(snappyflow_labels_configured_val_store[i]);
                flb_free(snappyflow_labels_configured_key_store[i]);
            }
        }
        int iter;
        for (i = 0; i < map_num; i++)
        {
            bool data_to_append = true;
            for (iter = 0 ; iter<new_fields_starting_index; iter++)
            {
                if(snappyflow_labels_configured_index_store[iter] == i)
                {
                    data_to_append = false;
                    break;
                }
            }
            if (data_to_append)
            {
                msgpack_pack_object(&packer, (kv + i)->key);
                msgpack_pack_object(&packer, (kv + i)->val);
            }
        
        }
        
        flb_free(pod_name_populated);
    }

    
    msgpack_unpacked_destroy(&unpacked);
    
    *out_buf = sbuffer.data;
    *out_size = sbuffer.size;
    return FLB_FILTER_MODIFIED;
}
static int cb_modifier_exit_apm_kubernetes_labels(void *data, struct flb_config *config)
{
    struct kubernetes_labels_ctx *ctx = data;
    if (ctx != NULL)
    {
        flb_free(ctx->pod_label_details_as_msgpack);
        flb_free(ctx);
        ctx = NULL;
    }
    return 0;
}
struct flb_filter_plugin filter_apm_kubernetes_labels_plugin = {
    .name = "apm_kubernetes_labels",
    .description = "Adds custom pod labels for incoming logs",
    .cb_init = cb_modifier_init_apm_kubernetes_labels,
    .cb_filter = cb_modifier_filter_apm_kubernetes_labels,
    .cb_exit = cb_modifier_exit_apm_kubernetes_labels,
    .flags = 0};
