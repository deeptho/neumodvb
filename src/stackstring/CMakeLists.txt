add_library(stackstring SHARED stackstring.cc neumotime.cc)
target_compile_options(stackstring PRIVATE $<$<CONFIG:DEBUG>:-O2>)

if(USE_BOOST_LOCALE)
  SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -DUSE_BOOST_LOCALE")
target_link_libraries(stackstring PUBLIC neumoutil boost_locale)
endif()

target_compile_options(stackstring PRIVATE -fPIC)

if(TEST_STACKSTRING)
add_executable(testss testss.cc)
target_link_libraries(testss stackstring neumoutil)
install (TARGETS testss DESTINATION bin)
endif()

install (TARGETS stackstring)
