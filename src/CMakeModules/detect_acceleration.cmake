# Advisory platform detection for native Linux builds.
# This module never changes PLATFORM and never makes configuration fail.
# It only records acceleration options that can be shown after a successful build.

set(RTL_AIRBAND_ACCEL_HINTS "")
set(RTL_AIRBAND_VC4_SYSTEM FALSE)
set(RTL_AIRBAND_VULKAN_GPU FALSE)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
	# Detect Raspberry Pi SoCs with VideoCore IV from the native device tree.
	if(EXISTS "/proc/device-tree/compatible")
		file(READ "/proc/device-tree/compatible" RTL_AIRBAND_DT_COMPATIBLE HEX)
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
	endif()

	if(RTL_AIRBAND_VC4_SYSTEM)
		if(CMAKE_SIZEOF_VOID_P EQUAL 4)
			list(APPEND RTL_AIRBAND_ACCEL_HINTS "VideoCore IV detected on a 32-bit ARM userspace: PLATFORM=rpiv2 can use the VideoCore IV FFT backend.")
		elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
			list(APPEND RTL_AIRBAND_ACCEL_HINTS "VideoCore IV detected on a 64-bit ARM userspace: PLATFORM=v4cma can use the VideoCore IV CMA/QPU FFT backend.")
		endif()
	endif()

	# Best-effort Vulkan GPU detection. vulkaninfo is intentionally optional so
	# native/generic builds acquire no new mandatory dependency. Only advertise
	# Vulkan when vulkaninfo reports at least one non-CPU physical device.
	find_program(RTL_AIRBAND_VULKANINFO vulkaninfo)
	if(RTL_AIRBAND_VULKANINFO)
		execute_process(
			COMMAND ${RTL_AIRBAND_VULKANINFO} --summary
			RESULT_VARIABLE RTL_AIRBAND_VULKANINFO_RESULT
			OUTPUT_VARIABLE RTL_AIRBAND_VULKANINFO_OUTPUT
			ERROR_VARIABLE RTL_AIRBAND_VULKANINFO_ERROR
			TIMEOUT 5
		)
		if(RTL_AIRBAND_VULKANINFO_RESULT EQUAL 0)
			string(TOLOWER "${RTL_AIRBAND_VULKANINFO_OUTPUT}" RTL_AIRBAND_VULKANINFO_LOWER)
			# vulkaninfo summary prints deviceType for each physical device. CPU-only
			# implementations such as llvmpipe/lavapipe must not trigger this hint.
			if(RTL_AIRBAND_VULKANINFO_LOWER MATCHES "devicetype[^\n]*(discrete_gpu|integrated_gpu|virtual_gpu|other)")
				set(RTL_AIRBAND_VULKAN_GPU TRUE)
				list(APPEND RTL_AIRBAND_ACCEL_HINTS "A non-CPU Vulkan device was detected: the Vulkan FFT backend may provide GPU acceleration where PLATFORM=vulkan is available.")
			endif()
		endif()
	endif()
endif()
