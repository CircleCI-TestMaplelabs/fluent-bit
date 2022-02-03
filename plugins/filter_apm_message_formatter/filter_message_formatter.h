#define AVAILABLE 1
#define NOT_AVAILABLE 0
#define NEW_ENTRIES 1
#define RETRIES 2
#define GLOBALRETRIES 100
#define DELAYINSEC 2
#define LOOKUPKEY "message_key"
#define PORTKEY "port"
#define MESSAGE "message"
#define MESSAGE_KEY_SIZE 7
#define SOCKET_BUF_SIZE 1024
enum message_formatter_status {
    message_field_not_available,
    message_field_available,
    data_collected,
    unable_to_connect
};
struct message_formatter_ctx {
    char *lookup_key;
    char *port;
    int port_key_len;
    int port_key_check;
    int lookup_key_len; 
    int lookup_key_check;
    int sock;
    struct flb_filter_instance *ins;
};
