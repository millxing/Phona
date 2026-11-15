// For test programs only (platform.sh passes it with -include on GCC). The tests make their own
// engine::Engine, which Rack marks PRIVATE. Clang (macOS) only warns about that; GCC stops with an
// error. This header turns GCC's error into the same deprecation warning, for rack.hpp only.
#pragma once
#if !defined(__clang__)
// Standard headers first, so that the macro below cannot touch them.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#define error deprecated
#include <rack.hpp>
#undef error
#endif
