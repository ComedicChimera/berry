#include "base.hpp"

#include <iostream>

static int err_count = 0;

int ErrorCount() {
    return err_count;
}

/* -------------------------------------------------------------------------- */

void impl_ReportCompileError(
    const std::string& mod_name, 
    const std::string& display_path, 
    const TextSpan& span,
    const std::string& message
) {
    err_count++;

    fprintf(
        stderr, "error: [%s] %s:%zu:%zu: %s\n\n", 
        mod_name.c_str(), display_path.c_str(), 
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
    exit(1);
}
