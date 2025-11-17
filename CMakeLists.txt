cmake_minimum_required(VERSION 3.16)
project(gribview VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
  add_compile_options(/permissive- /W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# SDL2 ------------------------------------------------------
find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
  find_package(SDL2 REQUIRED)
endif()

set(SDL2_CORE_LIBS "")
set(SDL2_MAIN_LIB "")
set(SDL2_INCLUDE_FALLBACK "")
if(TARGET SDL2::SDL2)
  list(APPEND SDL2_CORE_LIBS SDL2::SDL2)
else()
  list(APPEND SDL2_CORE_LIBS ${SDL2_LIBRARIES})
  if(DEFINED SDL2_INCLUDE_DIRS)
    list(APPEND SDL2_INCLUDE_FALLBACK ${SDL2_INCLUDE_DIRS})
  endif()
  if(DEFINED SDL2_INCLUDE_DIR)
    list(APPEND SDL2_INCLUDE_FALLBACK ${SDL2_INCLUDE_DIR})
  endif()
endif()

if(TARGET SDL2::SDL2main)
  set(SDL2_MAIN_LIB SDL2::SDL2main)
elseif(DEFINED SDL2MAIN_LIBRARY)
  set(SDL2_MAIN_LIB ${SDL2MAIN_LIBRARY})
endif()

list(REMOVE_DUPLICATES SDL2_INCLUDE_FALLBACK)

# ecCodes ---------------------------------------------------
set(ECCODES_IMPORTED_TARGET "")
find_package(ecCodes CONFIG QUIET)
if(TARGET eccodes::eccodes)
  set(ECCODES_IMPORTED_TARGET eccodes::eccodes)
elseif(TARGET eccodes)
  set(ECCODES_IMPORTED_TARGET eccodes)
endif()

if(NOT ECCODES_IMPORTED_TARGET)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(ECCODES_PKG QUIET eccodes)
    if(ECCODES_PKG_FOUND)
      set(ECCODES_IMPORTED_TARGET PkgConfig::ECCODES)
    endif()
  endif()
endif()

if(NOT ECCODES_IMPORTED_TARGET)
  set(_eccodes_hint "")
  if(DEFINED ENV{ECCODES_ROOT})
    list(APPEND _eccodes_hint $ENV{ECCODES_ROOT})
  endif()
  if(DEFINED ENV{ECCODES_HOME})
    list(APPEND _eccodes_hint $ENV{ECCODES_HOME})
  endif()

  find_path(ECCODES_INCLUDE_DIR eccodes.h
    HINTS ${_eccodes_hint}
    PATH_SUFFIXES include include/eccodes)
  find_library(ECCODES_LIBRARY NAMES eccodes
    HINTS ${_eccodes_hint}
    PATH_SUFFIXES lib lib64)
  if(ECCODES_INCLUDE_DIR AND ECCODES_LIBRARY)
    add_library(gribview_eccodes INTERFACE)
    target_include_directories(gribview_eccodes INTERFACE ${ECCODES_INCLUDE_DIR})
    target_link_libraries(gribview_eccodes INTERFACE ${ECCODES_LIBRARY})
    set(ECCODES_IMPORTED_TARGET gribview_eccodes)
  endif()
endif()

if(NOT ECCODES_IMPORTED_TARGET)
  message(FATAL_ERROR "ecCodes not found. Install eccodes (e.g. via your package manager) or set ECCODES_ROOT.")
endif()

# Dear ImGui ------------------------------------------------
add_library(imgui STATIC
  external/imgui/imgui.cpp
  external/imgui/imgui_demo.cpp
  external/imgui/imgui_draw.cpp
  external/imgui/imgui_tables.cpp
  external/imgui/imgui_widgets.cpp
  external/imgui/backends/imgui_impl_opengl3.cpp
  external/imgui/backends/imgui_impl_sdl2.cpp
)
target_include_directories(imgui PUBLIC
  external/imgui
  external/imgui/backends
)
if(SDL2_INCLUDE_FALLBACK)
  target_include_directories(imgui PUBLIC ${SDL2_INCLUDE_FALLBACK})
endif()
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLEW)
target_link_libraries(imgui PUBLIC GLEW::GLEW)
if(SDL2_CORE_LIBS)
  target_link_libraries(imgui PUBLIC ${SDL2_CORE_LIBS})
endif()

add_library(tinyfiledialogs STATIC external/tinyfiledialogs/tinyfiledialogs.c)
target_include_directories(tinyfiledialogs PUBLIC external/tinyfiledialogs)
set_target_properties(tinyfiledialogs PROPERTIES LINKER_LANGUAGE C)

# gribview executable ---------------------------------------
add_executable(gribview src/gribview.cpp)
target_include_directories(gribview PRIVATE src)
if(SDL2_INCLUDE_FALLBACK)
  target_include_directories(gribview PRIVATE ${SDL2_INCLUDE_FALLBACK})
endif()
target_link_libraries(gribview PRIVATE
  imgui
  tinyfiledialogs
  GLEW::GLEW
  OpenGL::GL
  ${ECCODES_IMPORTED_TARGET}
)
if(SDL2_CORE_LIBS)
  target_link_libraries(gribview PRIVATE ${SDL2_CORE_LIBS})
endif()
if(SDL2_MAIN_LIB)
  target_link_libraries(gribview PRIVATE ${SDL2_MAIN_LIB})
endif()

install(TARGETS gribview RUNTIME DESTINATION bin)
