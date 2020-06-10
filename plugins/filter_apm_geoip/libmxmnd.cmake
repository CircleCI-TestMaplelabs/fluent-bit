include(ExternalProject)
add_library(libmaxminddb STATIC IMPORTED GLOBAL)

# Global Settings
set(LIBMAXMINDDB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/libmaxminddb)
set(LIBMAXMINDDB_DEST ${CMAKE_CURRENT_BINARY_DIR}/libmaxminddb)
ExternalProject_Add(maxminddb
  BUILD_IN_SOURCE TRUE
  EXCLUDE_FROM_ALL TRUE
  SOURCE_DIR ${LIBMAXMINDDB_SRC}
  INSTALL_DIR ${LIBMAXMINDDB_DEST}
  CONFIGURE_COMMAND ${LIBMAXMINDDB_SRC}/configure --with-pic --disable-shared --enable-static --prefix=${LIBMAXMINDDB_DEST}
  BUILD_COMMAND $(MAKE) 
  INSTALL_COMMAND $(MAKE) install)

if(MSVC)
  add_dependencies(libmaxminddb maxminddb)
  set(LIBMAXMINDDB_STATIC_LIB "${LIBMAXMINDDB_DEST}/lib/libmaxminddb.lib")
else()
  add_dependencies(libmaxminddb maxminddb)
  set(LIBMAXMINDDB_STATIC_LIB "${LIBMAXMINDDB_DEST}/lib/libmaxminddb.a")
endif()
set_property(TARGET libmaxminddb PROPERTY POSITION_INDEPENDENT_CODE ON)
set_target_properties(libmaxminddb PROPERTIES IMPORTED_LOCATION "${LIBMAXMINDDB_STATIC_LIB}")
