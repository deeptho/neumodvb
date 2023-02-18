
IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  SET(CMAKE_INSTALL_PREFIX /usr CACHE PATH <comment> FORCE)
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

project(neumodvb VERSION 0.6.1)

# documentation build as an option and set it to ON by default
option (BUILD_DOCS "Build documentation" false)
option (SANITIZE "Sanitize" true)

if (SANITIZE)
#add_compile_options(--save-temps)
add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address -lasan)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fsanitize=address -lasan -fno-omit-frame-pointer")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fsanitize=address -lasan -fno-omit-frame-pointer")
endif (SANITIZE)

#ensure that absolute filenames do not appear in debugging strings and in generated debug data in the sharedlibs
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__FILENAME__=='\"$(subst ${CMAKE_SOURCE_DIR}/,,$(abspath $<))\"'")

#add_compile_options(-DNDEBUG)

#Debug options for assert:
#   assert_fail_stop: program will suspend so that debugger can be attached
#   assert_fail_log: program will log error and continue
#   __assert_fail (i.e., comment out fully): regular assert
#add_compile_options(-D__assert_fail=assert_fail_stop)

if (BUILD_DOCS)
#only build docs when asked
find_package(Doxygen)
# check if Doxygen is installed
find_package(Doxygen)

if (DOXYGEN_FOUND)
    # set input and output files
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/../docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # request to configure the file
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message("Doxygen build started")

    # note the option ALL which allows to build the docs together with the application
    add_custom_target( doc_doxygen ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM )
else (DOXYGEN_FOUND)
  message("Doxygen need to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)
endif (BUILD_DOCS)

SET(CMAKE_INSTALL_RPATH "$ORIGIN/.")
SET(CMAKE_BUILD_RPATH "$ORIGIN/.")

#cmake_host_system_information(RESULT RELEASE QUERY OS_RELEASE)

include_directories(
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>/stackstring
  )

add_subdirectory(stackstring)
add_subdirectory(neumodb)
add_subdirectory(receiver)
add_subdirectory(util)
add_subdirectory(viewer)

add_custom_target(etags
                    COMMAND ctags -R -e `find . -name '*.cc' -o -name '*.h' -o -name '*.y'`
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})


#git revision info todo: same information is already retrieved in util
execute_process(COMMAND git log --pretty=format:'%h' -n 1
                OUTPUT_VARIABLE GIT_REV
                ERROR_QUIET)

# Check whether we got any revision (which isn't
# always the case, e.g. when someone downloaded a zip
# file from Github instead of a checkout)
if ("${GIT_REV}" STREQUAL "")
    set(GIT_REV "N/A")
    set(GIT_DIFF "")
    set(GIT_TAG "N/A")
    set(GIT_BRANCH "N/A")
else()
    execute_process(
        COMMAND bash -c "git diff --quiet --exit-code || echo +"
        OUTPUT_VARIABLE GIT_DIFF)
    execute_process(
        COMMAND git describe --exact-match --tags
        OUTPUT_VARIABLE GIT_TAG ERROR_QUIET)
    execute_process(
        COMMAND git rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE GIT_BRANCH)

    string(STRIP "${GIT_REV}" GIT_REV)
    string(SUBSTRING "${GIT_REV}" 1 7 GIT_REV)
    string(STRIP "${GIT_DIFF}" GIT_DIFF)
    string(STRIP "${GIT_TAG}" GIT_TAG)
    string(STRIP "${GIT_BRANCH}" GIT_BRANCH)
endif()

set(VERSION "const char* GIT_REV=\"${GIT_REV}${GIT_DIFF}\";
const char* GIT_TAG=\"${GIT_TAG}\";
const char* GIT_BRANCH=\"${GIT_BRANCH}\";")
