cmake_minimum_required(VERSION 3.28)
project(SunriseEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(SOURCES 
    src/Graphics/Window.cpp
    src/Graphics/GraphicsContext.cpp
    src/Graphics/ResourceFactory.cpp
    src/Graphics/ShaderCache.cpp
    src/Graphics/GraphicsCommands.cpp
)

set(HEADERS
    include/Logger.h
    include/Application.h
    include/EntryPoint.h
    include/SceneManagement/BaseScene.h
)
    
find_package(Vulkan REQUIRED)

FetchContent_Declare(glm 
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        master
)

FetchContent_MakeAvailable(glm)

FetchContent_Declare(glfw3
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(glfw3)

FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        master
)

FetchContent_MakeAvailable(vma)

add_library(SunriseEngine STATIC 
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(SunriseEngine PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(SunriseEngine PUBLIC
    glm::glm
    glfw
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
)
