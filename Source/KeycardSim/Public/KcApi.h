// Export macro for the simulation core. Inside Unreal, KeycardSim is its own module (a DLL in
// editor builds) and UBT defines KEYCARDSIM_API as DLLEXPORT/DLLIMPORT. Core sources include no
// UE headers, so those two are provided here with the same definitions UE uses.
// The standalone CMake build defines nothing and KC_API is empty.
#pragma once

#if defined(KEYCARDSIM_API)
	#if !defined(DLLEXPORT)
		#if defined(_MSC_VER)
			#define DLLEXPORT __declspec(dllexport)
			#define DLLIMPORT __declspec(dllimport)
		#else
			#define DLLEXPORT __attribute__((visibility("default")))
			#define DLLIMPORT __attribute__((visibility("default")))
		#endif
	#endif
	#define KC_API KEYCARDSIM_API
#else
	#define KC_API
#endif

#if defined(_MSC_VER)
// Exported classes hold std:: members; the module and its users always share one compiler and CRT.
#pragma warning(disable : 4251)
#endif
