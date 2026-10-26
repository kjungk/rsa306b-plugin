cmake_minimum_required(VERSION 3.25)
project(rsa306b_native VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0
  HINTS "${LIBUSB_ROOT}/include" /opt/homebrew/include /usr/local/include REQUIRED)
find_library(LIBUSB_LIBRARY NAMES usb-1.0
  HINTS "${LIBUSB_ROOT}/lib" /opt/homebrew/lib /usr/local/lib REQUIRED)

add_library(rsa_core src/spectrum.cpp src/usb.cpp src/protocol.cpp src/acquisition.cpp src/device_lock.cpp src/calibration.cpp src/mock.cpp)
target_include_directories(rsa_core PUBLIC include)
target_include_directories(rsa_core SYSTEM PRIVATE "${LIBUSB_INCLUDE_DIR}")
target_link_libraries(rsa_core PRIVATE "${LIBUSB_LIBRARY}")
target_compile_options(rsa_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
add_executable(rsa306 src/main.cpp)
target_link_libraries(rsa306 PRIVATE rsa_core)
target_compile_options(rsa306 PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

include(CTest)
if(BUILD_TESTING)
  add_executable(rsa_native_calibration_check tools/native_calibration_check.cpp)
  target_link_libraries(rsa_native_calibration_check PRIVATE rsa_core)
  # Explicit manual hardware tool; never invoked by CTest.
  add_executable(rsa_core_tests tests/core_tests.cpp)
  target_link_libraries(rsa_core_tests PRIVATE rsa_core)
  add_test(NAME core COMMAND rsa_core_tests)
  add_executable(rsa_lock_tests tests/lock_tests.cpp)
  target_link_libraries(rsa_lock_tests PRIVATE rsa_core)
  add_test(NAME device_lock COMMAND rsa_lock_tests)
  add_executable(rsa_protocol_tests tests/protocol_tests.cpp)
  target_link_libraries(rsa_protocol_tests PRIVATE rsa_core)
  target_compile_definitions(rsa_protocol_tests PRIVATE RSA_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rsa306b-if-frame.bin")
  target_compile_definitions(rsa_protocol_tests PRIVATE RSA_ROUTE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rsa306b-hw7-routes.csv")
  add_test(NAME protocol COMMAND rsa_protocol_tests)
  find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)
  add_test(NAME python_and_cli COMMAND "${Python3_EXECUTABLE}"
    -B -m unittest discover -s tests -p "test_*.py")
  set_tests_properties(python_and_cli PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    ENVIRONMENT "RSA306_BINARY=$<TARGET_FILE:rsa306>;RSA_CALIBRATION_CHECK=$<TARGET_FILE:rsa_native_calibration_check>")
endif()
