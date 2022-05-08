cmake_minimum_required(VERSION 3.12.1)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,defs -Wl,--no-undefined -fuse-ld=gold")

project(neumo VERSION 0.6.1)
set(PROJECT_AUTHOR "Deep Thought")
set(PROJECT_AUTHOR_EMAIL "deeptho@gmail.com")
set(PROJECT_URL "https://github.com/deeptho/neumodvb")
set(PROJECT_DESCRIPTION "DVB Settop box and dx-program for linux")
set(PROJECT_LICENCSE "GPL V2")
set(PROJECT_KEYWORDS "DVB-S2, DVB-T, DVB-C, spectrum scan, blindscan")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
SET(CMAKE_BUILD_RPATH "$ORIGIN/.")

# libneumowxsvg
include(cmake/neumowxsvg.cmake)

execute_process(COMMAND  lsb_release  -c -s OUTPUT_VARIABLE RELEASE)
string(STRIP ${RELEASE} RELEASE)
message(STATUS "xxx=/${RELEASE}/")
if ("${RELEASE}" STREQUAL "focal")
  message(STATUS "Ancient Unbuntu detected - using clang 17")
  set (CMAKE_CXX_STANDARD 17)
else()
  set (CMAKE_CXX_STANDARD 20)
  message(STATUS "Modern OS detected - using clang 20")
endif()

#add_compile_options (-fsanitize=address -fPIC -fsized-deallocation)
# -DHAS_UNCAUGHT_EXCEPTIONS=1 for date module



#add_compile_options(-DBOOST_DISABLE_ASSERTS -fdebug-prefix-map=${CMAKE_SOURCE_DIR}=. -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)

add_compile_options (-fPIC -fsized-deallocation -DHAS_UNCAUGHT_EXCEPTIONS=1 -D DATA_DIR=${CMAKE_SOURCE_DIR}/data)
#add_link_options( -lasan)

find_package(Boost 1.69 REQUIRED regex context program_options)
include_directories(${BOOST_INCLUDE_DIRS})
include_directories(date/include)
include_directories(xformat/include)

find_package(PkgConfig REQUIRED)

find_package(Python COMPONENTS Interpreter Development)


pkg_check_modules(GTK3 REQUIRED gtk+-3.0)



set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_BUILD_TYPE Release)
#set(CMAKE_BUILD_TYPE RelWithDebInfo)

SET(USE_BOOST_LOCALE false)
SET(TEST_CHDB false)

#set(BUILD_SHARED_LIBS ON)
#option( USE_SYSTEM_TZ_DB "Use the operating system's timezone database" OFF )
#option( BUILD_TZ_LIB "build/install of TZ library" ON )
#option( BUILD_SHARED_LIBS  "Build a shared version of library" ON )
add_subdirectory(date)

option(BUILD_TESTING "Build the testing tree." OFF)

add_subdirectory(xformat)
add_subdirectory(pybind11)
add_subdirectory(gui)
add_subdirectory(src)
add_subdirectory(neumolmdb)
add_subdirectory(config)

#-Wl,-z,defs -fuse-ld=gold

#add_definitions(-DTODO)



function(print_directory_variables dir)
    # Dump variables:
    get_property(_variableNames DIRECTORY ${dir} PROPERTY VARIABLES)
    list (SORT _variableNames)
    foreach (_variableName ${_variableNames})
        get_directory_property(_variableValue DIRECTORY ${dir} DEFINITION ${_variableName})
        message(STATUS "DIR ${dir}: ${_variableName}=${_variableValue}")
    endforeach()
endfunction(print_directory_variables)

#print_directory_variables(gui)
