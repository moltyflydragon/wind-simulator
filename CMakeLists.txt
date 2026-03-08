cmake_minimum_required(VERSION 3.16)
project(WindSimulator LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# SDL2
find_package(SDL2 REQUIRED)

# OpenGL
find_package(OpenGL REQUIRED)

# ImGui sources
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(windsim
    src/main.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(windsim PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(windsim PRIVATE
    ${SDL2_LIBRARIES}
    OpenGL::GL
)

# macOS: suppress OpenGL deprecation warnings
if(APPLE)
    target_compile_definitions(windsim PRIVATE GL_SILENCE_DEPRECATION)
endif()
