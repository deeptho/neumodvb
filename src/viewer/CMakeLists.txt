find_package(wxWidgets REQUIRED net gl core base)
include(${wxWidgets_USE_FILE})

pybind11_add_module(pyneumompv neumompv.cc neumosvg.cc neumompv_pybind.cc neumoradiobg.cc)

include_directories(
    # The location of the headers before installation
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # The location of the headers after installation
    $<INSTALL_INTERFACE:include>
)

# -fsized-deallocation needed to prevent operator delete error
target_compile_options(pyneumompv PUBLIC -fPIC -fsized-deallocation ${NEUMOWXSVG_CFLAGS} )
target_link_libraries(pyneumompv PUBLIC neumoreceiver recdb neumoutil ${wxWidgets_LIBRARIES} neumowxsvg exif  -lmpv -lGL -lglut )


install (TARGETS pyneumompv DESTINATION ${Python_STDLIB}/${PROJECT_NAME})
