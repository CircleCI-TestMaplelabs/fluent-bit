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

#ifndef FLB_OUT_KAFKA_REST_H
#define FLB_OUT_KAFKA_REST_H

#define FLB_KAFKA_TIME_KEY   "@timestamp"
#define FLB_KAFKA_TIME_KEYF  "%Y-%m-%dT%H:%M:%S"
#define FLB_KAFKA_TAG_KEY    "_flb-key"

#define SOURCE_LOG_KEY "source_log"
#define CONVERSION_FACTOR_DAY_TO_SEC 86400
#define CONVERSION_FACTOR_HOUR_TO_SEC 3600
#define CONVERSION_FACTOR_MINUTE_TO_SEC 60
#define CUSTOM_FAILED_CHUNK_LOGGING_UNIT_DAY 'd'
#define CUSTOM_FAILED_CHUNK_LOGGING_UNIT_HOUR 'h'
#define CUSTOM_FAILED_CHUNK_LOGGING_UNIT_MINUTE 'm'
#define CUSTOM_FAILED_CHUNK_LOGGING_UNIT_SEC 's'

struct flb_kafka_rest {

    long next_scheduled_chunk_logging_time;
    long num_chunks_logged_per_observation_period;

    long failed_max_chunks_to_be_collected_in_a_period;

    /* Received logging duration configuration; No actual config data will be stored; Used only for local metadata key relationship with user-config since its enforced by fluent */
    char *failed_chunk_logging_interval_custom;

    /* Representing logging duration in sec. */
    long failed_chunk_logging_interval_in_sec;

    /* Kafka specifics */
    long partition;
    char *topic;
    char *token;
    size_t token_len;
    int message_key_len;
    char *message_key;

    /* HTTP Auth */
    char *http_user;
    char *http_passwd;

    /* time key */
    int time_key_len;
    char *time_key;

    /* time key format */
    int time_key_format_len;
    char *time_key_format;

    /* include_tag_key */
    int include_tag_key;
    int tag_key_len;
    char *tag_key;

    /* HTTP URI */
    char uri[256];
    char *path;

    /* Upstream connection to the backend server */
    struct flb_upstream *u;

    /* Plugin instance */
    struct flb_output_instance *ins;

    /* Avro http header*/
    int avro_http_header;
};


#endif
