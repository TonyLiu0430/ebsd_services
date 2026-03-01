cmake_minimum_required(VERSION 3.11)

project(ebsd_cpp_backend)

# avoid windows SDK minmax conflict             
add_compile_definitions(NOMINMAX)


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)



set(EbsdLib_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/share/EbsdLib")
find_package(EbsdLib REQUIRED)
find_package(unofficial-libsvm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)



file(GLOB_RECURSE HEADERS "include/*.h" "include/*.hpp")
file(GLOB_RECURSE SOURCES "src/*.cpp")

if (MSVC)
    add_compile_options(/DEBUG /utf-8)
endif()


add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})


include_directories(include)

target_link_libraries(${PROJECT_NAME} PRIVATE EbsdLib::EbsdLib unofficial::libsvm::libsvm)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)