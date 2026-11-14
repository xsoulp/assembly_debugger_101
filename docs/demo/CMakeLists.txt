cmake_minimum_required(VERSION 3.22)
project(ASM101Demo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_DISABLE_FIND_PACKAGE_XKB TRUE)
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
get_filename_component(ASM101_ROOT "../.." ABSOLUTE)
add_executable(record_demo
    record.cpp
    ${ASM101_ROOT}/src/MainWindow.cpp
    ${ASM101_ROOT}/src/DebuggerEngine.cpp
    ${ASM101_ROOT}/src/MemoryModel.cpp
    ${ASM101_ROOT}/src/RegisterModel.cpp
    ${ASM101_ROOT}/src/AssemblyEditor.cpp
    ${ASM101_ROOT}/src/MovAnimation.cpp
    ${ASM101_ROOT}/resources/asm101.qrc)
target_include_directories(record_demo PRIVATE ${ASM101_ROOT}/src)
target_link_libraries(record_demo PRIVATE Qt6::Widgets Qt6::Test)
target_compile_definitions(record_demo PRIVATE EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/example.asm")
