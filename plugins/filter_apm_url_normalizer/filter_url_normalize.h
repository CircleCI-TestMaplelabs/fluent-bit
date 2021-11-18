#define AVAILABLE 1
#define NOT_AVAILABLE 0
#define NEW_ENTRIES 1
#define RETRIES 2
#define GLOBALRETRIES 100
#define DELAYINSEC 2
#define LOOKUPKEY "url_path_key"
#define DEFAULT "Unknown"
#define DEFAULT_LEN 7
#define NORMALIZED_PATH "normalized_path"
#define NORMALIZED_PATH_LEN 15
#define PORTKEY "port"

enum url_normalize_status {
    url_path_not_available,
    url_path_available,
    data_collected,
    unable_to_connect
};
struct urlnormalizer_ctx {
    char *lookup_key;
    char *port;
    int port_key_len;
    int port_key_check;
    int lookup_key_len; 
    int lookup_key_check;
    int sock;
    struct flb_filter_instance *ins;
};
