cmake_minimum_required(VERSION 3.18)

project("R3maJ" CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Discover Python so pip-installed PyTorch can provide its CMake package path.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import torch; print(torch.utils.cmake_prefix_path)"
    RESULT_VARIABLE TORCH_PATH_RESULT
    OUTPUT_VARIABLE TORCH_INSTALL_PREFIX
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE TORCH_PATH_ERROR
)

if(NOT TORCH_PATH_RESULT EQUAL 0 OR NOT TORCH_INSTALL_PREFIX)
    message(FATAL_ERROR
        "Could not locate PyTorch's CMake package. Make sure Python can import torch.\n"
        "Python: ${Python3_EXECUTABLE}\n"
        "Error: ${TORCH_PATH_ERROR}"
    )
endif()

list(APPEND CMAKE_PREFIX_PATH "${TORCH_INSTALL_PREFIX}")
message(STATUS "PyTorch CMake prefix: ${TORCH_INSTALL_PREFIX}")

# Modern CUDA detection. CUDAToolkit is sufficient for the GigaLearn/PyTorch
# CUDA build and avoids CMake's deprecated FindCUDA module.
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    enable_language(CUDA)
    set(CMAKE_CUDA_ARCHITECTURES "75" CACHE STRING "CUDA architectures" FORCE)
    message(STATUS "CUDA toolkit found: ${CUDAToolkit_VERSION}")
    message(STATUS "CUDA compiler: ${CMAKE_CUDA_COMPILER}")
else()
    message(STATUS "CUDA toolkit not found - building CPU-only")
endif()

find_package(Torch REQUIRED)

add_subdirectory(thirdparty/GigaLearnCPP-Leak/GigaLearnCPP)

file(GLOB R3MAJ_SRC "src/*.cpp" "src/*.h")
list(FILTER R3MAJ_SRC EXCLUDE REGEX ".*R3maJStarter\\.cpp$")

add_executable(R3maJ ${R3MAJ_SRC})

if(MSVC)
    target_link_options(R3maJ PRIVATE /STACK:8388608)
endif()

target_link_libraries(R3maJ PRIVATE GigaLearnCPP Torch::Torch)
target_include_directories(R3maJ PRIVATE "src")

add_custom_command(TARGET R3maJ POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "$<TARGET_FILE_DIR:GigaLearnCPP>"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying GigaLearnCPP DLLs to output..."
)

add_custom_command(TARGET R3maJ POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/collision_meshes"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/collision_meshes"
    COMMENT "Copying collision meshes to output..."
)

add_custom_command(TARGET R3maJ POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/thirdparty/GigaLearnCPP-Leak/GigaLearnCPP/python_scripts"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/python_scripts"
    COMMENT "Copying Python scripts to output..."
)

if(WIN32)
    add_executable(R3maJStarter WIN32 src/R3maJStarter.cpp)

    add_custom_command(TARGET R3maJStarter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:R3maJStarter>"
            "$<TARGET_FILE_DIR:R3maJ>/"
        COMMENT "Copying R3maJStarter to output..."
    )

    add_custom_command(TARGET R3maJStarter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:R3maJStarter>"
            "$ENV{APPDATA}/Microsoft/Windows/Start Menu/Programs/Startup/"
        COMMENT "Registering R3maJStarter for automatic startup..."
    )
endif()
