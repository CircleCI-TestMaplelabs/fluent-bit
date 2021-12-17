# Build librdkafka
FLB_OPTION(RDKAFKA_BUILD_STATIC    On)
FLB_OPTION(RDKAFKA_BUILD_EXAMPLES Off)
FLB_OPTION(RDKAFKA_BUILD_TESTS    Off)
FLB_OPTION(ENABLE_LZ4_EXT         Off)

add_subdirectory(librdkafka-1.6.0 EXCLUDE_FROM_ALL)
include_directories(librdkafka-1.6.0/src/)
set(src
  in_kafka.c)

FLB_PLUGIN(in_kafka "${src}" "rdkafka")
