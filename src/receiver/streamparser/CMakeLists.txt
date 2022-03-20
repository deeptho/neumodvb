set (CMAKE_CXX_STANDARD 17)

add_library(streamparser STATIC  events.cc pes.cc  packetstream.cc psi.cc streamtime.cc streamwriter.cc
  dvbtext.cc freesat_decode.cc opentv_string_decoder.cc si_state.cc sidebug.cc)
add_dependencies(streamparser recdb rec_generated_files xformat)
target_link_libraries(streamparser PUBLIC ${Boost_CONTEXT_LIBRARY} xformat)
target_link_libraries(streamparser PRIVATE neumoutil)

install (TARGETS streamparser DESTINATION lib64)


include_directories(
    # The location of the headers before installation
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # The location of the headers after installation
    $<INSTALL_INTERFACE:include>
)
