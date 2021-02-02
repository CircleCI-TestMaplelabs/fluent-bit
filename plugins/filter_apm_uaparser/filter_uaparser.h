#define AVAILABLE 1
#define NOT_AVAILABLE 0
#define NEW_ENTRIES 9
#define RETRIES 2
#define GLOBALRETRIES 100
#define DELAYINSEC 2
#define LOOKUPKEY "agent_key"
#define DEFAULT "Unknown"
#define DEFAULT_LEN 7
#define BROWSER_NAME "browser_name"
#define BROWSER_NAME_LEN 12
#define BROWSER_VER "browser_version"
#define BROWSER_VER_LEN 15
#define BROWSER "browser"
#define BROWSER_LEN 7
#define OS_NAME "OS_name"
#define OS_NAME_LEN 7
#define OS_VERSION "OS_version"
#define OS_VERSION_LEN 10
#define OS "OS"
#define OS_LEN 2
#define DEVICE "device"
#define DEVICE_LEN 6
#define DEVICE_BRAND "device_brand"
#define DEVICE_BRAND_LEN 12
#define DEVICE_MODEL "device_model"
#define DEVICE_MODEL_LEN 12

#define PORTKEY "port"

enum ua_parser_status {
    agent_not_available,
    agent_available,
    data_collected,
    unable_to_connect,
    add_default
};
struct uaparser_ctx {
    char *lookup_key;
    char *port;
    int port_key_len;
    int port_key_check;
    int lookup_key_len; 
    int lookup_key_check;
    int sock;
    struct flb_filter_instance *ins;
};
