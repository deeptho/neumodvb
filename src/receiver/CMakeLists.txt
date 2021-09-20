find_package(wxWidgets REQUIRED net gl core base)
include(${wxWidgets_USE_FILE})


add_library(neumoreceiver SHARED  receiver.cc subscriber.cc subscriber_notify.cc tune.cc scan.cc
  active_adapter.cc devmanager.cc fe_monitor.cc
  active_si_stream.cc recmgr.cc frontend.cc scam.cc
  active_stream.cc active_service.cc filemapper.cc live_mpm.cc active_playback.cc playback_mpm.cc
  dvbcsa.cc capmt.cc streamfilter.cc spectrum_algo2.cc)


target_precompile_headers(neumoreceiver PRIVATE
  active_playback.h active_stream.h dvbcsa.h subscriber.h options.h reservation.h signal_info.h
  active_adapter.h active_service.h adapter.h filemapper.h neumofrontend.h receiver.h scam.h
  active_si_stream.h dvbapi.h mpm.h neumo.h recmgr.h scan.h task.h
  )


string (REPLACE ";" " -I" EXTRA_FLAGS_STR "${pybind11_INCLUDE_DIRS}")
set_source_files_properties( subscriber_notify.cc COMPILE_FLAGS ${EXTRA_FLAGS_STR} )

add_dependencies(neumoreceiver chdb statdb epgdb recdb recdb stackstring streamparser)

target_link_libraries(neumoreceiver PUBLIC chdb statdb epgdb recdb neumoutil neumodb streamparser config++ PRIVATE dvbcsa ${wxWidgets_LIBRARIES})


target_compile_options(neumoreceiver PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error
install (TARGETS neumoreceiver  DESTINATION lib64)

add_executable(neumo-blindscan neumo-blindscan.cc)
add_executable(neumo-tune neumo-tune.cc)

target_link_libraries(neumo-blindscan PRIVATE neumoutil stdc++fs)
target_link_libraries(neumo-tune PRIVATE neumoutil  stdc++fs)




include_directories(
    # The location of the headers before installation
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../pybind11/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # The location of the headers after installation
    $<INSTALL_INTERFACE:include>
)

add_subdirectory(streamparser)


unset(CMAKE_CXX_STANDARD)
set(PYBIND11_CPP_STANDARD -std=c++17) # Experimental C++17 support

pybind11_add_module(pyreceiver receiver_pybind.cc options_pybind.cc subscriber_pybind.cc logger_pybind.cc)
pybind11_add_module(pyspectrum spectrum_pybind.cc)


target_compile_options(pyreceiver PUBLIC -fPIC -fsized-deallocation) #needed to prevent operator delete error

target_link_libraries(pyspectrum PRIVATE neumoutil)
target_link_libraries(pyreceiver PRIVATE neumoutil)


string (REPLACE ";" " " EXTRA_FLAGS_STR "${GTK3_CFLAGS}")
set_source_files_properties(subscriber_pybind.cc COMPILE_FLAGS "${EXTRA_FLAGS_STR}" )

target_link_libraries(pyreceiver PUBLIC neumoreceiver )

target_link_options(pyreceiver PUBLIC -lgtk-3)

install (TARGETS pyreceiver pyspectrum DESTINATION ${Python_SITEARCH}/${PROJECT_NAME})
