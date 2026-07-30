cmake_minimum_required(VERSION 3.15)
#SET (CMAKE_SKIP_INSTALL_RULES ON)
option(USE_CLANG "build application with clang" ON) # ON is the default

if(USE_CLANG)
  SET (CMAKE_CXX_COMPILER clang++)
  SET (CMAKE_C_COMPILER clang)
endif(USE_CLANG)


project(neumo LANGUAGES C CXX VERSION 1.7 )
set(PROJECT_AUTHOR "Deep Thought")
set(PROJECT_AUTHOR_EMAIL "deeptho@gmail.com")
set(PROJECT_URL "https://github.com/deeptho/neumodvb")
set(PROJECT_DESCRIPTION "DVB Settop box and dx-program for linux")
set(PROJECT_LICENCSE "GPL V2")
set(PROJECT_KEYWORDS "DVB-S2, DVB-T, DVB-C, spectrum scan, blindscan")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
SET(CMAKE_BUILD_RPATH "$ORIGIN/.")

set(CMAKE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,defs -Wl,--no-undefined")

include(CheckLinkerFlag)
check_linker_flag(CXX -fuse-ld=mold  LINKER_HAS_MOLD)
if(${LINKER_HAS_MOLD})
  string(APPEND CMAKE_LINKER_FLAGS " -fuse-ld=mold")
else()
  check_linker_flag(CXX -fuse-ld=gold  LINKER_HAS_GOLD)
  if(${LINKER_HAS_GOLD})
    string(APPEND CMAKE_LINKER_FLAGS " -fuse-ld=gold")
  endif()
endif()

# libneumowxsvg
include(cmake/neumowxsvg.cmake)
include(FetchContent)

set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_BINARY_DIR}/../build_ext)

FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  #GIT_TAG f5e54359df4c26b6230fc61d38aa294581393084
  #GIT_TAG 40626af88bd7df9a5fb80be7b25ac85b122d6c21
  GIT_TAG 12.1.0
  )

FetchContent_Declare(nanobind
  GIT_REPOSITORY https://github.com/wjakob/nanobind.git
  #GIT_TAG f5e54359df4c26b6230fc61d38aa294581393084
  #GIT_TAG 40626af88bd7df9a5fb80be7b25ac85b122d6c21
  GIT_TAG v2.12.0
  )

# Prevent fmt from being installed system-wide
set(FMT_INSTALL OFF CACHE BOOL "Disable installation of fmt" FORCE)

execute_process(COMMAND  lsb_release  -c -s OUTPUT_VARIABLE RELEASE)
string(STRIP "${RELEASE}" RELEASE)
message(STATUS "xxx=/${RELEASE}/")
if ("${RELEASE}" STREQUAL "focal")
  message(STATUS "Ancient Unbuntu detected - install clang-14 first!!!")
  set (CMAKE_CXX_STANDARD 23)
else()
  set (CMAKE_CXX_STANDARD 23)
  message(STATUS "Modern OS detected - using clang 20")
endif()

#add_compile_options (-fsanitize=address -fPIC -fsized-deallocation -DBOOST_USE_ASAN)
# -DHAS_UNCAUGHT_EXCEPTIONS=1 for date module

set (CMAKE_EXPORT_COMPILE_COMMANDS 1)

#add_compile_options(-DBOOST_DISABLE_ASSERTS -fdebug-prefix-map=${CMAKE_SOURCE_DIR}=. -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)

add_compile_options (
#  $<$<CONFIG:DEBUG>:-Og>
  -fPIC -fsized-deallocation -DHAS_UNCAUGHT_EXCEPTIONS=1 -D DATA_DIR=${CMAKE_SOURCE_DIR}/data)
#add_link_options( -lasan)

#cmake_policy(SET CMP0167 NEW)
if (NOT ("${RELEASE}" STREQUAL "focal") AND NOT ("${RELEASE}" STREQUAL "noble"))
  cmake_policy(VERSION 3.30..3.99)
endif()
find_package(Boost 1.69 REQUIRED regex context program_options)
include_directories(${BOOST_INCLUDE_DIRS})

cmake_policy(SET CMP0148 OLD)

if (CMAKE_VERSION VERSION_LESS 3.18)
set(DEV_MODULE Development)
else()
set(DEV_MODULE Development.Module)
endif()
find_package(Python COMPONENTS Interpreter Development.Module Development.Embed REQUIRED)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel"
↪"RelWithDebInfo")
endif()

find_package(PkgConfig REQUIRED)


pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_BUILD_TYPE Release)
#set(CMAKE_BUILD_TYPE RelWithDebInfo)

#not working:  SET(CMAKE_DEBUG_POSTFIX "" CACHE STRING "add a postfix, usually d on windows")
FetchContent_MakeAvailable(fmt)
FetchContent_MakeAvailable(nanobind)

include_directories(${fmt_SOURCE_DIR}/include/)

SET(USE_BOOST_LOCALE false)
SET(TEST_CHDB false)

#set(BUILD_SHARED_LIBS ON)
option( USE_SYSTEM_TZ_DB "Use the operating system's timezone database" ON )
#option( BUILD_TZ_LIB "build/install of TZ library" ON )
#option( BUILD_SHARED_LIBS  "Build a shared version of library" ON )

option(BUILD_TESTING "Build the testing tree." OFF)

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
