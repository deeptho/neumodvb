find_package(Python 3.7 REQUIRED) #3.5 is minimum version
find_package(PythonLibs 3.7 REQUIRED)
include_directories("${PYTHON_INCLUDE_DIR}")

add_library(neumodb SHARED neumodb.cc dbdesc.cc neumodb_pybind.cc deserialize_pybind.cc)
add_dependencies(neumodb stackstring neumolmdb schema_generated_files)

target_link_libraries(neumodb PUBLIC stackstring ${PYTHON_LIBRARIES} stdc++fs)
target_compile_options(neumodb PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error

install (TARGETS neumodb)

add_executable(testneumoupdate testneumoupdate.cc)
add_dependencies(testneumoupdate neumodb ch_generated_files schema_generated_files)
target_link_libraries(testneumoupdate neumoutil stackstring neumodb schema chdb epgdb recdb ${Boost_PROGRAM_OPTIONS_LIBRARY})

add_executable(neumoupgrade neumoupgrade.cc)
add_dependencies(neumoupgrade neumodb ch_generated_files schema_generated_files)
target_link_libraries(neumoupgrade stackstring neumodb schema chdb epgdb recdb statdb ${Boost_PROGRAM_OPTIONS_LIBRARY})

add_executable(testencode testencode.cc)
add_dependencies(testencode neumodb schema ch_generated_files)
target_link_libraries(testencode stackstring chdb schema  neumolmdb neumodb pthread)

add_executable(testserialize testserialize.cc)
target_link_libraries(testserialize stackstring chdb neumodb pthread)


add_executable(testtempdb testtempdb.cc)
add_dependencies(testtempdb chdb neumodb schema ch_generated_files)
target_link_libraries(testtempdb stackstring chdb schema neumodb )

pybind11_add_module(pydeser deserialize_pybind.cc)
target_compile_options(pydeser PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error
target_link_libraries(pydeser PRIVATE neumoutil stackstring schema)



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

install(TARGETS neumoupgrade  DESTINATION bin)
