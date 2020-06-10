#ifndef FLB_FILTER_APM_GEOIP_H
#define FLB_FILTER_APM_GEOIP_H
#define AVAILABLE 1
#define NOT_AVAILABLE 0
#define NEW_ENTRIES 7
#define CITY "city"
#define CITY_LEN 4
#define COUNTRY "country"
#define COUNTRY_LEN 7
#define ISO_CODE "iso_code"
#define COUNTRY_NAME "country_name"
#define COUNTRY_NAME_LEN 12
#define REGION_CODE "region_code"
#define REGION_NAME "region_name"
#define REGION_NAME_LEN 11
#define REGION_CODE_LEN 11
#define REGION "subdivisions"
#define LOCATION "location"
#define LOCATION_LEN 8
#define LATITUDE "latitude"
#define LATITUDE_LEN 8
#define LONGITUDE "longitude"
#define LONGITUDE_LEN 9
#define NAME "names"
#define LANGUAGE_ENG "en"
#define UNKNOWN "-unknown-"
#define UNKNOWN_LEN 9
enum geo_info_status {
    remote_addr_not_available,
    remote_addr_available,
    data_collected,
    data_unavailable
};
#include <maxminddb.h>
struct geoip_ctx {
    MMDB_s *mmdb;
    char *lookup_key;
    int lookup_key_len; 
    int lookup_key_check;
    int db_availability;
    struct flb_filter_instance *ins;
};


#endif /* FLB_FILTER_APM_GEOIP_H */
