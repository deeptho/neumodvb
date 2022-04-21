
install(DIRECTORY . DESTINATION /etc/neumodvb
  PATTERN "CMakeLists.txt" EXCLUDE
  PATTERN "unused" EXCLUDE
  PATTERN "*~" EXCLUDE
  )
