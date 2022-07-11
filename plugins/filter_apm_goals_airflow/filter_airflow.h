#define AVAILABLE 1
#define NOT_AVAILABLE 0
#define NEW_ENTRIES 0
#define RETRIES 3
#define LOOKUPKEY "message_key"
#define SOCKET_BUF_SIZE 1024
#define PORTKEY "port"
#define FIELD_FILE "file"
#define FIELD_FILE_LEN 4
#define SNAPPY_LOG_SEPERATOR "SNAPPY_LOG_SEPERATOR"
#define SNAPPY_LOG_SEPERATOR_LEN 20
#define NUM_1 1
#define FACTOR_512KB 524288
#define LITERAL_HASH "#"
#define LITERAL_DISCARD "discard"
#define LITERAL_DISCARD_LEN 7
#define LITERAL_KEEP "keep"

enum message_collection_status {
    unable_to_connect,
    data_collection_in_progress,
    data_collection_successful,
    data_collection_failed,
};
struct multiline_collection_ctx {
    char *lookup_key;
    char *port;
    int port_key_len;
    int port_key_check;
    int lookup_key_len; 
    int lookup_key_check;
    int sock;
    struct flb_filter_instance *ins;
    flb_sds_t emitter_name;                 /* emitter input plugin name */
    flb_sds_t emitter_storage_type;         /* emitter storage type */
    size_t emitter_mem_buf_limit;           /* Emitter buffer limit */
    struct flb_input_instance *ins_emitter; /* emitter input plugin instance */
    struct flb_config *config;              /* Fluent Bit context */
};
