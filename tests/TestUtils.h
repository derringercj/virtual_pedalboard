#pragma once

#include <cmath>
#include <cstdio>

namespace test
{
    inline int failures = 0;

    inline void check (const char* what, double actual, double expected, double tolerance)
    {
        const bool ok = std::abs (actual - expected) <= tolerance;

        if (! ok)
            ++failures;

        std::printf ("  %-50s %9.4f  (want %8.4f +/-%-6g) %s\n",
                     what, actual, expected, tolerance, ok ? "ok" : "FAIL");
    }

    inline void checkTrue (const char* what, bool condition)
    {
        if (! condition)
            ++failures;

        std::printf ("  %-50s %-42s %s\n", what, "", condition ? "ok" : "FAIL");
    }
}
