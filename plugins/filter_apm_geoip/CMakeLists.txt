# libmaxminddb
# libmaxminddb
include(libmxmnd.cmake)
# libmaxminddb headers
include_directories(${LIBMAXMINDDB_DEST}/include/)
# libmaxminddb headers
#include_directories(libmaxminddb/include/)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(src filter_geoip.c)
FLB_PLUGIN(filter_apm_geoip "${src}" "")