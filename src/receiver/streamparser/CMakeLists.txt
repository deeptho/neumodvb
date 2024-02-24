#add_compile_options(-O2) #applies to subdirs as well

add_library(streamparser STATIC  events.cc pes.cc  packetstream.cc psi.cc section.cc
  streamtime.cc streamwriter.cc dvbtext.cc freesat_decode.cc opentv_string_decoder.cc
  si_state.cc sidebug.cc huffman_opentv_multi.cc huffman_opentv_single.cc)
add_dependencies(streamparser recdb rec_generated_files)
target_link_libraries(streamparser PUBLIC ${Boost_CONTEXT_LIBRARY})
target_link_libraries(streamparser PRIVATE neumoutil)
#target_compile_options(streamparser PRIVATE $<$<CONFIG:DEBUG>:-O2>)

add_executable(huffman_generator huffman_generator.cc huffman_opentv_data.cc)
target_link_libraries(huffman_generator PRIVATE neumoutil)

install (TARGETS streamparser DESTINATION ${CMAKE_INSTALL_LIBDIR})


include_directories(
    # The location of the headers before installation
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # The location of the headers after installation
    $<INSTALL_INTERFACE:include>
)
