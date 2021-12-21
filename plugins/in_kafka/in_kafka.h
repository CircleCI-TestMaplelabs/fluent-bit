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
    
    // SASL Auth
    flb_sds_t sasl_mechanism;
    flb_sds_t sasl_username;
    flb_sds_t sasl_password;
    
    //SSL
    flb_sds_t security_protocol;
    flb_sds_t ssl_protocol;
    flb_sds_t ssl_ca_location;
    flb_sds_t ssl_ca_pem;
    flb_sds_t ssl_ca_certificate_stores;
    flb_sds_t ssl_crl_location;
    flb_sds_t ssl_engine_location;
    flb_sds_t ssl_engine_id;
    flb_sds_t enable_ssl_certificate_verification;
    flb_sds_t ssl_endpoint_identification_algorithm;
    flb_sds_t ssl_certificate_verify_cb;
    flb_sds_t ssl_certificate_location;
    flb_sds_t ssl_certificate_pem;
    flb_sds_t ssl_key;
    flb_sds_t ssl_key_location;
    flb_sds_t ssl_key_password;
    flb_sds_t ssl_enabled_protocols;
    flb_sds_t ssl_cipher_suites;
    flb_sds_t ssl_curves_list;
    flb_sds_t ssl_sigalgs_list;

    //Kerberos
    flb_sds_t sasl_kerberos_keytab;
    flb_sds_t sasl_kerberos_service_name;
    flb_sds_t sasl_kerberos_principal;
    flb_sds_t sasl_kerberos_kinit_cmd;
    flb_sds_t sasl_kerberos_min_time_before_relogin;

    //oauth
    flb_sds_t sasl_oauthbearer_config;
    flb_sds_t enable_sasl_oauthbearer_unsecure_jwt;
    flb_sds_t sasl_oauthbearer_method;
    flb_sds_t sasl_oauthbearer_client_id;
    flb_sds_t sasl_oauthbearer_client_secret;
    flb_sds_t sasl_oauthbearer_scope;
    flb_sds_t sasl_oauthbearer_extensions;
    flb_sds_t sasl_oauthbearer_token_endpoint_url;
    /* Internal */
    rd_kafka_conf_t *conf;
    rd_kafka_t *rk;
    rd_kafka_topic_partition_list_t *subscription;
    
    int num_metrics_to_emit;
    struct flb_input_instance *ins;
};
#endif