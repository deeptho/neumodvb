find_package (Log4cxx)

# Add a custom target that produces version.cpp, plus
# a dummy output that's not actually produced, in order
# to force version.cmake to always be re-run before the build
add_custom_target(version_h BYPRODUCTS  ${CMAKE_CURRENT_BINARY_DIR}/version.h
    COMMAND ${CMAKE_COMMAND} -P
    ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake)

add_library(neumoutil SHARED util.cc logger.cc identification.cc ${CMAKE_CURRENT_BINARY_DIR}/version.h)
add_dependencies(neumoutil stackstring)

target_compile_options(neumoutil PUBLIC -fPIC)
target_link_libraries(neumoutil PUBLIC dl log4cxx stackstring stdc++fs pthread fmt::fmt)

install (TARGETS neumoutil)

include_directories(
    # The location of the headers before installation
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # The location of the headers after installation
    $<INSTALL_INTERFACE:include>
    )
