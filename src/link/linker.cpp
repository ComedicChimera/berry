#include "linker.hpp"

#include <iostream>

#include "vendor/microsoft_craziness.h"

static bool runWindowsLinker(LinkConfig& cfg, Find_Result& win_data) {
    std::wcout << win_data.vs_exe_path << L", " << win_data.windows_sdk_version << '\n';
    return true;
}

bool RunLinker(LinkConfig& cfg) {
    Find_Result win_data = find_visual_studio_and_windows_sdk();
    if (!win_data.windows_sdk_version) {
        free_resources(&win_data);
        return false;
    }

    bool result = runWindowsLinker(cfg, win_data);
    free_resources(&win_data);
    return result;
}

/* -------------------------------------------------------------------------- */

void RemoveObjFile(const std::string& fpath) {
    if (!DeleteFileA(fpath.c_str())) {
        auto err_code = GetLastError();
        LPTSTR buff = nullptr;

        if (!FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
            nullptr, 
            err_code, 0,
            (LPTSTR)(&buff),
            0, nullptr  
        )) {
            std::cout << "error: failed to delete object file\n";
        } else {
            std::cout << "error: failed to delete object file: " << buff << '\n';
            LocalFree(buff);
        }
    }
}