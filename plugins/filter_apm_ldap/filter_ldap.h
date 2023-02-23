#define AVAILABLE 1
#define NOT_AVAILABLE 0
#define NEW_ENTRIES 15
#define RETRIES 4
#define GLOBALRETRIES 100
#define DELAYINSEC 2
#define LOOKUPKEY "ldap_log_key"
#define DEFAULT "Unknown"
#define DEFAULT_LEN 7
#define PORTKEY "port"
#define SOCKET_BUF_SIZE 1024
#define END_OF_MESSAGE "~eom~"
#define BUFFER_SIZE_RESPONSE "buffersizeldap:"
#define BUFFER_RESPONSE_SIZE 16

enum ldap_status {
    ldap_path_not_available,
    ldap_path_available,
    data_collected,
    data_collection_failed,
    unable_to_connect
};
struct ldap_ctx {
    char *lookup_key;
    char *port;
    int port_key_len;
    int port_key_check;
    int lookup_key_len; 
    int lookup_key_check;
    int sock;
    struct flb_filter_instance *ins;
};