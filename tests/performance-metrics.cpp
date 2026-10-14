#include "src/display/performance-metrics.h"
#include <cassert>
#include <cmath>
using namespace PerformanceMetrics;
int main() {
    Sampler s;
    uint64_t time = 0;
    s.present(time);
    for (int i=0;i<120;++i) s.present(time += 16667);
    assert(s.reading().ready && std::abs(s.reading().fps-60)<0.01);
    assert(std::abs(s.reading().frameMs-16.667)<0.001);
    assert(std::abs(s.reading().peakMs-16.667)<0.001);
    s.present(time += 5000000);
    assert(s.reading().peakMs == 5000 && s.reading().fps < 10);
    const auto stallEnded = time;
    for(int i=0;i<60;++i) s.present(time+=16667);
    assert(s.reading().fps > 59 && s.reading().peakMs==5000);
    while (time < stallEnded + 4500000) s.present(time+=16667);
    assert(s.reading().peakMs==5000);
    while (time < stallEnded + 5600000) s.present(time+=16667);
    assert(s.reading().peakMs<17);
    s.reset(); s.present(time+=60000000);
    assert(!s.reading().ready);
    for(int i=0;i<30;++i) s.present(time+=33334);
    assert(std::abs(s.reading().fps-30)<0.01 && s.reading().peakMs<34);
    s.present(1);
    assert(!s.reading().ready);
    auto p=place(960,544,124,0,712,544,true);
    assert(p.border && p.x+p.width<=124 && p.y+p.height<=544);
    // A lower-left panel must remain there after bottom-up framebuffer
    // projection. Its first glyph row must appear above its last row.
    assert(framebufferY(544,p.y)==68);
    assert(framebufferY(544,p.y+p.height)==6);
    assert(framebufferY(544,p.y+6)>framebufferY(544,p.y+18));
    assert(framebufferY(544,0)==544 && framebufferY(544,544)==0);
    p=place(960,544,0,0,960,544,true);
    assert(!p.border && p.x==6 && p.y+p.height<=544);
    p=place(960,544,0,0,800,544,true);
    assert(p.border && p.x>=800);
    p=place(960,544,0,0,960,400,true);
    assert(p.border && p.y>=400);
    p=place(960,544,0,140,960,404,true);
    assert(p.border && p.y+p.height<=140);
    p=place(80,40,0,0,80,40,true);
    assert(p.x>=0 && p.y>=0 && p.x+p.width<=80 && p.y+p.height<=40);
    Reading r;
    r.ready=true; r.fps=60; r.frameMs=16.667; r.peakMs=5000;
    assert(panelText(r,true)=="FPS 60.0\nMS 16.7\nPEAK 5000");
    assert(panelText(r,false)=="FPS 60.0");
    for (double value : {16.7,99.94,99.95,999.9,5000.,9999.4,9999.5,60000.}) {
        r.peakMs=value;
        const auto text=panelText(r,true);
        size_t start=0;
        for (size_t i=0;i<=text.size();++i)
            if (i==text.size() || text[i]=='\n') {
                assert(i-start<=9); // Two-pixel glyphs fit the 118-pixel border panel.
                start=i+1;
            }
    }
}
