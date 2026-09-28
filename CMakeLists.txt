cmake_minimum_required(VERSION 3.16)
project(cyberpower-ups LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cyberpower-ups
  src/errors.cpp
  src/protocol.cpp
  src/driver.cpp
  src/device.cpp
  src/c_api.cpp
)

target_include_directories(cyberpower-ups
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
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
  target_sources(cyberpower-ups PRIVATE src/platform/windows/transport_stub.cpp)
endif()

add_executable(cpups tools/cpups.cpp)
target_link_libraries(cpups PRIVATE cyberpower-ups)

add_executable(list_and_status examples/list_and_status.cpp)
target_link_libraries(list_and_status PRIVATE cyberpower-ups)

install(TARGETS cpups RUNTIME DESTINATION bin)
