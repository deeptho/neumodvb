find_program(PYTHON "python")

if (PYTHON)

  set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
  set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
  set(DEPS        "${CMAKE_CURRENT_SOURCE_DIR}/neumodvb/__init__.py")
  set(DEPS        "${CMAKE_CURRENT_SOURCE_DIR}/neumodvb/upgrade/__init__.py")
  set(OUTPUT      "${CMAKE_CURRENT_BINARY_DIR}/timestamp")

  configure_file(${SETUP_PY_IN} ${SETUP_PY})

  install(CODE "execute_process(COMMAND ${PYTHON} ${SETUP_PY} install --prefix=/usr )")

endif()

install(PROGRAMS neumodvb.py DESTINATION bin RENAME neumodvb)
install(PROGRAMS neumoupgrade.py  DESTINATION bin)
