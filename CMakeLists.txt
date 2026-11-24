cmake_minimum_required(VERSION 3.20)
project(opera_grenouille LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(assimp REQUIRED)
find_package(Threads REQUIRED)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

set(OG_SOURCES
  src/main.cpp
  src/core/Application.cpp
  src/core/Input.cpp
  src/core/AssetPath.cpp
  src/render/Shader.cpp
  src/render/Mesh.cpp
  src/render/Framebuffer.cpp
  src/render/Renderer.cpp
  src/scene/CityScene.cpp
  src/scene/AuditoriumScene.cpp
  src/scene/ModelLoader.cpp
  src/scene/World.cpp
  src/actors/Frog.cpp
  src/instruments/Orchestra.cpp
  src/lighting/LightingRig.cpp
  src/camera/OrbitCamera.cpp
  src/camera/RailCamera.cpp
  src/audio/AudioEngine.cpp
  src/audio/MidiSong.cpp
  src/net/Protocol.cpp
  src/net/NetClient.cpp
  src/ui/Hud.cpp
)
if(APPLE)
  list(APPEND OG_SOURCES src/camera/Recorder_mac.mm)
  set_source_files_properties(src/camera/Recorder_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
  list(APPEND OG_SOURCES src/camera/Recorder.cpp)
endif()

add_executable(opera_grenouille ${OG_SOURCES})
target_include_directories(opera_grenouille PRIVATE src third_party)
target_compile_definitions(opera_grenouille PRIVATE GLFW_INCLUDE_NONE GL_SILENCE_DEPRECATION
  OG_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(opera_grenouille PRIVATE
  glad imgui glfw OpenGL::GL glm::glm assimp::assimp Threads::Threads)
if(APPLE)
  target_link_libraries(opera_grenouille PRIVATE
    "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework CoreMedia"
    "-framework AVFoundation" "-framework AudioToolbox" "-framework CoreAudio"
    "-framework UniformTypeIdentifiers"
    "-Wl,-sectcreate,__TEXT,__info_plist,${CMAKE_SOURCE_DIR}/cmake/Info.plist")
elseif(UNIX)
  target_link_libraries(opera_grenouille PRIVATE dl m)
endif()

# Shaders and assets live next to the binary so it runs from any directory.
add_custom_command(TARGET opera_grenouille POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:opera_grenouille>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:opera_grenouille>/shaders
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:opera_grenouille>/assets
)

add_executable(opera_server server/main.cpp src/net/Protocol.cpp)
target_include_directories(opera_server PRIVATE src third_party)
target_link_libraries(opera_server PRIVATE Threads::Threads)

install(TARGETS opera_grenouille opera_server RUNTIME DESTINATION bin)
