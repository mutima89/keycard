# Standalone build of the Keycard simulation core and its unit tests.
# The same sources compile inside Unreal as the KeycardSim module (Source/KeycardSim).
cmake_minimum_required(VERSION 3.20)
project(KeycardCore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB KC_SIM_SOURCES CONFIGURE_DEPENDS Source/KeycardSim/Private/Kc*.cpp)
add_library(KeycardSimCore STATIC ${KC_SIM_SOURCES})
target_include_directories(KeycardSimCore PUBLIC Source/KeycardSim/Public)
# UE builds without exceptions or RTTI; keep the core honest about that.
if(MSVC)
  target_compile_options(KeycardSimCore PRIVATE /W4 /GR-)
else()
  target_compile_options(KeycardSimCore PRIVATE -Wall -Wextra -Wpedantic -Wshadow -fno-exceptions -fno-rtti)
endif()

enable_testing()
file(GLOB KC_TEST_SOURCES CONFIGURE_DEPENDS Tests/Core/*.cpp)
add_executable(KeycardCoreTests ${KC_TEST_SOURCES})
target_link_libraries(KeycardCoreTests PRIVATE KeycardSimCore)
add_test(NAME KeycardCoreTests COMMAND KeycardCoreTests)
# Content tests read the shipped mission files.
target_compile_definitions(KeycardCoreTests PRIVATE KC_DATA_DIR="${CMAKE_SOURCE_DIR}/Data")
