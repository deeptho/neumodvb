find_package(Python3 COMPONENTS Interpreter)
if (Python3_FOUND)
  set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
  set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
  set(DEPS        "${CMAKE_CURRENT_SOURCE_DIR}/neumodvb/__init__.py")
  set(DEPS        "${CMAKE_CURRENT_SOURCE_DIR}/neumodvb/upgrade/__init__.py")
  set(OUTPUT      "${CMAKE_CURRENT_BINARY_DIR}/timestamp")

  configure_file(${SETUP_PY_IN} ${SETUP_PY})

  install(CODE "execute_process(COMMAND ${Python3_EXECUTABLE} ${SETUP_PY} install --prefix=/usr )")
else()
message(STATUS  "Could not find python interpreter")
endif()

install(PROGRAMS neumodvb.py DESTINATION bin RENAME neumodvb)
install(PROGRAMS neumoupgrade.py  DESTINATION bin)
