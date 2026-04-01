#pragma once

/**
 * DLL export macros for qutrit Clifford modules.
 * 
 * Define QMINIWASM_DLL_EXPORTS when building the shared library.
 * When consuming the library, the macros expand to dllimport on Windows.
 */

#ifdef _WIN32
    #ifdef QMINIWASM_DLL_EXPORTS
        #define QUTRIT_API __declspec(dllexport)
    #else
        #define QUTRIT_API __declspec(dllimport)
    #endif
    #define QUTRIT_LOCAL
#else
    #ifdef QMINIWASM_DLL_EXPORTS
        #define QUTRIT_API __attribute__((visibility("default")))
    #else
        #define QUTRIT_API
    #endif
    #define QUTRIT_LOCAL __attribute__((visibility("hidden")))
#endif

/**
 * Version information for the qutrit Clifford library.
 */
#define QUTRIT_VERSION_MAJOR 0
#define QUTRIT_VERSION_MINOR 2
#define QUTRIT_VERSION_PATCH 0

/**
 * ABI version for binary compatibility checks.
 */
#define QUTRIT_ABI_VERSION 1