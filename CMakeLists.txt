cmake_minimum_required(VERSION 3.19)

project(SurfaceReconstruction LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(Alembic CONFIG REQUIRED)

set(REACTIVE_BUILD_SAMPLES OFF CACHE BOOL "" FORCE) # Remove samples
add_subdirectory(reactive) # Add Reactive

file(GLOB_RECURSE sources src/*.cpp)
file(GLOB_RECURSE headers src/*.hpp)
file(GLOB shaders shader/*.glsl shader/*.comp shader/*.vert shader/*.frag shader/*.mesh shader/*.task shader/*.inc)
add_executable(${PROJECT_NAME} ${sources} ${headers} ${shaders})

source_group("Shader Files" FILES ${shaders})

target_link_libraries(${PROJECT_NAME} PUBLIC 
    reactive
    Alembic::Alembic
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PROJECT_SOURCE_DIR}
    Reactive/include
    PhysX/physx/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    "SHADER_DIR=std::string{\"${CMAKE_CURRENT_SOURCE_DIR}/shader/\"}")
    
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "ASSET_DIR=std::string{\"${CMAKE_CURRENT_SOURCE_DIR}/asset/\"}")

if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
endif()
