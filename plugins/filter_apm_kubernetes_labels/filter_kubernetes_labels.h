
#define LOOKUP_KEY_PATH "mapping_path"

#define SFAPM_PROJECTNAME_LABEL "SFAPM_PROJECTNAME_LABEL"
#define SFAPM_APPNAME_LABEL "SFAPM_APPNAME_LABEL"
#define SFAPM_PROJECT_NAME "SFAPM_PROJECT_NAME"
#define SFAPM_APP_NAME "SFAPM_APP_NAME"
#define MONITOR_ALL_PODS "MONITOR_ALL_PODS"

#define DEFAULT_PROJECTNAME_LABEL "snappyflow/projectname"
#define DEFAULT_APPNAME_LABEL "snappyflow/appname"
#define DEFAULT_PROJECTNAME "project"
#define DEFAULT_APPNAME "app"
#define COMPONENT_NAME_LABEL "snappyflow/component"
#define UA_PARSER_LABEL "snappyflow/ua_parser"
#define GEO_INFO_LABEL "snappyflow/geo_info"
#define EXCLUDE_CONTAINER_LABEL "snappyflow/exclude-containers-log"
#define POD_NAME_IDENT_KEY "pod_name"

#define DEFAULT_MONITOR_PODS_LOGS false

#define POD_NAME_IDENT_KEY_LEN 8
#define DEFAULT_PROJECTNAME_LABEL_LEN 22
#define DEFAULT_APPNAME_LABEL_LEN 18
#define COMPONENT_NAME_LABEL_LEN 20
#define UA_PARSER_LABEL_LEN 20
#define GEO_INFO_LABEL_LEN 19
#define EXCLUDE_CONTAINER_LABEL_LEN 33
#define POD_LABEL_MAPPING_FILE_LEN 21

#define MAX_JSMN_TOKEN_SIZE 213200
// enum kubernetes_labels_status {
//     unable_to_connect,
//     data_collection_in_progress,
//     data_collection_successful,
//     data_collection_failed,
// };
#include <fluent-bit/flb_jsmn.h>

struct kubernetes_labels_ctx {
    jsmntok_t *jsmn_tokens;
    int jsmn_ret;
    char* json_buf;
    char* appname_labe1;
    char* appname;
    char* projname_labe1;
    char* projname;
    bool monitor_pods_logs;
    struct flb_filter_instance *ins;
};
