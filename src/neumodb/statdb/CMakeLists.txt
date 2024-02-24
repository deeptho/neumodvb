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
set(GEN stats enums statdb_db) #basename of generated files

list(TRANSFORM GEN APPEND .cc OUTPUT_VARIABLE gensrc)

list(TRANSFORM GEN APPEND .h OUTPUT_VARIABLE genhdr)


list(TRANSFORM GEN APPEND _pybind.cc OUTPUT_VARIABLE pybind_srcs)

set(TEMPLATE structs enums db)
list(TRANSFORM TEMPLATE PREPEND ${PROJECT_SOURCE_DIR}/neumodb/templates/)
list(TRANSFORM TEMPLATE APPEND .h OUTPUT_VARIABLE genhtemplate)
list(TRANSFORM TEMPLATE APPEND .cc OUTPUT_VARIABLE gencctemplate)
list(TRANSFORM TEMPLATE APPEND _pybind.cc OUTPUT_VARIABLE genpybindtemplate)

list(APPEND gensrc statdb_keys.cc)
list(APPEND genhdr statdb_keys.h)
list(APPEND genhtemplate ${PROJECT_SOURCE_DIR}/neumodb/templates/db_keys.h)
list(APPEND gencctemplate ${PROJECT_SOURCE_DIR}/neumodb/templates/db_keys.cc)

set(gendeps ${genhtemplate} ${gencctemplate} ${genpybindtemplate} ${CMAKE_CURRENT_SOURCE_DIR}/dbdefs.py )
set(extgendeps ch_generated_files dev_generated_files epg_generated_files rec_generated_files)

set(codegen ${PROJECT_SOURCE_DIR}/neumodb/neumodb.py ${PROJECT_SOURCE_DIR}/neumodb/generators.py  )
list(TRANSFORM TEMPLATE PREPEND ${PROJECT_SOURCE_DIR}/neumodb/templates/)
list(TRANSFORM TEMPLATE APPEND .h OUTPUT_VARIABLE genhtemplate)


get_filename_component(DB ${CMAKE_CURRENT_SOURCE_DIR} NAME)
add_custom_command(
    OUTPUT ${genhdr} ${gensrc} ${pybind_srcs}
    COMMAND ${PYTHON_EXECUTABLE}  ${PROJECT_SOURCE_DIR}/neumodb/neumodb.py --db=${DB}
    DEPENDS ${gendeps} ${extgendeps} ${codegen}
    COMMENT "Generating code for neumodb statdb."
)

set_source_files_properties(
  ${gensrc} ${genhdr} ${pybind_srcs}
  PROPERTIES GENERATED TRUE
  )

add_custom_target(stat_generated_files DEPENDS ${gensrc} ${genhdr} ${pybind_srcs})

add_library(statdb SHARED ${gensrc} statdb_extra.cc statdb_upgrade.cc)
add_dependencies(statdb stat_generated_files dev_generated_files ch_generated_files epg_generated_files rec_generated_files)

# -fsized-deallocation needed to prevent operator delete error
target_compile_options(statdb PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error

pybind11_add_module(pystatdb statdb_pybind.cc statdb_vector_pybind.cc ${pybind_srcs})

target_compile_options(pystatdb PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error
target_link_libraries(statdb PUBLIC stackstring neumolmdb neumoutil devdb chdb neumodb schema fmt::fmt uuid)
target_link_libraries(pystatdb PRIVATE statdb pyneumodb)

list(TRANSFORM genhdr PREPEND ${CMAKE_CURRENT_BINARY_DIR}/  OUTPUT_VARIABLE precomp)
target_precompile_headers(statdb PRIVATE [[<stdint.h>]] statdb_extra.h ${precomp})
target_precompile_headers(pystatdb PRIVATE [[<stdint.h>]] statdb_extra.h ${precomp})


install (TARGETS pystatdb DESTINATION ${Python_STDLIB}/${PROJECT_NAME})
install (TARGETS statdb  DESTINATION ${CMAKE_INSTALL_LIBDIR})
