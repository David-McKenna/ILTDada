# Based on what I learnt from https://cliutils.gitlab.io/modern-cmake
cmake_minimum_required(VERSION 3.14...3.19)

# Create a release output by default
set(default_build_type "Release")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
	set(CMAKE_VERBOSE_MAKEFILE ON)
endif()

# Describe the project
project(ilt_dada 	VERSION 0.1.0
							DESCRIPTION "PSRDADA based recorder for international LOFAR stations."
							LANGUAGES C CXX
)

set(ILTD_CLI_VERSION 0.1.0)

if (NOT ILTD_DEFAULT_PORT)
	set(ILTD_DEFAULT_PORT 16130)
endif()

message("Building ILTDada Version ${PROJECT_VERSION} with default port ${ILTD_DEFAULT_PORT}\n\n")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -W -Wall -DALLOW_VERBOSE -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -W -Wall -DALLOW_VERBOSE -g")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")

# Find and install udpPacketManager
message("Configuring udpPacketManager")
include(FetchContent)

# Debug
#set(FETCHCONTENT_QUIET off)

FetchContent_Declare(udpPacketManager
                     GIT_REPOSITORY https://github.com/David-McKenna/udpPacketManager.git
                     GIT_TAG 634faabecd1ab67ee154c3ba18d40153ab25220f
                     )

FetchContent_MakeAvailable(udpPacketManager)

message("")

# Find the OpenMP package on the system, as taken from udpPacketManager (should abstract these between the two of them)

message("Configuring OpenMP...")
find_package(OpenMP REQUIRED)
message("")




# Include compile-time parameters into the main header
configure_file(	"${PROJECT_SOURCE_DIR}/src/lib/ilt_dada.h.in"
				"${PROJECT_SOURCE_DIR}/src/lib/ilt_dada.h"
)


# Setup the base library object
add_library(iltdada STATIC
            src/lib/ilt_dada.c)

add_dependencies(iltdada lofudpman)



# Include all of our headers
target_include_directories(iltdada PUBLIC
		${PROJECT_SOURCE_DIR}/src/lib
)
file(GLOB_RECURSE ILTD_INCLUDE_FILES "${PROJECT_SOURCE_DIR}/src/lib/*.h")

target_link_libraries(iltdada PUBLIC lofudpman)

# Set a few extra compiler options
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES "")
set(CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES "")
set_property(TARGET iltdada PROPERTY LINKER_LANGUAGE CXX)
set_property(TARGET iltdada PROPERTY LINK_WHAT_YOU_USE ON)
#set_property(TARGET iltdada PROPERTY INTERPROCEDURAL_OPTIMIZATION ON) # Static + IPO -> build failures?

target_link_libraries(iltdada PUBLIC OpenMP::OpenMP_CXX OpenMP::OpenMP_C)


# Setup the CLIs
add_executable(ilt_dada_cli src/recorder/ilt_dada_cli.c)
target_link_libraries(ilt_dada_cli PUBLIC iltdada)

add_executable(ilt_dada_dada2disk src/recorder/ilt_dada_dada2disk.c)
target_link_libraries(ilt_dada_dada2disk PUBLIC iltdada)

add_executable(ilt_dada_fill_buffer src/recorder/ilt_dada_fill_buffer.c)
target_link_libraries(ilt_dada_fill_buffer PUBLIC iltdada)


include(CMakePackageConfigHelpers)
write_basic_package_version_file(
		iltdadaConfigVersion.cmake
		VERSION ${PACKAGE_VERSION}
		COMPATIBILITY AnyNewerVersion
)


# Install everything except for the debug fill_buffer CLI
install(TARGETS iltdada ilt_dada_cli ilt_dada_dada2disk
		EXPORT iltdada
		LIBRARY DESTINATION lib
		RUNTIME DESTINATION bin
)
install(FILES ${ILTD_INCLUDE_FILES} DESTINATION include)

# Add the tests directory
#add_subdirectory(tests EXCLUDE_FROM_ALL)
