cmake_minimum_required(VERSION 3.22)
project(AETHERIA_GR LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(AETHERIA_STAGE "0" CACHE STRING "Build stage: 0=Triangle, 1=KerrBH, 2=FullCosmos")

include(FetchContent)

if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git  GIT_TAG release-3.2.8 GIT_SHALLOW TRUE)
FetchContent_Declare(glm   GIT_REPOSITORY https://github.com/g-truc/glm.git      GIT_TAG 1.0.1         GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git   GIT_TAG v1.91.8       GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3 glm imgui)

if(ANDROID)
    find_library(VULKAN_TARGET vulkan REQUIRED)
else()
    find_package(Vulkan REQUIRED)
    set(VULKAN_TARGET Vulkan::Vulkan)
endif()

add_library(imgui_vk STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_vk PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_vk PUBLIC SDL3::SDL3 ${VULKAN_TARGET})

if(ANDROID)
    # SDL3 SDLActivity expects libmain.so exporting SDL_main
    add_library(main SHARED src/main.cpp)
    target_compile_definitions(main PRIVATE AETHERIA_STAGE=${AETHERIA_STAGE})
    target_compile_options(main PRIVATE -O3 -march=armv8-a)
    target_include_directories(main PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(main PRIVATE SDL3::SDL3 ${VULKAN_TARGET} glm::glm imgui_vk log android)
else()
    add_executable(aetheria_gr src/main.cpp)
    target_compile_definitions(aetheria_gr PRIVATE AETHERIA_STAGE=${AETHERIA_STAGE})
    target_compile_options(aetheria_gr PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native>)
    target_include_directories(aetheria_gr PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(aetheria_gr PRIVATE SDL3::SDL3 ${VULKAN_TARGET} glm::glm imgui_vk)

    file(GLOB SHADER_SOURCES
        "${CMAKE_SOURCE_DIR}/shaders/*.comp"
        "${CMAKE_SOURCE_DIR}/shaders/*.vert"
        "${CMAKE_SOURCE_DIR}/shaders/*.frag"
    )
    set(SPV_DIR "${CMAKE_BINARY_DIR}/shaders")
    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(FNAME ${SHADER} NAME)
        set(SPV_OUT "${SPV_DIR}/${FNAME}.spv")
        add_custom_command(
            OUTPUT "${SPV_OUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SPV_DIR}"
            COMMAND Vulkan::glslc -O "${SHADER}" -o "${SPV_OUT}"
            DEPENDS "${SHADER}"
        )
        list(APPEND SPV_TARGETS "${SPV_OUT}")
    endforeach()
    add_custom_target(CompileShaders ALL DEPENDS ${SPV_TARGETS})
    add_dependencies(aetheria_gr CompileShaders)
    add_custom_command(TARGET aetheria_gr POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${SPV_DIR}" "$<TARGET_FILE_DIR:aetheria_gr>/shaders"
    )
endif()
