# libmaxminddb
# libmaxminddb
option(BUILD_TESTING "" OFF)
set(CMAKE_C_FLAGS "-std=gnu99 ${CMAKE_C_FLAGS}")
# add_subdirectory(libmaxminddb EXCLUDE_FROM_ALL)
include_directories(libmaxminddb/include/)
set(src filter_geoip.c)
# FLB_PLUGIN(filter_apm_geoip "${src}" "maxminddb")
FLB_PLUGIN(filter_apm_geoip "${src}" "")
