cmake_minimum_required(VERSION 3.16)
project(cyberpower-ups
  VERSION 0.3.0
  DESCRIPTION "C++17 library and CLI for CyberPower UPS devices over USB HID and serial"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(cyberpower-ups
  src/errors.cpp
  src/protocol.cpp
  src/driver.cpp
  src/device.cpp
  src/c_api.cpp
  src/raw_dump.cpp
)

add_library(cyberpower-ups::cyberpower-ups ALIAS cyberpower-ups)

target_include_directories(cyberpower-ups
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(cyberpower-ups PUBLIC cxx_std_17)

set_target_properties(cyberpower-ups PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 0
  EXPORT_NAME cyberpower-ups
  OUTPUT_NAME cyberpower-ups
)

if(APPLE)
  target_sources(cyberpower-ups PRIVATE
    src/platform/macos/hid_macos.cpp
    src/platform/macos/serial_macos.cpp
  )
  target_link_libraries(cyberpower-ups PUBLIC
    "-framework IOKit"
    "-framework CoreFoundation"
  )
elseif(WIN32)
  target_sources(cyberpower-ups PRIVATE src/platform/windows/transport_stub.cpp)
else()
  # Non-Apple Unix builds use the Windows stub until a Linux transport exists.
  target_sources(cyberpower-ups PRIVATE src/platform/windows/transport_stub.cpp)
endif()

add_executable(cpups tools/cpups.cpp)
target_link_libraries(cpups PRIVATE cyberpower-ups)

add_executable(list_and_status examples/list_and_status.cpp)
target_link_libraries(list_and_status PRIVATE cyberpower-ups)

# ---------------------------------------------------------------------------
# Offline tests (no UPS required)
# ---------------------------------------------------------------------------

include(CTest)
if(BUILD_TESTING)
  add_executable(offline_test tests/offline_test.cpp)
  target_link_libraries(offline_test PRIVATE cyberpower-ups)
  add_test(NAME offline_test COMMAND offline_test)
  set_tests_properties(offline_test PROPERTIES
    ENVIRONMENT "CPUPS_FIXTURES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  )
endif()

# ---------------------------------------------------------------------------
# Install / package config
# ---------------------------------------------------------------------------

install(TARGETS cyberpower-ups
  EXPORT cyberpower-upsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS cpups
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/cyberpower
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

install(EXPORT cyberpower-upsTargets
  FILE cyberpower-upsTargets.cmake
  NAMESPACE cyberpower-ups::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cyberpower-ups
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cyberpower-upsConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cyberpower-upsConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cyberpower-ups
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/cyberpower-upsConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/cyberpower-upsConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/cyberpower-upsConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cyberpower-ups
)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cyberpower-ups.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/cyberpower-ups.pc
  @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cyberpower-ups.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
