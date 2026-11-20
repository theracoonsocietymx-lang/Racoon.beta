#pragma once
// setenv / unsetenv for the test harness on Windows (POSIX has them; the Windows C runtime has _putenv_s)
#if defined(_WIN32)
#include <cstdlib>
inline int setenv (const char* name, const char* value, int overwrite)
{
    if (! overwrite && std::getenv (name) != nullptr) return 0;
    return _putenv_s (name, value);
}
inline int unsetenv (const char* name) { return _putenv_s (name, ""); }
#endif
