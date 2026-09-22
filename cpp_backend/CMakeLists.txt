cmake_minimum_required(VERSION 3.11)

project(ebsd_cpp_backend)

include(CTest)

# avoid windows SDK minmax conflict             
add_compile_definitions(NOMINMAX)


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)



set(EbsdLib_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/share/EbsdLib")
find_package(EbsdLib REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(stduuid CONFIG REQUIRED)



file(GLOB_RECURSE HEADERS "include/*.h" "include/*.hpp")
file(GLOB_RECURSE SOURCES "src/*.cpp")

if (MSVC)
    add_compile_options(/DEBUG /utf-8)
endif()


add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})


include_directories(include)

target_link_libraries(${PROJECT_NAME} PRIVATE EbsdLib::EbsdLib)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(${PROJECT_NAME} PRIVATE httplib::httplib)
target_link_libraries(${PROJECT_NAME} PRIVATE stduuid)

if(BUILD_TESTING)
    add_executable(
        band_contrast_map_test
        tests/band_contrast_map_test.cpp
        src/band_contrast_map.cpp
        src/scale_bar.cpp
        include/band_contrast_map.h
        include/ebsd_data.h
        include/scale_bar.h
    )
    target_include_directories(band_contrast_map_test PRIVATE include)
    add_test(NAME band_contrast_map_test COMMAND band_contrast_map_test)

    add_executable(
        texture_figures_test
        tests/texture_figures_test.cpp
        src/texture_figures.cpp
        include/texture_figures.h
        include/ebsd_data.h
    )
    target_include_directories(texture_figures_test PRIVATE include)
    if(NOT MSVC)
        target_compile_options(texture_figures_test PRIVATE -ffp-contract=off)
    endif()
    add_test(NAME texture_figures_test COMMAND texture_figures_test)

endif()
