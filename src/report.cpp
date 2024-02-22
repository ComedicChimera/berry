#include "base.hpp"

#include <iostream>

static int err_count = 0;

int ErrorCount() {
    return err_count;
}

/* -------------------------------------------------------------------------- */

void impl_ReportCompileError(
    const std::string& display_path, 
    const TextSpan& span,
    const std::string& message
) {
    err_count++;

    fprintf(
        stderr, "error: %s:%zu:%zu: %s\n\n", 
        display_path.c_str(), 
        span.start_line, span.start_col, 
        message.c_str()
    );
}

void impl_Panic(const std::string& msg) {
    fprintf(stderr, "panic: %s\n\n", msg.c_str());
    exit(1);
}

void impl_Fatal(const std::string& msg) {
    fprintf(stderr, "fatal: %s\n\n", msg.c_str());
    
    throw CompileError{};
}

void impl_Error(const std::string& msg) {
    err_count++;
    
    fprintf(stderr, "error: %s\n\n", msg.c_str());
}