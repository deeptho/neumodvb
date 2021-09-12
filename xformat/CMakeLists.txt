cmake_minimum_required(VERSION 3.1.3)
project(xformat CXX)
enable_testing()

include(CTest)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY tests)
set(CMAKE_CXX_STANDARD 14)

if(MINGW)
	add_definitions(-DMINGW_HAS_SECURE_API)
endif()

if(NOT MSVC)
	add_definitions(-Wall -Wconversion -Wsign-compare)
	set(CMAKE_CXX_EXTENSIONS OFF)
else()
	foreach(flagvar CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
	    CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
		string(REGEX REPLACE "/MD" "/MT" ${flagvar} "${${flagvar}}")
		string(REGEX REPLACE "/MDd" "/MTd" ${flagvar} "${${flagvar}}")
	endforeach()
endif()

include_directories(include)

file(GLOB xformat_srcs src/*.cc src/*.cpp)
file(GLOB tests_srcs tests/*.cc)

add_library(xformat STATIC ${xformat_srcs})

if(BUILD_TESTING)
	add_executable(run ${tests_srcs})
	target_link_libraries(run xformat)
	add_test(testall tests/run)
endif()
