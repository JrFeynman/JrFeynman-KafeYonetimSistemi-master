cmake_minimum_required(VERSION 3.16)
project(restopulse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_definitions(GL_SILENCE_DEPRECATION=1)

option(RESTOPULSE_WITH_REDIS "Link hiredis for Redis hot engine" ON)

# --- SQLite ---
find_path(SQLITE3_INCLUDE_DIR sqlite3.h
  HINTS /opt/homebrew/opt/sqlite/include /usr/local/opt/sqlite/include /opt/homebrew/include)
find_library(SQLITE3_LIBRARY sqlite3
  HINTS /opt/homebrew/opt/sqlite/lib /usr/local/opt/sqlite/lib /opt/homebrew/lib)
if(NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
  message(FATAL_ERROR "sqlite3 not found. brew install sqlite")
endif()

# --- GLFW ---
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
  find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h
    HINTS /opt/homebrew/include /usr/local/include)
  find_library(GLFW_LIBRARY glfw
    HINTS /opt/homebrew/lib /usr/local/lib)
  if(NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY)
    message(FATAL_ERROR "GLFW not found. brew install glfw")
  endif()
endif()

# --- OpenGL ---
find_package(OpenGL REQUIRED)

# --- Optional Redis ---
set(RESTOPULSE_REDIS_LIBS "")
if(RESTOPULSE_WITH_REDIS)
  find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h
    HINTS /opt/homebrew/include /usr/local/include)
  find_library(HIREDIS_LIBRARY hiredis
    HINTS /opt/homebrew/lib /usr/local/lib)
  if(HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARY)
    add_compile_definitions(RESTOPULSE_HAS_REDIS=1)
    list(APPEND RESTOPULSE_REDIS_LIBS ${HIREDIS_LIBRARY})
    message(STATUS "hiredis found — Redis hot bridge enabled")
  else()
    message(STATUS "hiredis not found — in-process hot store only")
  endif()
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(IMGUI_SOURCES
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(restopulse
  src/main.cpp
  src/db/ledger.cpp
  src/db/hot_store.cpp
  src/db/hub.cpp
  src/services/pos_app.cpp
  src/ui/app_ui.cpp
  ${IMGUI_SOURCES}
)

target_include_directories(restopulse PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${SQLITE3_INCLUDE_DIR}
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)

if(HIREDIS_INCLUDE_DIR)
  target_include_directories(restopulse PRIVATE ${HIREDIS_INCLUDE_DIR})
endif()

if(TARGET glfw)
  target_link_libraries(restopulse PRIVATE glfw)
else()
  target_include_directories(restopulse PRIVATE ${GLFW_INCLUDE_DIR})
  target_link_libraries(restopulse PRIVATE ${GLFW_LIBRARY})
endif()

target_link_libraries(restopulse PRIVATE
  ${SQLITE3_LIBRARY}
  OpenGL::GL
  ${RESTOPULSE_REDIS_LIBS}
)

if(APPLE)
  target_link_libraries(restopulse PRIVATE
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
  )
endif()

# Headless smoke does not need display when using --smoke after build
install(TARGETS restopulse RUNTIME DESTINATION bin)
