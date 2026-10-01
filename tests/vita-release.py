#!/usr/bin/env python3
"""Check release diagnostic isolation, map lifetimes, and bitmap opacity fallback."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / 'src/display/bitmap.cpp').read_text()
start = source.index('static float bltNormOpacity(')
body = source.index('{', start)
depth = 1
end = body + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
opacity = source[start:end]

preamble = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include "src/util/boost-hash.h"
#include "src/vita_diagnostic.h"
static_assert(sizeof(BoostHash<int, int>) == sizeof(std::map<int, int>),
              "Diagnostic probes must not change the map wrapper's layout");
#ifdef MKXPZ_VITA_DIAGNOSTICS
static int logCalls = 0;
extern "C" void vitaDiagLog(const char *, const char *, ...) { ++logCalls; }
#endif
struct Bitmap { enum BitmapBltMode { NORMAL, KGL_SUBTRACT }; };
template<class T> T clamp(T n, T lo, T hi) { return std::clamp(n, lo, hi); }
'''
checks = r'''
int main() {
    int evaluated = 0;
    vitaDiagLog("TEST", "%d", ++evaluated);
#ifdef MKXPZ_VITA_DIAGNOSTICS
    assert(evaluated == 1 && logCalls == 1);
#else
    assert(evaluated == 0);
    // Even unavailable diagnostic-only variables must compile out.
    vitaDiagLog("TEST", "%d", diagnosticOnlyVariable);
#endif
    std::weak_ptr<int> lifetime;
    {
        BoostHash<int, std::shared_ptr<int>> map;
        auto value = std::make_shared<int>(7);
        lifetime = value;
        map.insert(1, value);
        value.reset();
        assert(map.contains(1) && *map.value(1) == 7 && !lifetime.expired());
        map.remove(1);
        assert(!map.contains(1) && lifetime.expired());
        value = std::make_shared<int>(8);
        lifetime = value;
        map.insert(2, value);
        value.reset();
        map.clear();
        assert(lifetime.expired() && map.cbegin() == map.cend());
        value = std::make_shared<int>(9);
        lifetime = value;
        map.insert(3, value);
    }
    assert(lifetime.expired());
    for (int opacity : {-10, 0, 1, 128, 254, 255, 300}) {
        const float clamped = std::clamp(opacity, 0, 255);
        const float normal = clamped / 255.0f;
        const float subtract = clamped >= 255 ? 1.0f : clamped / 256.0f;
        assert(std::abs(bltNormOpacity(Bitmap::NORMAL, opacity) - normal) < 1e-6f);
        assert(std::abs(bltNormOpacity(Bitmap::KGL_SUBTRACT, opacity) - subtract) < 1e-6f);
        assert(std::abs(bltNormOpacity(static_cast<Bitmap::BitmapBltMode>(2), opacity) - normal) < 1e-6f);
    }
}
'''
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    cpp = temp / 'release.cpp'
    cpp.write_text(preamble + opacity + checks)
    for diagnostic in (False, True):
        exe = temp / ('diagnostic' if diagnostic else 'release')
        flags = ['-DMKXPZ_VITA_DIAGNOSTICS'] if diagnostic else []
        subprocess.run(['c++', '-std=c++17', '-O0', '-Wall', '-Wextra',
                        '-Werror=return-type', '-D__vita__', *flags, '-I', str(ROOT),
                        str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('PASS: diagnostic argument isolation, map layout/lifetimes, opacity modes/clamping/fallback')
