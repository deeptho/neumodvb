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
set(GEN common sat mux service tune_state enums chdb_db) #basename of generated files

list(TRANSFORM GEN APPEND .cc OUTPUT_VARIABLE gensrc)

list(TRANSFORM GEN APPEND .h OUTPUT_VARIABLE genhdr)


list(TRANSFORM GEN APPEND _pybind.cc OUTPUT_VARIABLE pybind_srcs)

set(TEMPLATE structs enums db)
list(TRANSFORM TEMPLATE PREPEND ${PROJECT_SOURCE_DIR}/neumodb/templates/)
list(TRANSFORM TEMPLATE APPEND .h OUTPUT_VARIABLE genhtemplate)
list(TRANSFORM TEMPLATE APPEND .cc OUTPUT_VARIABLE gencctemplate)
list(TRANSFORM TEMPLATE APPEND _pybind.cc OUTPUT_VARIABLE genpybindtemplate)

list(APPEND gensrc chdb_keys.cc)
list(APPEND genhdr chdb_keys.h)
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
    COMMENT "Generating code for neumodb chdb."
)

set_source_files_properties(
  ${gensrc} ${genhdr} ${pybind_srcs}
  PROPERTIES GENERATED TRUE
  )

add_custom_target(ch_generated_files ALL DEPENDS ${gensrc} ${genhdr} ${pybind_srcs})

add_library(chdb SHARED ${gensrc} chdb_extra.cc chdb_clean.cc mux_find.cc chdb_upgrade.cc browse_history.cc minsnr.cc lang_codes.cc)
add_dependencies(chdb dev_generated_files ch_generated_files stat_generated_files epg_generated_files rec_generated_files)

#-fbracket-depth=1024 to work around bug in lang version 12.0.1  https://bugs.llvm.org/show_bug.cgi?id=50178
# -fsized-deallocation needed to prevent operator delete error
target_compile_options(chdb PUBLIC -fPIC  -fsized-deallocation PRIVATE -fvisibility=hidden -fbracket-depth=1024)

pybind11_add_module(pychdb chdb_pybind.cc chdb_vector_pybind.cc ${pybind_srcs})

target_compile_options(pychdb PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error
target_link_libraries(chdb PUBLIC devdb stackstring neumolmdb neumoutil neumodb schema uuid)

target_link_libraries(pychdb PRIVATE chdb pyneumodb pyschemadb)

list(TRANSFORM genhdr PREPEND ${CMAKE_CURRENT_BINARY_DIR}/  OUTPUT_VARIABLE precomp)
target_precompile_headers(chdb PRIVATE [[<stdint.h>]] chdb_extra.h  ${precomp})
target_precompile_headers(pychdb PRIVATE [[<stdint.h>]] chdb_extra.h ${precomp})

install (TARGETS pychdb  DESTINATION ${Python_STDLIB}/${PROJECT_NAME})
install (TARGETS chdb  DESTINATION ${CMAKE_INSTALL_LIBDIR})
