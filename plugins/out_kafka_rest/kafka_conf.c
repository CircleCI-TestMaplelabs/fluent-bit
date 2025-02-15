/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019-2021 The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *

 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fluent-bit/flb_output_plugin.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_utils.h>
#include <time.h>

#include "kafka.h"
#include "kafka_conf.h"

struct flb_kafka_rest *flb_kr_conf_create(struct flb_output_instance *ins,
                                          struct flb_config *config)
{
    long part;
    int io_flags = 0;
    const char *tmp;
    char *endptr;
    struct flb_upstream *upstream;
    struct flb_kafka_rest *ctx;
    int ret;

    /* Allocate context */
    ctx = flb_calloc(1, sizeof(struct flb_kafka_rest));
    if (!ctx) {
        flb_errno();
        return NULL;
    }
    ctx->ins = ins;

    ret = flb_output_config_map_set(ins, (void *) ctx);
    if (ret == -1) {
        flb_free(ctx);
        return NULL;
    }

    /* Get network configuration */
    flb_output_net_default("127.0.0.1", 8082, ins);

    /* use TLS ? */
    if (ins->use_tls == FLB_TRUE) {
        io_flags = FLB_IO_TLS;
    }
    else {
        io_flags = FLB_IO_TCP;
    }

    if (ins->host.ipv6 == FLB_TRUE) {
        io_flags |= FLB_IO_IPV6;
    }

    /* Prepare an upstream handler */
    upstream = flb_upstream_create(config,
                                   ins->host.name,
                                   ins->host.port,
                                   io_flags,
                                   ins->tls);
    if (!upstream) {
        flb_plg_error(ctx->ins, "cannot create Upstream context");
        flb_kr_conf_destroy(ctx);
        return NULL;
    }
    ctx->u = upstream;
    flb_output_upstream_set(ctx->u, ins);

    flb_output_upstream_set(ctx->u, ins);

    /* HTTP Auth */
    tmp = flb_output_get_property("http_user", ins);
    if (tmp) {
        ctx->http_user = flb_strdup(tmp);

        tmp = flb_output_get_property("http_passwd", ins);
        if (tmp) {
            ctx->http_passwd = flb_strdup(tmp);
        }
        else {
            ctx->http_passwd = flb_strdup("");
        }
    }

    /* Time Key */
    tmp = flb_output_get_property("time_key", ins);
    if (tmp) {
        ctx->time_key = flb_strdup(tmp);
        ctx->time_key_len = strlen(tmp);
    }
    else {
        ctx->time_key = flb_strdup(FLB_KAFKA_TIME_KEY);
        ctx->time_key_len = sizeof(FLB_KAFKA_TIME_KEY) - 1;
    }

    /* Time Key Format */
    tmp = flb_output_get_property("time_key_format", ins);
    if (tmp) {
        ctx->time_key_format = flb_strdup(tmp);
        ctx->time_key_format_len = strlen(tmp);
    }
    else {
        ctx->time_key_format = flb_strdup(FLB_KAFKA_TIME_KEYF);
        ctx->time_key_format_len = sizeof(FLB_KAFKA_TIME_KEYF) - 1;
    }

    tmp = flb_output_get_property("failed_chunk_logging_interval", ins);
    if ((tmp) && (strlen(tmp) != 0)) {
        flb_sds_t formatted_logging_duration = flb_sds_create_size(strlen(tmp)-1);
        if (formatted_logging_duration)
        {
            flb_sds_t formatted_logging_duration_tmp = NULL;
            formatted_logging_duration_tmp = flb_sds_copy(formatted_logging_duration, tmp, strlen(tmp)-1);
            if (formatted_logging_duration_tmp)
            {
                if (formatted_logging_duration_tmp != formatted_logging_duration)
                {
                    formatted_logging_duration = formatted_logging_duration_tmp;
                }
                if (tmp[strlen(tmp)-1] == CUSTOM_FAILED_CHUNK_LOGGING_UNIT_DAY)
                {
                        ctx->failed_chunk_logging_interval_in_sec = atoi(formatted_logging_duration) * CONVERSION_FACTOR_DAY_TO_SEC;
                }
                else if (tmp[strlen(tmp)-1] == CUSTOM_FAILED_CHUNK_LOGGING_UNIT_HOUR)
                {
                        ctx->failed_chunk_logging_interval_in_sec = atoi(formatted_logging_duration) * CONVERSION_FACTOR_HOUR_TO_SEC;
                }
                else if (tmp[strlen(tmp)-1] == CUSTOM_FAILED_CHUNK_LOGGING_UNIT_MINUTE)
                {
                        ctx->failed_chunk_logging_interval_in_sec = atoi(formatted_logging_duration) * CONVERSION_FACTOR_MINUTE_TO_SEC;
                }
                else if (tmp[strlen(tmp)-1] == CUSTOM_FAILED_CHUNK_LOGGING_UNIT_SEC)
                {
                        ctx->failed_chunk_logging_interval_in_sec = atoi(formatted_logging_duration) ;
                }
            }
            flb_sds_destroy(formatted_logging_duration);
        }
    }

    if (ctx->failed_chunk_logging_interval_in_sec == 0)
    {
       ctx->failed_chunk_logging_interval_in_sec = CONVERSION_FACTOR_DAY_TO_SEC;
    }

    /* Setting current time as the next scheduled time*/
    time(&ctx->next_scheduled_chunk_logging_time);


    flb_plg_info(ctx->ins, "failed_chunk_logging_interval_in_sec is set to %d", ctx->failed_chunk_logging_interval_in_sec);

    tmp = flb_output_get_property("failed_max_chunks_to_be_collected_in_a_period", ins);
    if (tmp) {
        ctx->failed_max_chunks_to_be_collected_in_a_period = atoi(tmp);
    }

    if (ctx->failed_max_chunks_to_be_collected_in_a_period == 0)
    {
       ctx->failed_max_chunks_to_be_collected_in_a_period = 1;
    }

    /* Include Tag key */
    tmp = flb_output_get_property("include_tag_key", ins);
    if (tmp) {
        ctx->include_tag_key = flb_utils_bool(tmp);
    }
    else {
        ctx->include_tag_key = FLB_FALSE;
    }

    /* Tag Key */
    if (ctx->include_tag_key == FLB_TRUE) {
        tmp = flb_output_get_property("tag_key", ins);
        if (tmp) {
            ctx->tag_key = flb_strdup(tmp);
            ctx->tag_key_len = strlen(tmp);
            if (tmp[0] != '_') {
                flb_plg_warn(ctx->ins, "consider use a tag_key "
                             "that starts with '_'");
            }
        }
        else {
            ctx->tag_key = flb_strdup(FLB_KAFKA_TAG_KEY);
            ctx->tag_key_len = sizeof(FLB_KAFKA_TAG_KEY) - 1;
        }
    }

    /* Kafka: partition */
    tmp = flb_output_get_property("partition", ins);
    if (tmp) {
         errno = 0;
         part = strtol(tmp, &endptr, 10);
         if ((errno == ERANGE && (part == LONG_MAX || part == LONG_MIN))
             || (errno != 0 && part == 0)) {
             flb_plg_error(ctx->ins, "invalid partition number");
         }

         if (endptr == tmp) {
             flb_plg_error(ctx->ins, "invalid partition number");
         }
         ctx->partition = part;
    }
    else {
        ctx->partition = -1;
    }

    /* Kafka: topic */
    tmp = flb_output_get_property("topic", ins);
    if (tmp) {
        ctx->topic = flb_strdup(tmp);
    }
    else {
        ctx->topic = flb_strdup("fluent-bit");
    }

    /* Kafka: token */
    tmp = flb_output_get_property("token", ins);
    if (tmp) {
        ctx->token = flb_strdup(tmp);
        ctx->token_len = strlen(tmp);
    }
    else {
        ctx->token = flb_strdup("");
        ctx->token_len = 0;
    }

    /* Set partition based on topic */
    tmp = flb_output_get_property("path", ins);
    if (tmp) {
        ctx->path = flb_strdup(tmp);
        snprintf(ctx->uri, sizeof(ctx->uri) - 1, "/%s/topics/%s", ctx->path, ctx->topic);
    } else {
        ctx->path = NULL;
        snprintf(ctx->uri, sizeof(ctx->uri) - 1, "/topics/%s", ctx->topic);
    }

    /* Kafka: message key */
    tmp = flb_output_get_property("message_key", ins);
    if (tmp) {
        ctx->message_key = flb_strdup(tmp);
        ctx->message_key_len = strlen(tmp);
    }
    else {
        ctx->message_key = NULL;
        ctx->message_key_len = 0;
    }

    return ctx;
}

int flb_kr_conf_destroy(struct flb_kafka_rest *ctx)
{
    flb_free(ctx->topic);
    flb_free(ctx->http_user);
    flb_free(ctx->http_passwd);

    flb_free(ctx->time_key);
    flb_free(ctx->time_key_format);
    if (ctx->path) {
        flb_free(ctx->path);
    }
    flb_free(ctx->token);
    if (ctx->include_tag_key) {
        flb_free(ctx->tag_key);
    }

    if (ctx->message_key) {
        flb_free(ctx->message_key);
    }

    flb_upstream_destroy(ctx->u);
    flb_free(ctx);

    return 0;
}
