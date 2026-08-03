cmake_minimum_required(VERSION 3.24)
project(PartSplice3D VERSION 1.0.0 LANGUAGES C CXX)

configure_file(src/version.h.in generated/version.h @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 7b6aead9fb88b3623e3b3725ebb42670cbe4c579)
FetchContent_MakeAvailable(glfw)

# Dear ImGui
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 01380c579715e62fb9a8d6ec0502c4ea83bfde6e)
FetchContent_MakeAvailable(imgui)
add_library(imgui_static STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp)
target_include_directories(imgui_static PUBLIC ${imgui_SOURCE_DIR})
target_link_libraries(imgui_static PUBLIC glfw)

# 3MF: ZIP container and XML model document
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 77d0dce8627735138c51770d1799a1ef48f2117d)
FetchContent_MakeAvailable(miniz)

set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 57eea48c5bfc354ac45d53b2eb499e66c80d63e0)
FetchContent_MakeAvailable(tinyxml2)

# Manifold: robuster, manifold-erhaltender CSG-Kern
set(MANIFOLD_TEST OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PYBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_JSBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CBIND OFF CACHE BOOL "" FORCE)
set(MANIFOLD_PAR OFF CACHE BOOL "" FORCE)
set(MANIFOLD_DEBUG OFF CACHE BOOL "" FORCE)
set(MANIFOLD_STRICT OFF CACHE BOOL "" FORCE)
set(MANIFOLD_CROSS_SECTION ON CACHE BOOL "" FORCE)
set(MANIFOLD_USE_BUILTIN_CLIPPER2 ON CACHE BOOL "" FORCE)
set(MANIFOLD_DOWNLOADS ON CACHE BOOL "" FORCE)
FetchContent_Declare(manifold_src
    GIT_REPOSITORY https://github.com/elalish/manifold.git
    GIT_TAG cc8a7f66d7d5a560da94346258c5b546af27811e
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(manifold_src)

find_package(OpenGL REQUIRED)
find_package(OpenCASCADE CONFIG REQUIRED)

add_executable(PartSplice3D WIN32
    src/main.cpp
    src/app_domain.cpp
    src/app_queries.cpp
    src/app_state.cpp
    src/app_log.cpp
    src/background_progress.cpp
    src/calibration_panel.cpp
    src/connector_editor.cpp
    src/file_dialogs.cpp
    src/export_workflow.cpp
    src/generation_workflow.cpp
    src/app_persistence.cpp
    src/interaction_hit_test.cpp
    src/localization.cpp
    src/app_resources.rc
    src/stl_io.cpp
    src/three_mf.cpp
    src/project_io.cpp
    src/project_actions.cpp
    src/project_workflow.cpp
    src/printer_presets.cpp
    src/update_check.cpp
    src/update_workflow.cpp
    src/ui_dialogs.cpp
    src/ui_runtime.cpp
    src/viewport_render.cpp
    src/viewport_interaction.cpp
    src/viewport_context_menu.cpp
    src/viewport_input.cpp
    src/windows_mesh_repair.cpp
    src/mesh_ops.cpp
    src/mesh_spatial_index.cpp
    src/model_projection.cpp
    src/safe_temp.cpp
    src/scene_core.cpp
    src/worker_client.cpp
    src/workspace_panels.cpp
    src/math3d.h
    src/app_domain.h
    src/app_queries.h
    src/app_state.h
    src/localization.h
    src/localized_imgui.h
    src/resources.h
    src/background_progress.h
    src/calibration_panel.h
    src/connector_editor.h
    src/export_workflow.h
    src/generation_workflow.h
    src/stl_io.h
    src/step_io.h
    src/three_mf.h
    src/project_io.h
    src/project_actions.h
    src/project_workflow.h
    src/printer_presets.h
    src/update_check.h
    src/update_workflow.h
    src/ui_dialogs.h
    src/ui_runtime.h
    src/visual_style.h
    src/viewport_render.h
    src/viewport_interaction.h
    src/viewport_context_menu.h
    src/viewport_input.h
    src/windows_mesh_repair.h
    src/mesh_ops.h
    src/mesh_spatial_index.h
    src/model_projection.h
    src/safe_temp.h
    src/scene_core.h
    src/worker_client.h
    src/workspace_panels.h
    src/connector_types.h)

target_include_directories(PartSplice3D PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(PartSplice3D PRIVATE
    imgui_static glfw OpenGL::GL manifold miniz tinyxml2)

# Under Windows all STEP operations belong to the worker, so the GUI neither
# links nor loads OpenCASCADE. Other platforms retain the in-process fallback.
if(NOT WIN32)
    target_sources(PartSplice3D PRIVATE src/step_io.cpp)
    target_include_directories(PartSplice3D PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(PartSplice3D PRIVATE ${OpenCASCADE_LIBRARIES})
endif()

add_executable(PartSpliceWorker
    src/worker_main.cpp
    src/worker_resources.rc
    src/stl_io.cpp
    src/step_io.cpp
    src/three_mf.cpp
    src/project_io.cpp
    src/mesh_ops.cpp
    src/mesh_spatial_index.cpp
    src/safe_temp.cpp
    src/math3d.h
    src/stl_io.h
    src/step_io.h
    src/three_mf.h
    src/project_io.h
    src/mesh_ops.h
    src/mesh_spatial_index.h
    src/safe_temp.h
    src/connector_types.h)
target_include_directories(PartSpliceWorker PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated" ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(PartSpliceWorker PRIVATE manifold miniz tinyxml2 ${OpenCASCADE_LIBRARIES})
add_dependencies(PartSplice3D PartSpliceWorker)

if(MSVC)
    get_filename_component(WINDOWS_KITS_ROOT
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
    target_include_directories(PartSplice3D PRIVATE
        "${WINDOWS_KITS_ROOT}/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    target_compile_options(PartSplice3D PRIVATE /W4 /permissive- /utf-8 /sdl /guard:cf)
    target_compile_options(PartSpliceWorker PRIVATE /W4 /permissive- /utf-8 /sdl /guard:cf)
    target_compile_definitions(PartSplice3D PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(PartSpliceWorker PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(PartSplice3D PRIVATE bcrypt comdlg32 ole32 shell32 runtimeobject winhttp)
    target_link_libraries(PartSpliceWorker PRIVATE bcrypt)
    target_link_options(PartSplice3D PRIVATE /DYNAMICBASE /NXCOMPAT /guard:cf /CETCOMPAT)
    target_link_options(PartSpliceWorker PRIVATE /DYNAMICBASE /NXCOMPAT /guard:cf /CETCOMPAT)
else()
    target_compile_options(PartSplice3D PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(PartSpliceWorker PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(PartSplice3D PROPERTIES
    OUTPUT_NAME "PartSplice3D")

install(TARGETS PartSplice3D PartSpliceWorker RUNTIME DESTINATION .)
