#!/usr/bin/env python3
"""Exercise the actual Vita glyph cache: pixel parity, hits, limits and lifetime."""
import importlib.util
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('font_raster', ROOT/'tests/font-raster.py')
raster = importlib.util.module_from_spec(spec)
spec.loader.exec_module(raster)
CHECKS = r'''
#include <stdexcept>
static void expect(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
static uint64_t pixels(SDL_Surface *s) {
    expect(s != nullptr, "render failed");
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int y=0; y<s->h; ++y) hash = vita_ttf_hash_bytes(hash,
        static_cast<unsigned char*>(s->pixels)+y*s->pitch, s->w*4);
    hash ^= uint64_t(s->w)<<32 | unsigned(s->h);
    SDL_FreeSurface(s); return hash;
}
static void limits() {
    expect(vita_ttf_glyphs.size() <= VitaTtfGlyphEntries, "entry limit");
    size_t bytes=0;
    for (const auto &glyph: vita_ttf_glyphs) bytes += glyph.coverage.size();
    expect(bytes == vita_ttf_glyph_bytes && bytes <= VitaTtfGlyphBytes, "byte limit/accounting");
}
int main(int argc, char **argv) try {
    if (argc!=2 || !vita_ttf_ensure_library()) return 2;
    // Use host FreeType faces: Vita's RWops size guard assumes 32-bit size_t.
    auto *font = new TTF_Font{};
    auto *other = new TTF_Font{};
    expect(FT_New_Face(vita_ttf_library,argv[1],0,&font->face)==0 &&
           FT_New_Face(vita_ttf_library,argv[1],0,&other->face)==0, "open failed");
    TTF_SetFontSize(font,24);TTF_SetFontSize(other,24);
    unsigned cases=0;
    for (int size: {14,24,32}) for (int outline: {0,1,2})
    for (int style: {0,1,2,3}) for (int kern: {0,1})
    for (const char *text: {"File 1", "00:16:49", "AVATAR jgy", "G", " ", "Items ", "Été 日本語"}) {
        TTF_SetFontSize(font,size);TTF_SetFontOutline(font,outline);
        TTF_SetFontStyle(font,style);TTF_SetFontKerning(font,kern);
        vita_ttf_forget_glyphs(font);
        SDL_Color color{37,149,233,173};
        const auto cold = pixels(vita_ttf_render(font,text,color));
        const unsigned loads = cache_test_loads;
        const auto warm = pixels(vita_ttf_render(font,text,color));
        expect(cold == warm, "cached raster changed pixels/geometry");
        expect(cache_test_loads == loads, "warm raster reloaded FreeType glyphs");
        int w0,h0,w1,h1;
        TTF_SizeUTF8(font,text,&w0,&h0);
        const unsigned measured=cache_test_loads;
        TTF_SizeUTF8(font,text,&w1,&h1);
        expect(w0==w1 && h0==h1 && cache_test_loads==measured, "measure cache mismatch");
        // Recolored text must use the current color, with the same cached mask.
        const auto colored = pixels(vita_ttf_render(font,text,SDL_Color{255,0,17,255}));
        vita_ttf_forget_glyphs(font);
        expect(colored == pixels(vita_ttf_render(font,text,SDL_Color{255,0,17,255})), "color cached incorrectly");
        limits();++cases;
    }
    pixels(vita_ttf_render(other,"distinct face",SDL_Color{255,255,255,255}));
    const auto cps=[](unsigned count) {
        std::vector<uint32_t> result;
        for(unsigned i=32;i<count+32;++i) result.push_back(i);
        return result;
    };
    VitaTtfLayout layout;
    vita_ttf_layout(font,cps(700),true,&layout);
    limits();
    // Oversized individual entries must not displace a bounded cache.
    VitaTtfCachedGlyph large={};large.coverage.resize(VitaTtfGlyphBytes+1);
    const auto entries=vita_ttf_glyphs.size(), bytes=vita_ttf_glyph_bytes;
    vita_ttf_remember_glyph(large);
    expect(entries==vita_ttf_glyphs.size() && bytes==vita_ttf_glyph_bytes, "oversized glyph retained");
    // A transient raster failure must be retried, rather than remembered as blank.
    vita_ttf_forget_glyphs(font);
    cache_test_fail_render = true;
    pixels(vita_ttf_render(font,"Q",SDL_Color{255,255,255,255}));
    const auto recovered=pixels(vita_ttf_render(font,"Q",SDL_Color{255,255,255,255}));
    vita_ttf_forget_glyphs(font);
    expect(recovered==pixels(vita_ttf_render(font,"Q",SDL_Color{255,255,255,255})), "raster failure poisoned cache");
    TTF_CloseFont(font);limits();
    for(const auto &glyph:vita_ttf_glyphs) expect(glyph.key.font!=font,"closed font retained");
    TTF_CloseFont(other);limits();
    expect(vita_ttf_glyphs.empty() && vita_ttf_glyph_bytes==0,"face cleanup");
    printf("PASS: %u pixel/measure/color parity cases, cache hits, eviction and close cleanup\n",cases);
} catch(const std::exception &e) {fprintf(stderr,"%s\n",e.what());return 1;}
'''
source = (ROOT/'src/vita_stubs.cpp').read_text()
backend = source[source.index('struct TTF_Font\n'):source.index('/* =================================================================\n * pixman')]
counting = r'''
static unsigned cache_test_loads=0;
static FT_Error cache_test_load(FT_Face face,FT_UInt glyph,FT_Int32 flags) {
    ++cache_test_loads;return FT_Load_Glyph(face,glyph,flags);
}
static bool cache_test_fail_render=false;
static FT_Error cache_test_render(FT_GlyphSlot slot,FT_Render_Mode mode) {
    if(cache_test_fail_render) {cache_test_fail_render=false;return 1;}
    return FT_Render_Glyph(slot,mode);
}
#define FT_Load_Glyph cache_test_load
#define FT_Render_Glyph cache_test_render
'''
flags = shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','sdl2','freetype2'],text=True))
with tempfile.TemporaryDirectory(prefix='hardrpg-font-cache-') as directory:
    work=Path(directory)
    (work/'cache.cpp').write_text(raster.PREAMBLE+counting+backend+CHECKS)
    subprocess.run(['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(work/'cache.cpp'),*flags,'-o',str(work/'cache')],check=True)
    subprocess.run([str(work/'cache'),str(ROOT/'assets/liberation.ttf')],check=True)
