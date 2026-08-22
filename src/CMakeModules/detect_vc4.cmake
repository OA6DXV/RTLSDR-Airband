# Detect a native Raspberry Pi VideoCore IV system when PLATFORM=vulkan is selected.
# This is advisory only: Vulkan remains a valid explicit choice and configuration
# must never fail because a VC4 platform was detected.
set(RTL_AIRBAND_VC4_SYSTEM FALSE)
set(RTL_AIRBAND_VC4_HINT "")

if(PLATFORM STREQUAL "vulkan" AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
		AND NOT CMAKE_CROSSCOMPILING AND EXISTS "/proc/device-tree/compatible")
	file(READ "/proc/device-tree/compatible" RTL_AIRBAND_DT_COMPATIBLE HEX)
	# Device-tree strings are NUL separated. Match the Broadcom SoCs used by
	# Raspberry Pi generations with VideoCore IV. Do not match bcm2711 (Pi 4,
	# VideoCore VI) or newer SoCs.
	string(TOLOWER "${RTL_AIRBAND_DT_COMPATIBLE}" RTL_AIRBAND_DT_COMPATIBLE_HEX)
	foreach(RTL_AIRBAND_VC4_COMPAT "brcm,bcm2835" "brcm,bcm2836" "brcm,bcm2837")
		string(HEX "${RTL_AIRBAND_VC4_COMPAT}" RTL_AIRBAND_VC4_COMPAT_HEX)
		string(TOLOWER "${RTL_AIRBAND_VC4_COMPAT_HEX}" RTL_AIRBAND_VC4_COMPAT_HEX)
		string(FIND "${RTL_AIRBAND_DT_COMPATIBLE_HEX}" "${RTL_AIRBAND_VC4_COMPAT_HEX}" RTL_AIRBAND_VC4_MATCH)
		if(NOT RTL_AIRBAND_VC4_MATCH EQUAL -1)
			set(RTL_AIRBAND_VC4_SYSTEM TRUE)
			break()
		endif()
	endforeach()

	if(RTL_AIRBAND_VC4_SYSTEM)
		set(RTL_AIRBAND_VC4_HINT "VideoCore IV detected: PLATFORM=v4cma is recommended on AArch64 Raspberry Pi systems; PLATFORM=vulkan remains enabled as explicitly requested.")
	endif()
endif()
