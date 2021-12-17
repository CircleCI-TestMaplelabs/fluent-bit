#ifndef FLB_IN_KAFKA_H
#define FLB_IN_KAFKA_H

#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_utils.h>

#include <monkey/monkey.h>

#define DEFAULT_INTERVAL_SEC 1
#define DEFAULT_INTERVAL_NSEC 0
#define DEFAULT_BROKER_LIST "localhost:9090"
#define DEFAULT_MESSAGE "message"
#define DEFAULT_TOPIC_NAME "_topicName"
#define DEFAULT_GROUP_ID "1577723"
#define FAILURE_RETRIES 2
#define MAX_TOPICS 1000
struct flb_in_kafka_config
{
    /* Config properties */
    int interval_sec;
    int interval_nsec;
    flb_sds_t broker_list; // comma-seperated list of brokerName:brokerPort
    flb_sds_t topics;
    flb_sds_t group_id;
    flb_sds_t message_key;
    flb_sds_t include_topic_name_in_record;
    flb_sds_t sasl_mechanism;
    flb_sds_t security_protocol;
    flb_sds_t sasl_jaas_config;
    flb_sds_t ssl_truststore_location;
    flb_sds_t ssl_truststore_password;
    flb_sds_t ssl_enabled_protocols;
    flb_sds_t ssl_protocol;
    /* Internal */
    rd_kafka_conf_t *conf;
    rd_kafka_t *rk;
    rd_kafka_topic_partition_list_t *subscription;
    
    int num_metrics_to_emit;
    struct flb_input_instance *ins;
};
#endif