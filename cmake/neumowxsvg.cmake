include(ExternalProject)

set(NEUMOWXSVG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/neumowxsvg)
set(NEUMOWXSVG_BIN ${CMAKE_CURRENT_BINARY_DIR}/../build_ext/neumowxsvg)
set(NEUMOWXSVG_STATIC_LIB ${NEUMOWXSVG_BIN}/lib/libwxsvg.a)
set(NEUMOWXSVG_INCLUDES ${NEUMOWXSVG_BIN}/include)

file(MAKE_DIRECTORY ${NEUMOWXSVG_INCLUDES})

ExternalProject_Add(
    libneumowxsvg
    PREFIX ${NEUMOWXSVG_BIN}
    SOURCE_DIR ${NEUMOWXSVG_DIR}
    DOWNLOAD_COMMAND cd ${NEUMOWXSVG_DIR} &&  ${NEUMOWXSVG_DIR}/autogen.sh
    CONFIGURE_COMMAND ${NEUMOWXSVG_DIR}/configure --srcdir=${NEUMOWXSVG_DIR} --prefix=${NEUMOWXSVG_BIN} --enable-static=yes --disable-shared
    BUILD_COMMAND make CXX=g++\ -fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.\ -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS ${NEUMOWXSVG_STATIC_LIB}
)

add_library(neumowxsvg STATIC IMPORTED GLOBAL)

add_dependencies(neumowxsvg libneumowxsvg)

set_target_properties(neumowxsvg PROPERTIES IMPORTED_LOCATION ${NEUMOWXSVG_STATIC_LIB})
set_target_properties(neumowxsvg PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${NEUMOWXSVG_INCLUDES})
