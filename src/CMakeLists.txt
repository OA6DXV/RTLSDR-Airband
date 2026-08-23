include(CheckCXXCompilerFlag)
include(CheckCXXSymbolExists)
include(CheckCXXSourceCompiles)
include(CheckIncludeFileCXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

if(UNIX OR MINGW)
	add_definitions(-D_FILE_OFFSET_BITS=64)
	# isnormal()
	add_definitions(-D_POSIX_C_SOURCE=200112L)
endif()

CHECK_CXX_COMPILER_FLAG(-pthread CXX_HAS_PTHREAD)
if(CXX_HAS_PTHREAD)
    add_compile_options(-pthread)
endif()

CHECK_CXX_COMPILER_FLAG(-ffast-math CXX_HAS_FFAST_MATH)

# asprintf on MacOS
if(APPLE)
	add_definitions(-D_DARWIN_C_SOURCE)
endif()

# sincosf on linux vs __sincosf on MacOS
set(CMAKE_REQUIRED_DEFINITIONS_ORIG ${CMAKE_REQUIRED_DEFINITIONS})
list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
set(CMAKE_REQUIRED_LIBRARIES_ORIG ${CMAKE_REQUIRED_LIBRARIES})
list(APPEND CMAKE_REQUIRED_LIBRARIES m)
CHECK_SYMBOL_EXISTS(sincosf math.h HAVE_SINCOSF)
if(HAVE_SINCOSF)
	set(SINCOSF "sincosf")
else()
	CHECK_SYMBOL_EXISTS(__sincosf math.h HAVE___SINCOSF)
	if(HAVE___SINCOSF)
		set(SINCOSF "__sincosf")
	endif()
endif()
if(NOT HAVE_SINCOSF AND NOT HAVE___SINCOSF)
	message(FATAL_ERROR "Required function sincosf() is unavailable")
endif()
set(CMAKE_REQUIRED_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS_ORIG})
set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES_ORIG})

find_library(LIBM m REQUIRED)
find_library(LIBDL dl REQUIRED)
find_library(LIBPTHREAD pthread REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(CONFIG REQUIRED libconfig++)
list(APPEND rtl_airband_extra_libs ${CONFIG_LIBRARIES})
list(APPEND rtl_airband_include_dirs ${CONFIG_INCLUDE_DIRS})
list(APPEND link_dirs ${CONFIG_LIBRARY_DIRS})

# Can't use pkg_check_modules here, as some distros do not install lame.pc file
find_package(Lame REQUIRED)
list(APPEND rtl_airband_extra_libs ${LAME_LIBRARIES})
list(APPEND rtl_airband_include_dirs ${LAME_INCLUDE_DIR})

pkg_check_modules(SHOUT REQUIRED shout)
list(APPEND rtl_airband_extra_libs ${SHOUT_LIBRARIES})
list(APPEND rtl_airband_include_dirs ${SHOUT_INCLUDE_DIRS})
list(APPEND link_dirs ${SHOUT_LIBRARY_DIRS})

set(CMAKE_REQUIRED_INCLUDES_SAVE ${CMAKE_REQUIRED_INCLUDES})
set(CMAKE_REQUIRED_LIBRARIES_SAVE ${CMAKE_REQUIRED_LIBRARIES})
set(CMAKE_REQUIRED_LINK_OPTIONS_SAVE ${CMAKE_REQUIRED_LINK_OPTIONS})
set(CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES} ${SHOUT_INCLUDE_DIRS}")
set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES} ${SHOUT_LIBRARIES}")
if ( NOT "${SHOUT_LIBRARY_DIRS}" STREQUAL "" )
	set(CMAKE_REQUIRED_LINK_OPTIONS "-L${SHOUT_LIBRARY_DIRS}")
endif()
set(LIBSHOUT_HEADER "shout/shout.h")
CHECK_CXX_SYMBOL_EXISTS("SHOUT_TLS_AUTO" ${LIBSHOUT_HEADER}
	HAVE_SHOUT_TLS_AUTO)
CHECK_CXX_SYMBOL_EXISTS("SHOUT_TLS_AUTO_NO_PLAIN" ${LIBSHOUT_HEADER}
	HAVE_SHOUT_TLS_AUTO_NO_PLAIN)
CHECK_CXX_SYMBOL_EXISTS("SHOUT_TLS_RFC2818" ${LIBSHOUT_HEADER}
	HAVE_SHOUT_TLS_RFC2818)
CHECK_CXX_SYMBOL_EXISTS("SHOUT_TLS_RFC2817" ${LIBSHOUT_HEADER}
	HAVE_SHOUT_TLS_RFC2817)
CHECK_CXX_SYMBOL_EXISTS("SHOUT_TLS_DISABLED" ${LIBSHOUT_HEADER}
	HAVE_SHOUT_TLS_DISABLED)
CHECK_CXX_SYMBOL_EXISTS("shout_set_tls" ${LIBSHOUT_HEADER}
	HAVE_SHOUT_SET_TLS)
CHECK_CXX_SYMBOL_EXISTS("shout_set_content_format" ${LIBSHOUT_HEADER}
	LIBSHOUT_HAS_CONTENT_FORMAT)

if(HAVE_SHOUT_TLS_AUTO AND HAVE_SHOUT_TLS_AUTO_NO_PLAIN AND
		HAVE_SHOUT_TLS_RFC2818 AND HAVE_SHOUT_TLS_RFC2817 AND
		HAVE_SHOUT_TLS_DISABLED AND HAVE_SHOUT_SET_TLS)
	set(LIBSHOUT_HAS_TLS TRUE)
else()
	set(LIBSHOUT_HAS_TLS FALSE)
endif()

# check for shout_set_metadata_utf8() - introduced in libshout v2.4.6
CHECK_CXX_SYMBOL_EXISTS("shout_set_metadata_utf8" ${LIBSHOUT_HEADER}
	HAVE_SHOUT_SET_METADATA_UTF8)
if(HAVE_SHOUT_SET_METADATA_UTF8)
	set(SHOUT_SET_METADATA "shout_set_metadata_utf8")
else()
	set(SHOUT_SET_METADATA "shout_set_metadata")
endif()

set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES_SAVE})
set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES_SAVE})
set(CMAKE_REQUIRED_LINK_OPTIONS ${CMAKE_REQUIRED_LINK_OPTIONS_SAVE})

option(NFM "Enable support for narrow FM channels" OFF)

set(PLATFORM "native" CACHE STRING "Optimize the build for the given hardware platform")
include(detect_acceleration)

option(RTLSDR "Enable RTL-SDR support" ON)
set(WITH_RTLSDR FALSE)

option(MIRISDR "Enable Mirics support" ON)
set(WITH_MIRISDR FALSE)

option(SOAPYSDR "Enable SoapySDR support" ON)
set(WITH_SOAPYSDR FALSE)

option(PULSEAUDIO "Enable PulseAudio support" ON)
set(WITH_PULSEAUDIO FALSE)

option(PROFILING "Enable profiling with gperftools")
set(WITH_PROFILING FALSE)

if(RTLSDR)
	find_package(RTLSDR)
	if(RTLSDR_FOUND)
		list(APPEND rtl_airband_extra_sources input-rtlsdr.cpp)
		list(APPEND rtl_airband_extra_libs ${RTLSDR_LIBRARIES})
		list(APPEND rtl_airband_include_dirs ${RTLSDR_INCLUDE_DIRS})
		list(APPEND link_dirs ${RTLSDR_LIBRARY_DIRS})
		set(WITH_RTLSDR TRUE)
	endif()
endif()

if(MIRISDR)
	find_package(MiriSDR)
	if(MIRISDR_FOUND)
		set(WITH_MIRISDR TRUE)
		list(APPEND rtl_airband_extra_sources input-mirisdr.cpp)
		list(APPEND rtl_airband_extra_libs ${MIRISDR_LIBRARIES})
		list(APPEND rtl_airband_include_dirs ${MIRISDR_INCLUDE_DIRS})
		list(APPEND link_dirs ${MIRISDR_LIBRARY_DIRS})
	endif()
endif()

if(SOAPYSDR)
	message(STATUS "Checking for SoapySDR")
	find_package(SoapySDR NO_MODULE)
	if(SoapySDR_FOUND)
		list(APPEND rtl_airband_extra_sources input-soapysdr.cpp)
		message(STATUS "  SoapySDR found, ${SoapySDR_INCLUDE_DIRS}, ${SoapySDR_LIBRARIES}")
		list(APPEND rtl_airband_extra_libs ${SoapySDR_LIBRARIES})
		list(APPEND rtl_airband_include_dirs ${SoapySDR_INCLUDE_DIRS})
		set(WITH_SOAPYSDR TRUE)
	else()
		message(STATUS "  SoapySDR not found")
	endif()
endif()

if(PULSEAUDIO)
	pkg_check_modules(PULSEAUDIO libpulse)
	if(PULSEAUDIO_FOUND)
		list(APPEND rtl_airband_extra_sources pulse.cpp)
		list(APPEND rtl_airband_extra_libs ${PULSEAUDIO_LIBRARIES})
		list(APPEND rtl_airband_include_dirs ${PULSEAUDIO_INCLUDE_DIRS})
		list(APPEND link_dirs ${PULSEAUDIO_LIBRARY_DIRS})
		set(WITH_PULSEAUDIO TRUE)
	endif()
endif()

if(PROFILING)
	pkg_check_modules(PROFILING libprofiler)
	if(PROFILING_FOUND)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
		list(APPEND rtl_airband_extra_libs ${PROFILING_LIBRARIES})
		list(APPEND rtl_airband_include_dirs ${PROFILING_INCLUDE_DIRS})
		list(APPEND link_dirs ${PROFILING_LIBRARY_DIRS})
		set(WITH_PROFILING TRUE)
	endif()
endif()

option(BCM_VC "Enable Broadcom Videocore 3 support" OFF)
set(WITH_BCM_VC FALSE)
set(VC4_CMA FALSE)
set(WITH_VULKAN FALSE)

if(PLATFORM STREQUAL "rpiv1" OR PLATFORM STREQUAL "armv7-generic" OR PLATFORM STREQUAL "armv8-generic")
	message(FATAL_ERROR "platform '${PLATFORM}' has been deprecated, see https://github.com/rtl-airband/RTLSDR-Airband/discussions/447")
elseif(PLATFORM STREQUAL "rpiv2")
	set(BCM_VC ON)
	add_compile_options(-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard)
	enable_language(ASM)
	list(APPEND rtl_airband_extra_sources rtl_airband_neon.s)
elseif(PLATFORM STREQUAL "v4cma")
	check_cxx_source_compiles("
#ifndef __aarch64__
#error Not an AArch64 compiler
#endif
int main() { return 0; }
" RTL_AIRBAND_COMPILER_ARM64)
	if(NOT RTL_AIRBAND_COMPILER_ARM64)
		message(FATAL_ERROR "PLATFORM=v4cma requires an AArch64 compiler")
	endif()
	check_include_file_cxx("linux/dma-heap.h" HAVE_LINUX_DMA_HEAP_H)
	if(NOT HAVE_LINUX_DMA_HEAP_H)
		message(FATAL_ERROR "PLATFORM=v4cma requires linux/dma-heap.h")
	endif()
	set(BCM_VC ON)
	set(VC4_CMA TRUE)
	add_compile_options(-march=armv8-a)
	add_compile_definitions(HAVE_VC4_CMA)
	list(APPEND rtl_airband_extra_sources samplefft_v4cma.cpp)
elseif(PLATFORM STREQUAL "vulkan")
	set(WITH_VULKAN TRUE)

	find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h)
	find_library(VULKAN_LIBRARY NAMES vulkan)
	if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY)
		message(FATAL_ERROR "PLATFORM=vulkan requires Vulkan headers and loader library")
	endif()

	find_program(VULKAN_GLSLC glslc)
	find_program(VULKAN_GLSLANG_VALIDATOR glslangValidator)
	if(NOT VULKAN_GLSLC AND NOT VULKAN_GLSLANG_VALIDATOR)
		message(FATAL_ERROR "PLATFORM=vulkan requires glslc or glslangValidator to compile the FFT compute shader")
	endif()

	set(VULKAN_SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/vulkan_fft.comp")
	set(VULKAN_SHADER_SPV "${CMAKE_CURRENT_BINARY_DIR}/vulkan_fft.spv")
	set(VULKAN_SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/vulkan_fft_spv.h")
	if(VULKAN_GLSLC)
		add_custom_command(
			OUTPUT ${VULKAN_SHADER_SPV}
			COMMAND ${VULKAN_GLSLC} -O --target-env=vulkan1.0 -fshader-stage=compute
				-o ${VULKAN_SHADER_SPV} ${VULKAN_SHADER_SOURCE}
			DEPENDS ${VULKAN_SHADER_SOURCE}
			VERBATIM
		)
	else()
		add_custom_command(
			OUTPUT ${VULKAN_SHADER_SPV}
			COMMAND ${VULKAN_GLSLANG_VALIDATOR} -V --target-env vulkan1.0 -S comp
				-o ${VULKAN_SHADER_SPV} ${VULKAN_SHADER_SOURCE}
			DEPENDS ${VULKAN_SHADER_SOURCE}
			VERBATIM
		)
	endif()
	add_custom_command(
		OUTPUT ${VULKAN_SHADER_HEADER}
		COMMAND ${CMAKE_COMMAND} -DINPUT=${VULKAN_SHADER_SPV} -DOUTPUT=${VULKAN_SHADER_HEADER}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules/embed_binary.cmake
		DEPENDS ${VULKAN_SHADER_SPV} ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules/embed_binary.cmake
		VERBATIM
	)
	set_source_files_properties(${VULKAN_SHADER_HEADER} PROPERTIES GENERATED TRUE)
	list(APPEND rtl_airband_extra_sources vulkan_fft.cpp ${VULKAN_SHADER_HEADER})
	list(APPEND rtl_airband_extra_libs ${VULKAN_LIBRARY})
	list(APPEND rtl_airband_include_dirs ${VULKAN_INCLUDE_DIR})
elseif(PLATFORM STREQUAL "native")
	CHECK_CXX_COMPILER_FLAG(-march=native CXX_HAS_MARCH_NATIVE)
	if(CXX_HAS_MARCH_NATIVE)
		add_compile_options(-march=native)
	else()
		message(FATAL_ERROR "Cannot build with PLATFORM=native: the compiler does not support -march=native option")
	endif()
elseif(PLATFORM STREQUAL "generic")
	# NO-OP
else()
	message(FATAL_ERROR "Unknown platform '${PLATFORM}'. Valid options are: rpiv2, v4cma, vulkan, native, and generic")
endif()

if(BCM_VC)
	add_subdirectory(hello_fft)
	list(APPEND rtl_airband_obj_files $<TARGET_OBJECTS:hello_fft>)
	set(WITH_BCM_VC TRUE)
endif()
if(NOT BCM_VC)
	pkg_check_modules(FFTW3F REQUIRED fftw3f)
	if(FFTW3F_FOUND)
		list(APPEND rtl_airband_extra_libs ${FFTW3F_LIBRARIES})
		list(APPEND rtl_airband_include_dirs ${FFTW3F_INCLUDE_DIRS})
		list(APPEND link_dirs ${FFTW3F_LIBRARY_DIRS})
	endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	list(APPEND rtl_airband_extra_libs c++)
endif()

if(BUILD_UNITTESTS)
	set(BUILD_UNITTESTS TRUE)
else()
	set(BUILD_UNITTESTS FALSE)
endif()

message(STATUS "RTLSDR-Airband configuration summary:\n")
message(STATUS "- Version string:\t\t${RTL_AIRBAND_VERSION}")
message(STATUS "- Build type:\t\t${CMAKE_BUILD_TYPE}")
message(STATUS "- Operating system:\t\t${CMAKE_SYSTEM_NAME}")
message(STATUS "- SDR drivers:")
message(STATUS "  - librtlsdr:\t\trequested: ${RTLSDR}, enabled: ${WITH_RTLSDR}")
message(STATUS "  - mirisdr:\t\t\trequested: ${MIRISDR}, enabled: ${WITH_MIRISDR}")
message(STATUS "  - soapysdr:\t\trequested: ${SOAPYSDR}, enabled: ${WITH_SOAPYSDR}")
message(STATUS "- Other options:")
message(STATUS "  - Platform:\t\t${PLATFORM}")
message(STATUS "  - Build Unit Tests:\t${BUILD_UNITTESTS}")
message(STATUS "  - Broadcom VideoCore GPU:\t${WITH_BCM_VC}")
message(STATUS "  - Vulkan Compute FFT:\t${WITH_VULKAN}")
message(STATUS "  - VideoCore IV CMA backend:\t${VC4_CMA}")
message(STATUS "  - NFM support:\t\t${NFM}")
message(STATUS "  - PulseAudio:\t\trequested: ${PULSEAUDIO}, enabled: ${WITH_PULSEAUDIO}")
message(STATUS "  - Profiling:\t\trequested: ${PROFILING}, enabled: ${WITH_PROFILING}")
message(STATUS "  - Icecast TLS support:\t${LIBSHOUT_HAS_TLS}")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config.h)
	message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/config.h nolonger used, delete before continuing")
endif()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/config.h"
	@ONLY
)

add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
		${CMAKE_CURRENT_BINARY_DIR}/_version.cpp
	COMMAND ${CMAKE_COMMAND} -DRTL_AIRBAND_VERSION=${RTL_AIRBAND_VERSION} -P
	${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules/version.cmake
)

add_library (rtl_airband_base OBJECT
	config.cpp
	input-common.cpp
	input-file.cpp
	input-helpers.cpp
	mixer.cpp
	output.cpp
	rtl_airband.cpp
	squelch.cpp
	ctcss.cpp
	util.cpp
	udp_stream.cpp
	logging.cpp
	filters.cpp
	helper_functions.cpp
	${CMAKE_CURRENT_BINARY_DIR}/version.cpp
	${rtl_airband_extra_sources}
	)

target_include_directories (rtl_airband_base PUBLIC
	${CMAKE_CURRENT_BINARY_DIR}
	${rtl_airband_include_dirs}
)

if(CXX_HAS_FFAST_MATH)
    target_compile_options(rtl_airband_base PRIVATE -ffast-math)
endif()

link_directories(${link_dirs})
list(APPEND rtl_airband_obj_files $<TARGET_OBJECTS:rtl_airband_base>)

add_executable (rtl_airband ${rtl_airband_obj_files})
set_property(TARGET rtl_airband PROPERTY ENABLE_EXPORTS 1)

if(RTL_AIRBAND_ACCEL_HINTS AND (PLATFORM STREQUAL "native" OR PLATFORM STREQUAL "generic"))
	add_custom_command(TARGET rtl_airband POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E echo ""
		COMMAND ${CMAKE_COMMAND} -E echo "RTLSDR-Airband acceleration recommendations:"
		VERBATIM)
	foreach(RTL_AIRBAND_ACCEL_HINT IN LISTS RTL_AIRBAND_ACCEL_HINTS)
		add_custom_command(TARGET rtl_airband POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E echo "WARNING: ${RTL_AIRBAND_ACCEL_HINT}"
			VERBATIM)
	endforeach()
	add_custom_command(TARGET rtl_airband POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E echo "Selected PLATFORM=${PLATFORM} is unchanged."
		VERBATIM)
elseif(PLATFORM STREQUAL "vulkan" AND RTL_AIRBAND_VC4_SYSTEM)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(RTL_AIRBAND_SELECTED_PLATFORM_HINT "VideoCore IV detected on a 64-bit ARM userspace: PLATFORM=v4cma is recommended for the dedicated VC4 CMA/QPU backend; PLATFORM=vulkan remains enabled as requested.")
	else()
		set(RTL_AIRBAND_SELECTED_PLATFORM_HINT "VideoCore IV detected: PLATFORM=rpiv2 is the dedicated 32-bit VC4 backend; PLATFORM=vulkan remains enabled as requested.")
	endif()
	add_custom_command(TARGET rtl_airband POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E echo "WARNING: ${RTL_AIRBAND_SELECTED_PLATFORM_HINT}"
		VERBATIM)
endif()

target_include_directories (rtl_airband PUBLIC
	${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries (rtl_airband
	dl
	m
	pthread
	${rtl_airband_extra_libs}
)

install(TARGETS rtl_airband
	RUNTIME DESTINATION bin
)

if(BUILD_UNITTESTS)
	cmake_minimum_required(VERSION 3.1...3.18 FATAL_ERROR)
	set(CMAKE_CXX_STANDARD 14)
	if(POLICY CMP0135)
		cmake_policy(SET CMP0135 NEW)
	endif()
	include(FetchContent)
	FetchContent_Declare(
		googletest
		URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
	)
	FetchContent_MakeAvailable(googletest)
	enable_testing()
	file(GLOB_RECURSE TEST_FILES "test_*.cpp")
	list(APPEND TEST_FILES
		squelch.cpp
		logging.cpp
		filters.cpp
		ctcss.cpp
		generate_signal.cpp
		helper_functions.cpp
	)
	add_executable(
		unittests
		${TEST_FILES}
	)
	target_link_libraries(
		unittests
		GTest::gtest_main
		dl
		${rtl_airband_extra_libs}
	)
	target_include_directories (unittests PUBLIC
		${CMAKE_CURRENT_BINARY_DIR}
	)
	include(GoogleTest)
	gtest_discover_tests(unittests)
endif()
