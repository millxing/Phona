// Load a plugin library: dlopen on Mac and Linux, LoadLibrary on Windows. Include after rack.hpp.
#pragma once
#include <stdexcept>
#include <string>
#ifdef _WIN32
// Declared here: <windows.h> clashes with the SIMD headers that rack.hpp includes.
extern "C" {
__declspec(dllimport) void* __stdcall LoadLibraryA(const char* name);
__declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
__declspec(dllimport) unsigned long __stdcall GetLastError(void);
}
static const char* const PLUGIN_LIB = "plugin.dll";
inline void* libOpen(const std::string& path) {
	void* h = LoadLibraryA(path.c_str());
	if (!h) throw std::runtime_error("LoadLibrary failed: " + path + " (Windows error " + std::to_string(GetLastError()) + ")");
	return h;
}
inline void* libSym(void* lib, const char* name) { return GetProcAddress(lib, name); }
#else
#include <dlfcn.h>
#ifdef __APPLE__
static const char* const PLUGIN_LIB = "plugin.dylib";
#else
static const char* const PLUGIN_LIB = "plugin.so";
#endif
inline void* libOpen(const std::string& path) {
	void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!h) throw std::runtime_error(dlerror());
	return h;
}
inline void* libSym(void* lib, const char* name) { return dlsym(lib, name); }
#endif
