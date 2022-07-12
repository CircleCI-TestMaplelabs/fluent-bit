
#define LOOKUP_KEY_PATH "mapping_path"

#define SFAPM_PROJECTNAME_LABEL "SFAPM_PROJECTNAME_LABEL"
#define SFAPM_APPNAME_LABEL "SFAPM_APPNAME_LABEL"

#define DEFAULT_PROJECTNAME_LABEL "snappyflow/projectname"
#define DEFAULT_APPNAME_LABEL "snappyflow/appname"
#define COMPONENT_NAME_LABEL "snappyflow/component"
#define UA_PARSER_LABEL "snappyflow/ua_parser"
#define GEO_INFO_LABEL "snappyflow/geo_info"
#define EXCLUDE_CONTAINER_LABEL "snappyflow/exclude-containers-log"
#define POD_NAME_IDENT_KEY "pod_name"

#define POD_NAME_IDENT_KEY_LEN 8
#define DEFAULT_PROJECTNAME_LABEL_LEN 22
#define DEFAULT_APPNAME_LABEL_LEN 18
#define COMPONENT_NAME_LABEL_LEN 20
#define UA_PARSER_LABEL_LEN 20
#define GEO_INFO_LABEL_LEN 19
#define EXCLUDE_CONTAINER_LABEL_LEN 33
#define POD_LABEL_MAPPING_FILE_LEN 21

// enum kubernetes_labels_status {
//     unable_to_connect,
//     data_collection_in_progress,
//     data_collection_successful,
//     data_collection_failed,
// };

struct kubernetes_labels_ctx {
    char* pod_label_details_as_msgpack;
    size_t len_of_pod_label_details_as_msgpack;
    char* appname_labe1;
    char* projname_labe1;
    struct flb_filter_instance *ins;
};