#include "linker.hpp"

#include <iostream>
#include <locale>
#include <codecvt>

#include "vendor/microsoft_craziness.h"

static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

static std::string toNormalStr(const std::wstring& wstr) {
    return converter.to_bytes(wstr);
}

static std::string quoted(const std::string& str) {
    return std::format("\"{}\"", str);
}

static std::string quoted(const std::wstring& wstr) {
    return std::format("\"{}\"", toNormalStr(wstr));
}

static void handleWindowsError(const std::string& base_msg) {
    auto err_code = GetLastError();
    LPTSTR buff = nullptr;

    if (!FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
        nullptr, 
        err_code, 0,
        (LPTSTR)(&buff),
        0, nullptr  
    )) {
        std::cerr << "error: " << base_msg << "\n\n";
    } else {
        std::cerr << "error: " << base_msg << ": " << buff << "\n\n";
        LocalFree(buff);
    }
}

static bool runWindowsLinker(LinkConfig& cfg, Find_Result& win_data) {
    // Get the link path.
    std::string link_path { toNormalStr(win_data.vs_exe_path) };
    link_path.append("\\link.exe");

    // Build link command.
    std::string command { "link.exe /entry:__LibBerry_Start /subsystem:console /nologo /out:" };
    command.append(cfg.out_path);

    // Generate debug info.
    command.append(" /debug:full");

    // Add Windows library paths
    command.append(" /libpath:");
    command.append(quoted(win_data.vs_library_path));

    command.append(" /libpath:");
    command.append(quoted(win_data.windows_sdk_um_library_path));

    command.append(" /libpath:");
    command.append(quoted(win_data.windows_sdk_ucrt_library_path));

    // Add user provided object files.
    for (const auto& obj_file : cfg.obj_files) {
        command.push_back(' ');

        command.append(quoted(obj_file));
    }

    // Required windows libraries.
    command.append(" kernel32.lib");

    // Create a null-terminated, *mutable* wchar buffer for CreateProcessW to
    // play with (because CreateProcessW can modify the cmd line buffer).
    char* cmd_buff = (char*)malloc(command.size() + 1);
    for (int i = 0; i < command.size(); i++) {
        cmd_buff[i] = command[i];
    }
    cmd_buff[command.size()] = '\0';

    // Create the process pipes for reading linker stdout.
    SECURITY_ATTRIBUTES sa_attr;
    sa_attr.nLength = sizeof(SECURITY_ATTRIBUTES); 
    sa_attr.bInheritHandle = TRUE; 
    sa_attr.lpSecurityDescriptor = NULL; 
    HANDLE in_pipe, out_pipe;
    if (!CreatePipe(&in_pipe, &out_pipe, &sa_attr, 0)) {
        handleWindowsError("creating process pipes");
        return false;
    }

    if (!SetHandleInformation(in_pipe, HANDLE_FLAG_INHERIT, 0)) {
        handleWindowsError("setting handle information");

        CloseHandle(in_pipe);
        CloseHandle(out_pipe);
        return false;
    }

    STARTUPINFO start_info;
    SecureZeroMemory(&start_info, sizeof(STARTUPINFO));
    start_info.cb = sizeof(STARTUPINFO);
    start_info.hStdOutput = out_pipe;
    start_info.dwFlags |= STARTF_USESTDHANDLES;

    // Create the linker process.
    PROCESS_INFORMATION proc_info;
    SecureZeroMemory(&proc_info, sizeof(PROCESS_INFORMATION));
    if (!CreateProcess(
        link_path.c_str(),
        cmd_buff,
        nullptr,
        nullptr,
        TRUE,
        0,
        nullptr,
        nullptr,
        &start_info,
        &proc_info
    )) {
        handleWindowsError("creating linker process");
        return false;
    }

    // Wait for the linker to finish.
    if (WaitForSingleObject(proc_info.hProcess, INFINITE) == WAIT_FAILED) {
        handleWindowsError("waiting on linker process");

        CloseHandle(proc_info.hProcess);
        CloseHandle(proc_info.hThread);
        CloseHandle(in_pipe);
        CloseHandle(out_pipe);
        return false;
    }

    // Get the linker's exit code.
    DWORD exit_code;
    if (!GetExitCodeProcess(proc_info.hProcess, &exit_code)) {
        handleWindowsError("getting linker process exit code");

        CloseHandle(proc_info.hProcess);
        CloseHandle(proc_info.hThread);
        CloseHandle(in_pipe);
        CloseHandle(out_pipe);
        return false;
    }

    // Close the out_pipe, so that we can read from in_pipe.
    CloseHandle(proc_info.hProcess);
    CloseHandle(proc_info.hThread);
    CloseHandle(out_pipe);

    // If there is an error, print the linker's stdout.
    if (exit_code != 0) {
        std::cerr << "error: unresolved link errors:\n\n";

        DWORD n_read;
        char buff[512];
        do {
            if (!ReadFile(in_pipe, buff, 512, &n_read, nullptr)) {
                break;
            }

            std::string data(buff, n_read);
            std::cerr << data;
        } while(n_read > 0);

        std::cerr << "\n\n";
    }

    CloseHandle(in_pipe);
    return exit_code == 0;
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
        handleWindowsError("deleting object file " + fpath);
    }
}