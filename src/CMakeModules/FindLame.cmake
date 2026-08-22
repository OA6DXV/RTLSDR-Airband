FIND_PATH(LAME_INCLUDE_DIR lame/lame.h)
FIND_LIBRARY(LAME_LIBRARIES NAMES mp3lame)

IF(LAME_INCLUDE_DIR AND LAME_LIBRARIES)
	SET(LAME_FOUND TRUE)
ENDIF(LAME_INCLUDE_DIR AND LAME_LIBRARIES)

IF(LAME_FOUND)
	IF (NOT Lame_FIND_QUIETLY)
		MESSAGE(STATUS "Found lame includes:\t${LAME_INCLUDE_DIR}/lame/lame.h")
		MESSAGE(STATUS "Found lame library: ${LAME_LIBRARIES}")
	ENDIF (NOT Lame_FIND_QUIETLY)
ELSE(LAME_FOUND)
	IF (Lame_FIND_REQUIRED)
		MESSAGE(FATAL_ERROR "lame library required but not found")
	ENDIF (Lame_FIND_REQUIRED)
ENDIF(LAME_FOUND)

# PLATFORM=vulkan is intentionally portable, but on native Raspberry Pi systems
# with VideoCore IV the dedicated AArch64 v4cma backend is normally preferable.
# Keep this advisory: an explicit Vulkan selection must never be rejected.
include(${CMAKE_CURRENT_LIST_DIR}/detect_vc4.cmake)
if(RTL_AIRBAND_VC4_SYSTEM)
	if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.19")
		function(rtl_airband_vc4_vulkan_notice)
			message(WARNING "${RTL_AIRBAND_VC4_HINT}")
		endfunction()
		cmake_language(DEFER CALL rtl_airband_vc4_vulkan_notice)
	else()
		message(WARNING "${RTL_AIRBAND_VC4_HINT}")
	endif()
endif()
