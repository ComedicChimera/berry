#ifndef PLATFORM_H_INC
#define PLATFORM_H_INC

#ifdef __clang__
    #define COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define COMPILER_GCC 1
#elif defined(_MSC_VER)
    #define COMPILER_MSVC 1
#else 
    #error missing compiler detection
#endif

#ifndef COMPILER_CLANG
    #define COMPILER_CLANG 0
#endif

#ifndef COMPILER_GCC
    #define COMPILER_GCC 0
#endif

#ifndef COMPILER_MSVC
    #define COMPILER_MSVC 0
#endif

/* -------------------------------------------------------------------------- */

#if defined(_WIN32) || defined(_WIN64)
    #define OS_WINDOWS 1
#elif defined(__APPLE__) && defined(__MACH__)
    #define OS_DARWIN 1
#elif defined(__UNIX__)
    #define OS_UNIX 1

    #ifdef __linux__
        #define OS_LINUX 1
    #elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
        #define OS_FREEBSD 1
    #elif defined(__OpenBSD__)
        #define OS_OPENBSD 1
    #else
        #error missing unix detection
    #endif
#else
    #error missing OS detection
#endif

#ifndef OS_WINDOWS
    #define OS_WINDOWS 0
#endif

#ifndef OS_DARWIN
    #define OS_DARWIN 0
#endif

#ifndef OS_UNIX
    #define OS_UNIX 0
#endif

#ifndef OS_LINUX
    #define OS_LINUX 0
#endif

#ifndef OS_FREEBSD
    #define OS_FREEBSD 0
#endif

#ifndef OS_OPENBSD
    #define OS_OPENBSD 0
#endif

/* -------------------------------------------------------------------------- */

#if defined(__amd64__) || defined(_M_AMD64) || defined(__x86_64__)
    #define ARCH_AMD64 1
#elif defined(__arm__) || defined(_M_ARM)
    #define ARCH_ARM 1
#elif defined(__aarch64__)
    #define ARCH_ARM64 1
#elif defined(__i386__) || defined(_M_IX86) 
    #define ARCH_I386 1
#else
    #error missing architecture detection
#endif

#ifndef ARCH_AMD64
    #define ARCH_AMD64 0
#endif

#ifndef ARCH_ARM
    #define ARCH_ARM 0
#endif

#ifndef ARCH_ARM64
    #define ARCH_ARM64 0
#endif

#ifndef ARCH_I386
    #define ARCH_I386 0
#endif

/* -------------------------------------------------------------------------- */


#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64) || defined(__64BIT__) || defined(__aarch64__)
    #define ARCH_32_BIT 0
	#define ARCH_64_BIT 1
#else
    // Common 16-bit L
	#define ARCH_32_BIT 1
    #define ARCH_64_BIT 0
#endif


#endif