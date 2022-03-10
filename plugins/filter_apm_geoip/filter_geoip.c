#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_filter.h>
#include <fluent-bit/flb_filter_plugin.h>
#include <fluent-bit/flb_str.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_kv.h>
#include <fluent-bit/flb_time.h>
#include <msgpack.h>
#include "filter_geoip.h"
#define PLUGIN_NAME "filter_apm_geoip"
static int geoip_configure(struct geoip_ctx *ctx, struct flb_filter_instance *f_ins)
{
    struct flb_kv *kv = NULL;
    struct mk_list *head = NULL;
    ctx->lookup_key_check = NOT_AVAILABLE;
    ctx->db_availability = NOT_AVAILABLE;
    ctx->mmdb = flb_malloc(sizeof(MMDB_s));
    mk_list_foreach(head, &f_ins->properties) {
        kv = mk_list_entry(head, struct flb_kv, _head);
        if (!strcasecmp(kv->key, "Database")) {
            ctx->db_availability = AVAILABLE;
            int status = MMDB_open(kv->val, MMDB_MODE_MMAP, ctx->mmdb);
            if (status != MMDB_SUCCESS) {
                flb_error("Cannot open geoip database: %s: %s", kv->val, MMDB_strerror(status));
                flb_free(ctx->mmdb);
                return -1;
            }
        }
        if (!strcasecmp(kv->key, "Lookup_key")) {
            ctx->lookup_key_check = AVAILABLE;
            ctx->lookup_key = flb_strndup(kv->val, flb_sds_len(kv->val));
            ctx->lookup_key_len = flb_sds_len(kv->val);
        }
    }
    if (ctx->lookup_key_check == NOT_AVAILABLE) {
        flb_error("[%s] lookup key not found", PLUGIN_NAME);
        return -1;
    }
    if (ctx->db_availability == NOT_AVAILABLE) {
        flb_error("[%s] no GeoIp db specified", PLUGIN_NAME);
        return -1;
    }
    return 0;
}

static int cb_modifier_init_apm(struct flb_filter_instance *f_ins,
                                struct flb_config *config,
                                void *data)
{
    struct geoip_ctx *ctx = NULL;
    ctx = flb_malloc(sizeof(struct geoip_ctx));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    if ( geoip_configure(ctx, f_ins) < 0 ){
        flb_free(ctx);
        ctx = NULL;
        return -1;
    }

    flb_filter_set_context(f_ins, ctx);
    return 0;
}

static int get_geo_info(char *ip, MMDB_s *mmdb,msgpack_packer *packer){
    int gai_error, mmdb_error;
    MMDB_lookup_result_s result = MMDB_lookup_string(mmdb, ip, &gai_error, &mmdb_error);
    if (0 != gai_error) {
       flb_debug("Error from getaddrinfo for %s- %s",ip, gai_strerror(gai_error));
       return data_unavailable;
    }
    if (MMDB_SUCCESS != mmdb_error) {
        flb_debug("Got an error from libmaxminddb: %s",MMDB_strerror(mmdb_error));
        return data_unavailable;
    }
    if (!result.found_entry){
        flb_debug("Could not find an entry for this IP address: %s",ip);
        return data_unavailable;
    }
    MMDB_entry_data_s entry_data;
    //Adding City Name
    MMDB_get_value(&result.entry, &entry_data, CITY,NAME,LANGUAGE_ENG,NULL);
    msgpack_pack_str(packer, CITY_LEN);
    msgpack_pack_str_body(packer, CITY, CITY_LEN );
    if (entry_data.has_data){
        msgpack_pack_str(packer, entry_data.data_size);
        msgpack_pack_str_body(packer, entry_data.utf8_string, entry_data.data_size);
    }else{
        msgpack_pack_str(packer, UNKNOWN_LEN);
        msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    }
    //Adding Country Name
    MMDB_get_value(&result.entry, &entry_data,COUNTRY,NAME,LANGUAGE_ENG,NULL);
    msgpack_pack_str(packer, COUNTRY_NAME_LEN);
    msgpack_pack_str_body(packer, COUNTRY_NAME, COUNTRY_NAME_LEN);
    if (entry_data.has_data){
        msgpack_pack_str(packer, entry_data.data_size);
        msgpack_pack_str_body(packer, entry_data.utf8_string, entry_data.data_size);
    }else{
        msgpack_pack_str(packer, UNKNOWN_LEN);
        msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    }
    //Adding Country Code
    MMDB_get_value(&result.entry, &entry_data,COUNTRY,ISO_CODE,NULL);
    msgpack_pack_str(packer,COUNTRY_LEN);
    msgpack_pack_str_body(packer, COUNTRY, COUNTRY_LEN);
    if (entry_data.has_data){
        msgpack_pack_str(packer, entry_data.data_size);
        msgpack_pack_str_body(packer, entry_data.utf8_string, entry_data.data_size);
    }else{
        msgpack_pack_str(packer, UNKNOWN_LEN);
        msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    }
    //Adding Region Name
    MMDB_get_value(&result.entry, &entry_data,REGION,"0",NAME,LANGUAGE_ENG,NULL);
    msgpack_pack_str(packer, REGION_NAME_LEN);
    msgpack_pack_str_body(packer, REGION_NAME, REGION_NAME_LEN);
    if (entry_data.has_data){
        msgpack_pack_str(packer, entry_data.data_size);
        msgpack_pack_str_body(packer, entry_data.utf8_string, entry_data.data_size);
    }else{
        msgpack_pack_str(packer, UNKNOWN_LEN);
        msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    }
    //Adding Region Code
    MMDB_get_value(&result.entry, &entry_data,REGION,"0",ISO_CODE,NULL);
    msgpack_pack_str(packer, REGION_CODE_LEN);
    msgpack_pack_str_body(packer, REGION_CODE, REGION_CODE_LEN);
    if (entry_data.has_data){
        msgpack_pack_str(packer, entry_data.data_size);
        msgpack_pack_str_body(packer, entry_data.utf8_string, entry_data.data_size);
    }else{
        msgpack_pack_str(packer, UNKNOWN_LEN);
        msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    }
    //Adding Latitude info
    MMDB_get_value(&result.entry, &entry_data,LOCATION,LATITUDE,NULL);
    msgpack_pack_str(packer, LATITUDE_LEN);
    msgpack_pack_str_body(packer, LATITUDE,LATITUDE_LEN);
    if (entry_data.has_data){
        msgpack_pack_double(packer, entry_data.double_value);
    }else{
        msgpack_pack_double(packer, -1);
    }
    //Adding Longitude info
    MMDB_get_value(&result.entry, &entry_data,LOCATION,LONGITUDE,NULL);
    msgpack_pack_str(packer, LONGITUDE_LEN);
    msgpack_pack_str_body(packer, LONGITUDE, LONGITUDE_LEN);
    if (entry_data.has_data){
        msgpack_pack_double(packer, entry_data.double_value);
    }else{
        msgpack_pack_double(packer,-1);
    }
    return data_collected;
}
static void add_default_geo_info(msgpack_packer *packer)
{
    //Defaulting City Name to Unknown
    msgpack_pack_str(packer, CITY_LEN);
    msgpack_pack_str_body(packer, CITY, CITY_LEN );
    msgpack_pack_str(packer, UNKNOWN_LEN);
    msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    //Defaulting Countru Name to Unknown
    msgpack_pack_str(packer, COUNTRY_NAME_LEN);
    msgpack_pack_str_body(packer, COUNTRY_NAME, COUNTRY_NAME_LEN);
    msgpack_pack_str(packer, UNKNOWN_LEN);
    msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    //Defaulting Country Name to Unknown
    msgpack_pack_str(packer,COUNTRY_LEN);
    msgpack_pack_str_body(packer, COUNTRY, COUNTRY_LEN);
    msgpack_pack_str(packer, UNKNOWN_LEN);
    msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    //Defaulting Region Name to Unknown
    msgpack_pack_str(packer, REGION_NAME_LEN);
    msgpack_pack_str_body(packer, REGION_NAME, REGION_NAME_LEN);
    msgpack_pack_str(packer, UNKNOWN_LEN);
    msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    //Defaulting Region Code to Unknown
    msgpack_pack_str(packer, REGION_CODE_LEN);
    msgpack_pack_str_body(packer, REGION_CODE, REGION_CODE_LEN);
    msgpack_pack_str(packer, UNKNOWN_LEN);
    msgpack_pack_str_body(packer, UNKNOWN, UNKNOWN_LEN);
    //Defaulting Latitude value to -1
    msgpack_pack_str(packer, LATITUDE_LEN);
    msgpack_pack_str_body(packer, LATITUDE,LATITUDE_LEN);
    msgpack_pack_double(packer,-1);
    //Defaulting Longitude value to -1
    msgpack_pack_str(packer, LONGITUDE_LEN);
    msgpack_pack_str_body(packer, LONGITUDE, LONGITUDE_LEN);
    msgpack_pack_double(packer,-1);
}

static int cb_modifier_filter_apm(const void *data, size_t bytes,
                              const char *tag, int tag_len,
                              void **out_buf, size_t *out_size,
                              struct flb_filter_instance *f_ins,
                              void *context,
                              struct flb_config *config)
{
    struct geoip_ctx *ctx = context;
    //flb_info("ppm %d",ctx->lookup_key->len);
    size_t off = 0;
    int geoinfo_status = remote_addr_not_available;
    int map_num = 0;
    struct flb_time tm;
    msgpack_sbuffer sbuffer;
    msgpack_packer packer;
    msgpack_unpacked unpacked;
    msgpack_object *obj,*old_record_key,*old_record_value;
    msgpack_object_kv *kv;
    msgpack_sbuffer_init(&sbuffer);
    msgpack_packer_init(&packer, &sbuffer, msgpack_sbuffer_write);
    msgpack_unpacked_init(&unpacked);
    while (msgpack_unpack_next(&unpacked, data, bytes, &off) == MSGPACK_UNPACK_SUCCESS) {

        if (unpacked.data.type != MSGPACK_OBJECT_ARRAY) {
            continue;
        }
        flb_time_pop_from_msgpack(&tm, &unpacked, &obj);

        if (obj->type == MSGPACK_OBJECT_MAP) {
            map_num = obj->via.map.size;
        }
        else {
            continue;
        }
        msgpack_pack_array(&packer, 2);
        flb_time_append_to_msgpack(&tm, &packer, 0);
        msgpack_pack_map(&packer, map_num+NEW_ENTRIES);
        kv = obj->via.map.ptr;
        int i=0;
        for (i = 0; i < map_num; i++) {
            old_record_key = &(kv+i)->key;
            old_record_value = &(kv+i)->val;
            if (old_record_key->type == MSGPACK_OBJECT_STR && !strncasecmp(old_record_key->via.str.ptr,ctx->lookup_key,ctx->lookup_key_len))
            {
                char *ip_address = flb_strndup(old_record_value->via.str.ptr, old_record_value->via.str.size);
                //populates record map with geo information
                geoinfo_status = get_geo_info(ip_address,ctx->mmdb,&packer);
                flb_free(ip_address);
            }
            msgpack_pack_object(&packer, (kv + i)->key);
            msgpack_pack_object(&packer, (kv + i)->val);
        }
        if(geoinfo_status==data_unavailable){
            add_default_geo_info(&packer);
        }
    }
    msgpack_unpacked_destroy(&unpacked);
    if(geoinfo_status == remote_addr_not_available)
    {
        flb_error("Lookup key %s not found",ctx->lookup_key);
        msgpack_sbuffer_destroy(&sbuffer);
        return FLB_FILTER_NOTOUCH;
    }
    *out_buf  = sbuffer.data;
    *out_size = sbuffer.size;
    return FLB_FILTER_MODIFIED;

}
static int cb_modifier_exit_apm(void *data, struct flb_config *config)
{
    struct geoip_ctx *ctx = data;

    if (ctx != NULL) {
        MMDB_close(ctx->mmdb);
        flb_free(ctx->mmdb);
        ctx->mmdb = NULL;
        flb_free(ctx->lookup_key);
        ctx->lookup_key = NULL;
        flb_free(ctx);
        ctx = NULL;
    }
    return 0;
}
struct flb_filter_plugin filter_apm_geoip_plugin = {
    .name         = "apm_geoip",
    .description  = "Adds Geo Information",
    .cb_init      = cb_modifier_init_apm,
    .cb_filter    = cb_modifier_filter_apm,
    .cb_exit      = cb_modifier_exit_apm,
    .flags        = 0
};
