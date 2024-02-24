find_package (Python3 COMPONENTS Interpreter Development)

include_directories(
    # The location of the headers before installation
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../pybind11/include>
    # The location of the headers after installation
    $<INSTALL_INTERFACE:include>
)
set(GEN epg enums epgdb_db) #basename of generated files

list(TRANSFORM GEN APPEND .cc OUTPUT_VARIABLE gensrc)

list(TRANSFORM GEN APPEND .h OUTPUT_VARIABLE genhdr)


list(TRANSFORM GEN APPEND _pybind.cc OUTPUT_VARIABLE pybind_srcs)

set(TEMPLATE structs enums db)
list(TRANSFORM TEMPLATE PREPEND ${PROJECT_SOURCE_DIR}/neumodb/templates/)
list(TRANSFORM TEMPLATE APPEND .h OUTPUT_VARIABLE genhtemplate)
list(TRANSFORM TEMPLATE APPEND .cc OUTPUT_VARIABLE gencctemplate)
list(TRANSFORM TEMPLATE APPEND _pybind.cc OUTPUT_VARIABLE genpybindtemplate)

list(APPEND gensrc epgdb_keys.cc)
list(APPEND genhdr epgdb_keys.h)
list(APPEND genhtemplate ${PROJECT_SOURCE_DIR}/neumodb/templates/db_keys.h)
list(APPEND gencctemplate ${PROJECT_SOURCE_DIR}/neumodb/templates/db_keys.cc)

set(gendeps ${genhtemplate} ${gencctemplate} ${genpybindtemplate} ${CMAKE_CURRENT_SOURCE_DIR}/dbdefs.py )

set(codegen ${PROJECT_SOURCE_DIR}/neumodb/neumodb.py ${PROJECT_SOURCE_DIR}/neumodb/generators.py  )
list(TRANSFORM TEMPLATE PREPEND ${PROJECT_SOURCE_DIR}/neumodb/templates/)
list(TRANSFORM TEMPLATE APPEND .h OUTPUT_VARIABLE genhtemplate)


get_filename_component(DB ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_custom_command(
    OUTPUT ${genhdr} ${gensrc} ${pybind_srcs}
    COMMAND ${PYTHON_EXECUTABLE}  ${PROJECT_SOURCE_DIR}/neumodb/neumodb.py --db=${DB}
    DEPENDS ${gendeps} ${codegen}
    COMMENT "Generating code for neumodb epgdb."
)

set_source_files_properties(
  ${gensrc} ${genhdr} ${pybind_srcs}
  PROPERTIES GENERATED TRUE
  )

add_custom_target(epg_generated_files ALL DEPENDS ${gensrc} ${genhdr} ${pybind_srcs})

add_dependencies(epg_generated_files dev_generated_files ch_generated_files)

add_library(epgdb SHARED ${gensrc} epgdb_extra.cc epgdb_upgrade.cc)
add_dependencies(epgdb epg_generated_files dev_generated_files ch_generated_files stat_generated_files)

# -fsized-deallocation needed to prevent operator delete error
target_compile_options(epgdb PUBLIC -fPIC -fsized-deallocation)

pybind11_add_module(pyepgdb epgdb_pybind.cc ${pybind_srcs})

target_compile_options(pyepgdb PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error
target_link_libraries(epgdb PUBLIC stackstring neumolmdb neumoutil neumodb schema fmt::fmt uuid)

target_link_libraries(pyepgdb PRIVATE epgdb chdb pyneumodb)

list(TRANSFORM genhdr PREPEND ${CMAKE_CURRENT_BINARY_DIR}/  OUTPUT_VARIABLE precomp)
target_precompile_headers(epgdb PRIVATE [[<stdint.h>]] epgdb_extra.h ${precomp})
target_precompile_headers(pyepgdb PRIVATE [[<stdint.h>]] epgdb_extra.h ${precomp})

install (TARGETS pyepgdb DESTINATION ${Python_STDLIB}/${PROJECT_NAME})
install (TARGETS epgdb  DESTINATION ${CMAKE_INSTALL_LIBDIR})
