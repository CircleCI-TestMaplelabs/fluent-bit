#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_error.h>
#include <fluent-bit/flb_pack.h>
#include <fluent-bit/flb_random.h>
#include <msgpack.h>
#include <fluent-bit/flb_config_map.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "rdkafka.h"

#include "in_kafka.h"

char *string_copy(const char *from, char *to)
{
    char *p;
    for (p = to; (*p = *from) != '\0'; ++p, ++from)
    {
        ;
    }
    return to;
}

static int create_kafka_consumer_ins(struct flb_in_kafka_config *ctx)
{
    char errstr[512];
    ctx->rk = rd_kafka_new(RD_KAFKA_CONSUMER, ctx->conf, errstr, sizeof(errstr));
    if (!ctx->rk)
    {
        flb_plg_error(ctx->ins, "Failed to create new consumer: %s", errstr);
        return -1;
    }
    return 0;
}
static int subscribe_to_topics(struct flb_in_kafka_config *ctx)
{
    rd_kafka_resp_err_t err;
    err = rd_kafka_subscribe(ctx->rk, ctx->subscription);
    if (err)
    {
        flb_plg_error(ctx->ins, "Failed to subscribe to %d topics: %s",
                      ctx->subscription->cnt, rd_kafka_err2str(err));
        return -1;
    }

    return 0;
}
static int set_config_kafka_client(struct flb_in_kafka_config *ctx)
{
    char errstr[512];        
    char *topics[MAX_TOPICS]; 
    int topic_cnt;            
    int i;

    ctx->conf = rd_kafka_conf_new();

    if (rd_kafka_conf_set(ctx->conf, "bootstrap.servers", ctx->broker_list, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
    {
        flb_plg_error(ctx->ins, "Error in configuring bootstrap servers: %s\n", errstr);
        rd_kafka_conf_destroy(ctx->conf);
        return -1;
    }

    if (rd_kafka_conf_set(ctx->conf, "group.id", ctx->group_id, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
    {
        flb_plg_error(ctx->ins, "Error in configuring group id: %s", errstr);
        rd_kafka_conf_destroy(ctx->conf);
        return -1;
    }

    if (rd_kafka_conf_set(ctx->conf, "auto.offset.reset", "earliest", errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
    {
        flb_plg_error(ctx->ins, "Error in configuring auto.offset.reset property to earliest: %s\n", errstr);
        rd_kafka_conf_destroy(ctx->conf);
        return -1;
    }

    if (ctx->sasl_mechanism)
    {
        if (rd_kafka_conf_set(ctx->conf, "sasl.mechanism", ctx->sasl_mechanism, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
        {
            flb_plg_error(ctx->ins, "Error in configuring sasl.mechanism property: %s\n", errstr);
            rd_kafka_conf_destroy(ctx->conf);
            return -1;
        }
    }

    if (ctx->security_protocol)
    {
        if (rd_kafka_conf_set(ctx->conf, "security.protocol", ctx->security_protocol, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
        {
            flb_plg_error(ctx->ins, "Error in configuring security.protocol property: %s\n", errstr);
            rd_kafka_conf_destroy(ctx->conf);
            return -1;
        }
    }
    if (ctx->ssl_truststore_location)
    {
        if (rd_kafka_conf_set(ctx->conf, "ssl.truststore.location", ctx->ssl_truststore_location, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
        {
            flb_plg_error(ctx->ins, "Error in configuring ssl.truststore.location property: %s\n", errstr);
            rd_kafka_conf_destroy(ctx->conf);
            return -1;
        }
    }
    if (ctx->ssl_truststore_password)
    {
        if (rd_kafka_conf_set(ctx->conf, "ssl.truststore.password", ctx->ssl_truststore_password, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
        {
            flb_plg_error(ctx->ins, "Error in configuring ssl.truststore.password property: %s\n", errstr);
            rd_kafka_conf_destroy(ctx->conf);
            return -1;
        }
    }
    if (ctx->ssl_enabled_protocols)
    {
        if (rd_kafka_conf_set(ctx->conf, "ssl.enabled.protocols", ctx->ssl_enabled_protocols , errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
        {
            flb_plg_error(ctx->ins, "Error in configuring ssl.enabled.protocols property: %s\n", errstr);
            rd_kafka_conf_destroy(ctx->conf);
            return -1;
        }
    }
    if (ctx->ssl_protocol)
    {
        if (rd_kafka_conf_set(ctx->conf, "ssl.protocol", ctx->ssl_protocol, errstr,
                          sizeof(errstr)) != RD_KAFKA_CONF_OK)
        {
            flb_plg_error(ctx->ins, "Error in configuring in ssl.protocol property %s\n", errstr);
            rd_kafka_conf_destroy(ctx->conf);
            return -1;
        }
    }
    char *topic = strtok(ctx->topics, ",");
    while (topic != NULL)
    {
        if (topic_cnt >= MAX_TOPICS)
        {
            flb_plg_error(ctx->ins, "Max topics allowed is 1000");
            return -1;
        }
        topics[topic_cnt] = flb_malloc(strlen(topic) + 1);
        string_copy(topic, topics[topic_cnt++]);
        topic = strtok(NULL, ",");
    }

    ctx->subscription = rd_kafka_topic_partition_list_new(topic_cnt);
    for (i = 0; i < topic_cnt; i++)
    {
        rd_kafka_topic_partition_list_add(ctx->subscription, topics[i],
                                          /* the partition is ignored
                                           * by subscribe() */
                                          RD_KAFKA_PARTITION_UA);
        flb_free(topics[i]);
    }
    int ret = create_kafka_consumer_ins(ctx);
    if (ret == -1)
    {
        for (i = 1; i < FAILURE_RETRIES; i++)
        {
            ret = create_kafka_consumer_ins(ctx);
            if (ret == 0)
            {
                break;
            }
        }
        ctx->conf = NULL;
        rd_kafka_topic_partition_list_destroy(ctx->subscription);
        flb_plg_error(ctx->ins, "Gave up creating consumer instance after %d retires",
                      FAILURE_RETRIES);
        return -1;
    }
    ctx->conf = NULL;
    rd_kafka_poll_set_consumer(ctx->rk);
    ret = subscribe_to_topics(ctx);
    if (ret == -1)
    {
        for (i = 1; i < FAILURE_RETRIES; i++)
        {
            ret = subscribe_to_topics(ctx);
            if (ret == 0)
            {
                break;
            }
        }
        rd_kafka_topic_partition_list_destroy(ctx->subscription);
        rd_kafka_destroy(ctx->rk);
        flb_plg_error(ctx->ins, "Gave up subscribing to topics after %d retires",
                      FAILURE_RETRIES);
        return -1;
    }
    rd_kafka_topic_partition_list_destroy(ctx->subscription);
    return 0;
}
static int in_kafka_collect(struct flb_input_instance *ins,
                            struct flb_config *config, void *in_context)
{

    msgpack_packer mp_pck;
    msgpack_sbuffer mp_sbuf;
    struct flb_in_kafka_config *ctx = in_context;

    time_t endwait;
    time_t start = time(NULL);
    time_t seconds = ctx->interval_sec; // end loop after this time has elapsed

    endwait = start + seconds;
    int wait_timeout = (ctx->interval_sec * 1000) / 10;
    while (start < endwait)
    {
        rd_kafka_message_t *rkm;
        start = time(NULL);
        rkm = rd_kafka_consumer_poll(ctx->rk, wait_timeout);
        if (!rkm)
            continue; 
        if (rkm->err)
        {
            flb_plg_error(ctx->ins, "Consumer error: %s\n",
                          rd_kafka_message_errstr(rkm));
            rd_kafka_message_destroy(rkm);
            continue;
        }
        flb_plg_debug(ctx->ins, "Message on %s [%" PRId32 "] at offset %" PRId64 " message_key: %s message: %s",
                      rd_kafka_topic_name(rkm->rkt), rkm->partition,
                      rkm->offset, rkm->key, rkm->payload);
        const char *consumed_topic = rd_kafka_topic_name(rkm->rkt);
        if ((ctx->message_key) && (rkm->key != ctx->message_key))
        {
            flb_plg_error(ctx->ins, "Expected message key(%s) not found in consumed data for topic %s", ctx->message_key, rd_kafka_topic_name(rkm->rkt));
            continue;
        } 
        else if (!rkm->payload)
        {
            flb_plg_error(ctx->ins, "Empty payload identified in consumed data for topic %s", rd_kafka_topic_name(rkm->rkt));
            continue;
        }

        //Init Buffer

        msgpack_sbuffer_init(&mp_sbuf);
        msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);

        msgpack_pack_array(&mp_pck, 2);
        flb_pack_time_now(&mp_pck);
        msgpack_pack_map(&mp_pck, ctx->num_metrics_to_emit);

        if (ctx->include_topic_name_in_record)
        {
            msgpack_pack_str(&mp_pck, 10);
            msgpack_pack_str_body(&mp_pck, DEFAULT_TOPIC_NAME, 10);
            msgpack_pack_str(&mp_pck, strlen(consumed_topic));
            msgpack_pack_str_body(&mp_pck, consumed_topic, strlen(consumed_topic));
        }
        msgpack_pack_str(&mp_pck, 7);
        msgpack_pack_str_body(&mp_pck, DEFAULT_MESSAGE, 7);
        msgpack_pack_str(&mp_pck, strlen(rkm->payload));
        msgpack_pack_str_body(&mp_pck, rkm->payload, strlen(rkm->payload));

            
        flb_input_chunk_append_raw(ins, NULL, 0, mp_sbuf.data, mp_sbuf.size);
        msgpack_sbuffer_destroy(&mp_sbuf);
        rd_kafka_message_destroy(rkm);
    }

    return 0;
}

/* Set plugin configuration */

static int in_kafka_init(struct flb_input_instance *in,
                         struct flb_config *config, void *data)
{
    int ret = -1;
    struct flb_in_kafka_config *ctx = NULL;

    /* Allocate space for the configuration */
    ctx = flb_malloc(sizeof(struct flb_in_kafka_config));
    if (!ctx)
    {
        return -1;
    }
    ctx->ins = in;

    /* Initialize head config */

    ret = flb_input_config_map_set(in, (void *)ctx);
    if (ret == -1)
    {
        return -1;
    }

    flb_plg_info(ctx->ins, "broker=%s topic=%s message_key=%s include_topic=%s interval_sec=%d",
                 ctx->broker_list, ctx->topics, ctx->message_key, ctx->include_topic_name_in_record ? "true" : "false", ctx->interval_sec);

    if (!ctx->topics)
    {
        flb_plg_error(ctx->ins, "Missing list of topics to consume upon");
        return -1;
    }
    if (ctx->include_topic_name_in_record)
        ctx->num_metrics_to_emit = 2;
    else 
        ctx->num_metrics_to_emit = 1;
    int client_config_ret = set_config_kafka_client(ctx);
    if (client_config_ret < 0)
    {
        flb_plg_error(ctx->ins, "could not set consumer client");
        return -1;
    }
    flb_input_set_context(in, ctx);

    ret = flb_input_set_collector_time(in,
                                       in_kafka_collect,
                                       ctx->interval_sec,
                                       ctx->interval_nsec, config);
    if (ret < 0)
    {
        flb_plg_error(ctx->ins, "could not set collector for kafka input plugin");
        flb_free(ctx);
        return -1;
    }

    return 0;
}
/* Configuration properties map */
static struct flb_config_map config_map[] = {
    {FLB_CONFIG_MAP_STR, "broker_list", DEFAULT_BROKER_LIST,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, broker_list),
     "Comma-seperated list of brokerName:brokerPort. Default localhost:9092"},
    {FLB_CONFIG_MAP_STR, "topics", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, topics),
     "comma-seperated list of topics."},
    {FLB_CONFIG_MAP_STR, "group_id", DEFAULT_GROUP_ID,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, group_id),
     "Optional GroupID"},
    {FLB_CONFIG_MAP_STR, "sasl_mechanism", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, sasl_mechanism),
     "kafka consumer property sasl_mechanism"},
    {FLB_CONFIG_MAP_STR, "security_protocol", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, security_protocol),
     "kafka consumer property: security.protocol"},
    {FLB_CONFIG_MAP_STR, "sasl_jaas_config", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, sasl_jaas_config),
     "kafka consumer property: sasl.jaas.config"},
    {FLB_CONFIG_MAP_STR, "ssl_truststore_location", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, ssl_truststore_location),
     "kafka consumer property: ssl.truststore.location"},
    {FLB_CONFIG_MAP_STR, "ssl_truststore_password", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, ssl_truststore_password),
     "kafka consumer property: ssl.truststore.password"},
    {FLB_CONFIG_MAP_STR, "ssl_enabled_protocols", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, ssl_enabled_protocols),
     "kafka consumer property: ssl.enabled.protocols"},
    {FLB_CONFIG_MAP_STR, "ssl_protocol", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, ssl_protocol),
     "kafka consumer property: ssl.protocol"},
    {FLB_CONFIG_MAP_STR, "message_key", NULL,
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, message_key),
     "Message Key to consumer from. Default: message"},
    {FLB_CONFIG_MAP_INT, "interval_sec", "1",
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, interval_sec),
     "polling interval(in sec). Default 1"},
    {FLB_CONFIG_MAP_INT, "interval_nsec", "1",
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, interval_nsec),
     "polling interval(in nano sec)."},
    {FLB_CONFIG_MAP_BOOL, "include_topic_name_in_record", "true",
     0, FLB_TRUE, offsetof(struct flb_in_kafka_config, include_topic_name_in_record),
     "Include topic name as a part of record. Default: false."},
    {0}};

static int in_kafka_exit(void *data, struct flb_config *config)
{
    (void)*config;
    struct flb_in_kafka_config *ctx = data;
    if (ctx->conf)
    {
        flb_free(ctx->conf);
    }
    if (ctx->rk)
    {
        flb_free(ctx->rk); 
    }
    if (ctx->subscription)
    {
        flb_free(ctx->subscription); 
    }
    if (!ctx)
    {
        return 0;
    }
    flb_free(ctx);
    return 0;
}

struct flb_input_plugin in_kafka_plugin = {
    .name = "kafka",
    .description = "Kafka",
    .cb_init = in_kafka_init,
    .cb_pre_run = NULL,
    .cb_collect = in_kafka_collect,
    .cb_flush_buf = NULL,
    .config_map = config_map,
    .cb_exit = in_kafka_exit
};