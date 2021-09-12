cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

# The following must be set BEFORE doing project() or enable_language().
if (NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type defined; defaulting to 'Debug'")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
            "The type of build. Possible values are: Debug, Release, RelWithDebInfo and MinSizeRel.")
endif ()

# ::-------------------------------------------------------------------------::

project(neumolmdb VERSION 1.0.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

message(STATUS "The host system is: ${CMAKE_HOST_SYSTEM}.")
message(STATUS "Project base dir is: ${CMAKE_SOURCE_DIR}")

# To force pthreads if necessary: set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
add_compile_options($<$<CONFIG:DEBUG>:-O2>) #applies to subdirs as well
add_library(neumolmdb
        neumolmdb.h
        mdb.c
        midl.c
        midl.h
        )

target_compile_definitions(neumolmdb
        PUBLIC -DMDB_FDATASYNC_WORKS)

target_compile_options(neumolmdb
        PUBLIC -W -Wall -Wno-unused-parameter -Wbad-function-cast -Wuninitialized -fPIC
        )

add_executable(mdb_stat
        mdb_stat.c
        )

target_link_libraries(neumolmdb
        PUBLIC Threads::Threads
        )

target_link_libraries(mdb_stat
        PRIVATE neumolmdb
        PRIVATE Threads::Threads
        )

if (GPERFTOOLS_FOUND)
    target_link_libraries(mdb_stat
            PRIVATE ${GPERFTOOLS_TCMALLOC_AND_PROFILER}
            )
endif ()

install (TARGETS neumolmdb  DESTINATION lib64)
