cmake_minimum_required(VERSION 3.8)

# Use pip-installed torch as libtorch
set(TORCH_INSTALL_PREFIX "C:/Users/mypc2/AppData/Local/Programs/Python/Python310/Lib/site-packages/torch" CACHE PATH "Path to libtorch (pip-installed torch)")

project("R3maJ" CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake's find_package searches <prefix>/share/cmake/<PackageName>/ automatically
list(APPEND CMAKE_PREFIX_PATH "${TORCH_INSTALL_PREFIX}")

# Optional CUDA support - only enable if CUDA toolkit is found
find_package(CUDA QUIET)
if(CUDA_FOUND)
    enable_language(CUDA)
    set(CMAKE_CUDA_ARCHITECTURES "75" CACHE STRING "CUDA architectures" FORCE)
    message(STATUS "CUDA found: ${CUDA_TOOLKIT_ROOT_DIR}")
else()
    message(STATUS "CUDA not found - building CPU-only")
endif()

add_subdirectory(thirdparty/GigaLearnCPP-Leak/GigaLearnCPP)

file(GLOB R3MAJ_SRC "src/*.cpp" "src/*.h")
list(FILTER R3MAJ_SRC EXCLUDE REGEX ".*R3maJStarter\\.cpp$")

add_executable(R3maJ ${R3MAJ_SRC})
if(MSVC)
    target_link_options(R3maJ PRIVATE /STACK:8388608)
endif()
target_link_libraries(R3maJ PRIVATE GigaLearnCPP)
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