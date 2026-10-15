cmake_minimum_required(VERSION 3.24)
project(EBSDDesktop LANGUAGES CXX)

include(CTest)
include(FetchContent)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optional local header locations make the gateway tests usable offline.
set(EBSD_HTTPLIB_INCLUDE_DIR "" CACHE PATH "Directory containing httplib.h")
set(EBSD_JSON_INCLUDE_DIR "" CACHE PATH "Directory containing nlohmann/json.hpp")
if(NOT EBSD_HTTPLIB_INCLUDE_DIR)
  FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.20.1.tar.gz
    URL_HASH SHA256=b74b1c2c150be2841eba80192f64d93e9a6711985b3ae8aaa1a9cec4863d1dd9
    SOURCE_SUBDIR unused)
  FetchContent_MakeAvailable(httplib)
  set(EBSD_HTTPLIB_INCLUDE_DIR "${httplib_SOURCE_DIR}")
endif()
if(NOT EBSD_JSON_INCLUDE_DIR)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    SOURCE_SUBDIR unused)
  FetchContent_MakeAvailable(json)
  set(EBSD_JSON_INCLUDE_DIR "${json_SOURCE_DIR}/single_include")
endif()

find_package(Threads REQUIRED)
add_library(desktop_gateway STATIC src/LocalServer.cpp)
target_include_directories(desktop_gateway PUBLIC src "${EBSD_HTTPLIB_INCLUDE_DIR}" "${EBSD_JSON_INCLUDE_DIR}")
target_link_libraries(desktop_gateway PUBLIC Threads::Threads)
if(WIN32)
  target_compile_definitions(desktop_gateway PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(desktop_gateway PUBLIC ws2_32 crypt32)
endif()
if(MSVC)
  target_compile_options(desktop_gateway PUBLIC /utf-8 /bigobj)
endif()
if(BUILD_TESTING)
  add_executable(gateway_tests tests/gateway_tests.cpp)
  target_link_libraries(gateway_tests PRIVATE desktop_gateway)
  add_test(NAME desktop_gateway_contract COMMAND gateway_tests)
  set_tests_properties(desktop_gateway_contract PROPERTIES TIMEOUT 45)
endif()

option(EBSD_BUILD_WINUI "Build the Windows XAML application with MSBuild and NuGet" ${WIN32})
if(EBSD_BUILD_WINUI)
  if(NOT WIN32 OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR "WinUI requires Windows and the Visual Studio generator; use the windows-x64 preset.")
  endif()
  set(EBSD_WINDOWS_APP_SDK_VERSION "1.8.260921001" CACHE STRING "Pinned Windows App SDK NuGet version")
  set(EBSD_CPPWINRT_VERSION "2.0.250303.1" CACHE STRING "Pinned C++/WinRT NuGet version")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/winui")
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" EBSD_NATIVE_SOURCE)
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}" EBSD_NATIVE_BINARY)
  file(TO_NATIVE_PATH "${EBSD_HTTPLIB_INCLUDE_DIR}" EBSD_NATIVE_HTTP_INCLUDE)
  file(TO_NATIVE_PATH "${EBSD_JSON_INCLUDE_DIR}" EBSD_NATIVE_JSON_INCLUDE)
  configure_file(cmake/EBSDDesktop.vcxproj.in winui/EBSDDesktop.vcxproj @ONLY)
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/winui/EBSDDesktop.vcxproj" EBSD_NATIVE_PROJECT)
  # XAML, MIDL, C++/WinRT projections and PRI resources need Microsoft's targets.
  # Keep this separate from CMake's ordinary C++ project generation.
  add_custom_target(windows_frontend ALL
    COMMAND "${CMAKE_VS_MSBUILD_COMMAND}" "${EBSD_NATIVE_PROJECT}"
      /restore /m /nologo /p:Configuration=$<CONFIG> /p:Platform=x64
    USES_TERMINAL VERBATIM)
  find_program(EBSD_BUN_EXECUTABLE NAMES bun)
  if(EBSD_BUN_EXECUTABLE)
    add_custom_target(web_assets
      COMMAND "${EBSD_BUN_EXECUTABLE}" --bun run node_modules/nuxt/bin/nuxt.mjs generate
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../frontend"
      USES_TERMINAL VERBATIM)
  endif()
endif()
