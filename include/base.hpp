#ifndef BASE_H_INC
#define BASE_H_INC

#include <stdint.h>
#include <string>
#include <stdexcept>
#include <vector>
#include <memory>
#include <format>
#include <span>
#include <string_view>

#include "platform.hpp"

typedef uint8_t byte;
typedef int32_t rune;
typedef unsigned int uint;

/* -------------------------------------------------------------------------- */

// Panic prints a fatal error and exits the process.  This is only meant to be
// used for unreachable states in the compiler (asserts, etc.).
template<typename... Args>
[[ noreturn ]] 
inline void Panic(const std::string& fmt, Args&&... args) {
    void impl_Panic(const std::string& msg);

    impl_Panic(std::format(fmt, args...));
}

// Assert panics if cond is false with the given formatted message.
template<typename... Args>
inline void Assert(bool cond, const std::string& fmt, Args&&... args) {
    if (!cond) {
        Panic(fmt, args...);
    }
}

// ReportFatal reports a fatal error during compilation.
template<typename... Args>
inline void ReportFatal(const std::string& fmt, Args&&... args) {
    void impl_Fatal(const std::string& msg);

    impl_Fatal(std::format(fmt, args...));
}

// ReportError reports a non-fatal error during compilation.
template<typename... Args>
inline void ReportError(const std::string& fmt, Args&&... args) {
    void impl_Error(const std::string& msg);

    impl_Error(std::format(fmt, args...));
}

/* -------------------------------------------------------------------------- */

// TextSpan is the location of a range of source text.
struct TextSpan {
    // The line and column of the start of the range.
    size_t start_line, start_col;

    // The line and column of the end of the range.
    size_t end_line, end_col;
};

// SpanOver returns a new text span starting at start and ending at end.
inline TextSpan SpanOver(const TextSpan& start, const TextSpan& end) {
    return { start.start_line, start.start_col, end.end_line, end.end_col };
}

// CompileError is just a signal used to exit out of deeply nested code. The
// thrower should report all appropriate information before throwing.
struct CompileError : public std::exception {
};

// ReportCompileError reports a compile error to the console.
template<typename... Args>
inline void ReportCompileError( 
    const std::string& display_path, 
    const TextSpan& span,
    const std::string& message,
    Args&&... args
) {
    void impl_ReportCompileError(
        const std::string& display_path, 
        const TextSpan& span,
        const std::string& message
    );

    impl_ReportCompileError(display_path, span, std::format(message, args...));
}

// ErrorCount returns the number of errors that have been reported.
int ErrorCount();

#endif