cmake_minimum_required(VERSION 3.25)
project(moodle-storage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable CCache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
else()
    message(STATUS "ccache not found, proceeding without it.")
endif()

# CPM.cmake - Dependency Manager
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_0.38.2.cmake")
if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
        "https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.2/CPM.cmake"
        "${CPM_DOWNLOAD_LOCATION}"
    )
endif()
include("${CPM_DOWNLOAD_LOCATION}")

# Prefer local system packages instead of building from source if available
set(CPM_USE_LOCAL_PACKAGES ON)

# Dependencies
CPMAddPackage(
    NAME cpr
    GITHUB_REPOSITORY libcpr/cpr
    GIT_TAG 1.10.5
)
CPMAddPackage("gh:nlohmann/json@3.11.3")
CPMAddPackage("gh:gabime/spdlog@1.12.0")
CPMAddPackage("gh:CLIUtils/CLI11@2.4.2")
CPMAddPackage("gh:ArthurSonzogni/FTXUI@5.0.0")
CPMAddPackage("gh:google/googletest@1.14.0")

find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(libsecret libsecret-1 REQUIRED)

# Includes
include_directories(src ${libsecret_INCLUDE_DIRS})

# Common compile options
add_compile_options(-Wall -Wextra -Wpedantic)

# Source groups
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.hpp")

# Separate library sources (everything except main.cpp)
set(LIB_SOURCES ${SOURCES})
list(REMOVE_ITEM LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(mstorage_core STATIC ${LIB_SOURCES})
target_link_libraries(mstorage_core
    PUBLIC
    cpr::cpr
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    CLI11::CLI11
    ftxui::screen
    ftxui::dom
    ftxui::component
    ${SQLite3_LIBRARIES}
    ${libsecret_LIBRARIES}
)
target_include_directories(mstorage_core PUBLIC ${SQLite3_INCLUDE_DIRS})

# Main executable
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE mstorage_core)

# Automatically copy compile_commands.json to root
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMENT "Copying compile_commands.json to project root"
)

# Testing
enable_testing()
add_subdirectory(tests)
