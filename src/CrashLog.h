#pragma once

#include <JuceHeader.h>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

// Crash logging system for Legion Stage.
// Catches unhandled exceptions and signals, writes a timestamped log file
// next to the executable.

namespace CrashLog
{

static juce::File getLogFile()
{
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    return exeDir.getChildFile("crash.log");
}

static void writeEntry(const std::string& reason, const std::string& detail = "")
{
    try
    {
        auto logFile = getLogFile();
        std::ofstream out(logFile.getFullPathName().toStdString(), std::ios::app);
        if (!out.is_open()) return;

        // Timestamp
        std::time_t now = std::time(nullptr);
        char timeBuf[64];
        std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

        out << "=== CRASH REPORT ===" << std::endl;
        out << "Time:    " << timeBuf << std::endl;
        out << "App:     Legion Stage" << std::endl;
        out << "Reason:  " << reason << std::endl;
        if (!detail.empty())
            out << "Detail:  " << detail << std::endl;

#ifdef _WIN32
        // Walk the stack if possible
        out << "Stack:" << std::endl;
        void* stack[64];
        HANDLE process = GetCurrentProcess();
        SymInitialize(process, NULL, TRUE);
        WORD frames = CaptureStackBackTrace(0, 64, stack, NULL);

        SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
        if (symbol)
        {
            symbol->MaxNameLen = 255;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

            for (WORD i = 0; i < frames; ++i)
            {
                SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
                out << "  [" << i << "] " << symbol->Name << " (0x"
                    << std::hex << symbol->Address << std::dec << ")" << std::endl;
            }
            free(symbol);
        }
        SymCleanup(process);
#endif

        out << std::endl;
        out.flush();
    }
    catch (...) {}
}

// Signal handler for SIGSEGV, SIGABRT, SIGFPE, etc.
static void signalHandler(int sig)
{
    const char* name = "Unknown signal";
    switch (sig)
    {
        case SIGSEGV: name = "SIGSEGV (Segmentation fault)"; break;
        case SIGABRT: name = "SIGABRT (Abort)"; break;
        case SIGFPE:  name = "SIGFPE (Floating point exception)"; break;
        case SIGILL:  name = "SIGILL (Illegal instruction)"; break;
        case SIGTERM: name = "SIGTERM (Terminated)"; break;
    }
    writeEntry("Signal caught", name);
    std::_Exit(1);
}

#ifdef _WIN32
// Windows structured exception handler
static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* info)
{
    std::string detail = "Exception code: 0x" +
        ([](DWORD code) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%08lX", code);
            return std::string(buf);
        })(info->ExceptionRecord->ExceptionCode);

    switch (info->ExceptionRecord->ExceptionCode)
    {
        case EXCEPTION_ACCESS_VIOLATION:     detail += " (Access violation)"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: detail += " (Array bounds exceeded)"; break;
        case EXCEPTION_STACK_OVERFLOW:       detail += " (Stack overflow)"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:   detail += " (Float divide by zero)"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:   detail += " (Integer divide by zero)"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:  detail += " (Illegal instruction)"; break;
    }

    detail += " at address 0x";
    char addrBuf[32];
    snprintf(addrBuf, sizeof(addrBuf), "%p", info->ExceptionRecord->ExceptionAddress);
    detail += addrBuf;

    writeEntry("Unhandled Windows exception", detail);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// Call this once at app startup
static void install()
{
    // Register signal handlers
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE,  signalHandler);
    std::signal(SIGILL,  signalHandler);
    std::signal(SIGTERM, signalHandler);

#ifdef _WIN32
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
#endif

    // Log that we started cleanly
    writeEntry("App started", "Clean launch");
}

} // namespace CrashLog
