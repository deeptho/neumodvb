find_package(Python 3.7 REQUIRED) #3.5 is minimum version
find_package(PythonLibs 3.7 REQUIRED)
include_directories("${PYTHON_INCLUDE_DIR}")

add_library(neumodb SHARED neumodb.cc dbdesc.cc)
add_dependencies(neumodb stackstring neumolmdb schema_generated_files)

pybind11_add_module(pyneumodb SHARED neumodb_pybind.cc ${pybind_srcs})

target_link_libraries(neumodb PUBLIC stackstring neumolmdb ${PYTHON_LIBRARIES} stdc++fs)
target_compile_options(neumodb PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error

target_link_libraries(pyneumodb PUBLIC neumodb)


install (TARGETS neumodb)

add_executable(testneumoupdate testneumoupdate.cc)
add_dependencies(testneumoupdate neumodb dev_generated_files ch_generated_files schema_generated_files)
target_link_libraries(testneumoupdate neumoutil stackstring neumodb schema devdb chdb epgdb recdb ${Boost_PROGRAM_OPTIONS_LIBRARY})

add_executable(neumoupgrade neumoupgrade.cc)
add_dependencies(neumoupgrade neumodb dev_generated_files ch_generated_files schema_generated_files)
target_link_libraries(neumoupgrade stackstring neumodb schema devdb chdb epgdb recdb statdb ${Boost_PROGRAM_OPTIONS_LIBRARY})

add_executable(testencode testencode.cc)
add_dependencies(testencode neumodb schema dev_generated_files ch_generated_files)
target_link_libraries(testencode stackstring devdb chdb schema  neumolmdb neumodb pthread)

add_executable(testserialize testserialize.cc)
target_link_libraries(testserialize stackstring devdb chdb neumodb pthread)


add_executable(testtempdb testtempdb.cc)
add_dependencies(testtempdb devdb chdb neumodb schema dev_generated_files ch_generated_files)
target_link_libraries(testtempdb stackstring devdb chdb schema neumodb )


pybind11_add_module(pydeser deserialize_pybind.cc  )
#add_dependencies(pydeser schema_generated_files )
target_compile_options(pydeser PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error
target_link_libraries(pydeser PRIVATE neumoutil stackstring pyschemadb pyneumodb neumodb)



add_subdirectory(devdb)
add_subdirectory(chdb)
add_subdirectory(statdb)
add_subdirectory(epgdb)
add_subdirectory(recdb)
add_subdirectory(schema)

include_directories(
    # The location of the headers before installation
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>/../pybind11/include
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # The location of the headers after installation
    $<INSTALL_INTERFACE:include>
)

install (TARGETS pyneumodb  DESTINATION ${Python_SITEARCH}/${PROJECT_NAME})
install(TARGETS neumoupgrade  DESTINATION bin)
