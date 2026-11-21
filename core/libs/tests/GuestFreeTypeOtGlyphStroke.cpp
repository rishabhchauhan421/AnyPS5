#include "FreeTypeFontFixture.hpp"
#include "prx/libSceFreeType/include/Api.hpp"
#include "prx/libSceFreeType/include/GuestObjects.hpp"
#include "prx/libSceFreeTypeOt/libSceFreeTypeOt.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace FreeType;

namespace {

void Check(bool value, int line) {
    if (value) return;
    std::fprintf(stderr, "FreeTypeOt glyph stroke check failed at line %d\n", line);
    std::abort();
}
#define Require(value) Check((value), __LINE__)

void Matches(GuestGlyphRec* glyph, FT_Glyph native) {
    Require(glyph && native && glyph->format == static_cast<std::uint32_t>(native->format));
    Require(glyph->advance.x == native->advance.x && glyph->advance.y == native->advance.y);
    const auto& a = reinterpret_cast<GuestOutlineGlyphRec*>(glyph)->outline;
    const auto& b = reinterpret_cast<FT_OutlineGlyph>(native)->outline;
    Require(a.n_points == b.n_points && a.n_contours == b.n_contours && a.flags == b.flags);
    Require(a.n_points > 0 && a.n_contours > 0);
    for (unsigned index = 0; index < a.n_points; ++index)
        Require(a.points[index].x == b.points[index].x && a.points[index].y == b.points[index].y && a.tags[index] == static_cast<unsigned char>(b.tags[index]));
    for (unsigned index = 0; index < a.n_contours; ++index) Require(a.contours[index] == static_cast<std::uint16_t>(b.contours[index]));
}

struct Fixture {
    std::vector<unsigned char> font = SquareGlyphFont('A', 700);
    GuestLibrary* library = nullptr;
    GuestFaceRec* face = nullptr;
    void* stroker = nullptr;
    FT_Library hostLibrary = nullptr;
    FT_Face host = nullptr;
    FT_Stroker hostStroker = nullptr;

    Fixture() {
        Require(FT_Init_FreeType_nid_postfix(&library) == 0);
        Require(FT_Init_FreeType(&hostLibrary) == 0);
        Require(FT_New_Memory_Face_nid_postfix(library, font.data(), static_cast<std::int64_t>(font.size()), 0, &face) == 0);
        Require(FT_New_Memory_Face(hostLibrary, font.data(), static_cast<FT_Long>(font.size()), 0, &host) == 0);
        Require(FT_Set_Pixel_Sizes_nid_postfix(face, 0, 48) == 0 && FT_Set_Pixel_Sizes(host, 0, 48) == 0);
        Require(FT_Load_Glyph_nid_postfix(face, 1, FT_LOAD_NO_HINTING) == 0 && FT_Load_Glyph(host, 1, FT_LOAD_NO_HINTING) == 0);
        Require(FT_Stroker_New_nid_postfix(library, &stroker) == 0 && stroker);
        Require(FT_Stroker_New(hostLibrary, &hostStroker) == 0);
        FT_Stroker_Set_nid_postfix(stroker, 96, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_MITER, 0x20000);
        FT_Stroker_Set(hostStroker, 96, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_MITER, 0x20000);
    }

    GuestGlyphRec* Glyph() {
        GuestGlyphRec* glyph = nullptr;
        Require(FT_Get_Glyph_nid_postfix(face->glyph, &glyph) == 0 && glyph);
        return glyph;
    }

    FT_Glyph HostGlyph() {
        FT_Glyph glyph = nullptr;
        Require(FT_Get_Glyph(host->glyph, &glyph) == 0 && glyph);
        return glyph;
    }

    ~Fixture() {
        FT_Stroker_Done_nid_postfix(stroker);
        FT_Stroker_Done(hostStroker);
        Require(FT_Done_Face_nid_postfix(face) == 0 && FT_Done_Face(host) == 0);
        Require(FT_Done_FreeType_nid_postfix(library) == 0 && FT_Done_FreeType(hostLibrary) == 0);
    }
};

void Stroke(bool destroy) {
    Fixture fixture;
    GuestGlyphRec* original = fixture.Glyph();
    GuestGlyphRec* glyph = original;
    FT_Glyph native = fixture.HostGlyph();
    FT_Glyph hostOriginal = native;
    Require(FT_Glyph_Stroke_nid_postfix(&glyph, fixture.stroker, destroy) == FT_Glyph_Stroke(&native, fixture.hostStroker, destroy));
    Require(glyph != original);
    Matches(glyph, native);
    if (!destroy) {
        Require(reinterpret_cast<GuestOutlineGlyphRec*>(original)->outline.n_points == reinterpret_cast<FT_OutlineGlyph>(hostOriginal)->outline.n_points);
        FT_Done_Glyph_nid_postfix(original);
        FT_Done_Glyph(hostOriginal);
    }
    GuestGlyphRec* copy = nullptr;
    Require(FT_Glyph_Copy_nid_postfix(glyph, &copy) == 0);
    Matches(copy, native);
    FT_Done_Glyph_nid_postfix(copy);
    FT_Done_Glyph_nid_postfix(glyph);
    FT_Done_Glyph(native);
}

void StrokeBorder(bool inside) {
    Fixture fixture;
    GuestGlyphRec* glyph = fixture.Glyph();
    FT_Glyph native = fixture.HostGlyph();
    Require(sceFreeTypeOtGlyphStrokeBorder_nid_no_patch(&glyph, fixture.stroker, inside, 1) == FT_Glyph_StrokeBorder(&native, fixture.hostStroker, inside, 1));
    Matches(glyph, native);
    FT_Done_Glyph_nid_postfix(glyph);
    FT_Done_Glyph(native);
}

void Rejections() {
    Fixture fixture;
    Require(FT_Glyph_Stroke_nid_postfix(nullptr, fixture.stroker, 0) == FT_Err_Invalid_Argument);
    Require(sceFreeTypeOtGlyphStrokeBorder_nid_no_patch(nullptr, fixture.stroker, 0, 0) == FT_Err_Invalid_Argument);
    GuestGlyphRec* empty = nullptr;
    Require(FT_Glyph_Stroke_nid_postfix(&empty, fixture.stroker, 0) == FT_Err_Invalid_Argument && empty == nullptr);

    GuestGlyphRec* bitmap = nullptr;
    Require(FreeTypeNewGlyph_nid_no_patch(fixture.library, FT_GLYPH_FORMAT_BITMAP, &bitmap) == 0 && bitmap);
    GuestGlyphRec* kept = bitmap;
    Require(FT_Glyph_Stroke_nid_postfix(&kept, fixture.stroker, 1) == FT_Err_Invalid_Argument && kept == bitmap);
    Require(sceFreeTypeOtGlyphStrokeBorder_nid_no_patch(&kept, fixture.stroker, 1, 1) == FT_Err_Invalid_Argument && kept == bitmap);
    FT_Done_Glyph_nid_postfix(bitmap);

    GuestGlyphRec* glyph = fixture.Glyph();
    GuestGlyphRec* original = glyph;
    FT_Glyph native = fixture.HostGlyph();
    FT_Glyph hostOriginal = native;
    Require(FT_Glyph_Stroke_nid_postfix(&glyph, nullptr, 0) == FT_Glyph_Stroke(&native, nullptr, 0));
    Require(glyph == nullptr && native == nullptr);
    glyph = original;
    native = hostOriginal;
    Require(FT_Glyph_Stroke_nid_postfix(&glyph, nullptr, 1) == FT_Glyph_Stroke(&native, nullptr, 1));
    Require(glyph == original && native == hostOriginal);
    FT_Done_Glyph_nid_postfix(glyph);
    FT_Done_Glyph(native);
}

}

int main() {
    Stroke(false);
    Stroke(true);
    StrokeBorder(false);
    StrokeBorder(true);
    Rejections();
    std::puts("FreeTypeOt glyph stroke tests passed");
    return 0;
}
