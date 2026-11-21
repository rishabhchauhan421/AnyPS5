#include "prx/libSceFreeType/include/Api.hpp"
#include "prx/libSceFreeType/include/GuestObjects.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/GuestArena.hpp"

#include FT_GLYPH_H
#include FT_STROKER_H

#include <algorithm>
#include <bit>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

using namespace FreeType;

namespace {

template <typename TTarget, typename TSource> TTarget Count(TSource value, const char* function) {
    if (!std::in_range<TTarget>(value)) NotImplemented_nid_no_patch(function);
    return static_cast<TTarget>(value);
}

struct Glyph {
    GuestOutlineGlyphRec outline{};
    GuestBitmapGlyphRec bitmap{};
    GuestLibrary* library = nullptr;
    bool isBitmap = false;
    std::unique_ptr<FT_GlyphRec, decltype(&FT_Done_Glyph)> host{nullptr, &FT_Done_Glyph};
    std::vector<GuestVector> points;

    GuestGlyphRec* Root() { return isBitmap ? &bitmap.root : &outline.root; }

    void Publish(const GuestOutline& source, const char* function) {
        *Root() = {library, host->clazz, static_cast<std::uint32_t>(host->format), {host->advance.x, host->advance.y}};
        if (isBitmap) {
            const auto* native = reinterpret_cast<FT_BitmapGlyph>(host.get());
            bitmap.left = native->left;
            bitmap.top = native->top;
            const auto& image = native->bitmap;
            bitmap.bitmap = {image.rows, image.width, image.pitch, image.buffer, image.num_grays, image.pixel_mode, image.palette_mode, image.palette};
        } else {
            const auto& native = reinterpret_cast<FT_OutlineGlyph>(host.get())->outline;
            std::copy_n(source.points, points.size(), points.begin());
            outline.outline = {Count<std::uint16_t>(native.n_contours, function), Count<std::uint16_t>(native.n_points, function),
                               points.empty() ? nullptr : points.data(), reinterpret_cast<unsigned char*>(native.tags),
                               reinterpret_cast<std::uint16_t*>(native.contours), native.flags};
        }
    }
};

std::mutex glyphMutex;
std::unordered_map<GuestGlyphRec*, std::unique_ptr<Glyph>> glyphs;

FT_Vector Advance(const GuestVector& advance) {
    constexpr std::int64_t limit = 0x200000;
    return {static_cast<FT_Pos>(std::clamp(advance.x, -limit, limit)), static_cast<FT_Pos>(std::clamp(advance.y, -limit, limit))};
}

bool ValidAdvance(const GuestVector& advance) {
    return advance.x > -0x200000 && advance.x < 0x200000 && advance.y > -0x200000 && advance.y < 0x200000;
}

FT_Bitmap Bitmap(const GuestBitmap& bitmap) {
    return {bitmap.rows, bitmap.width, bitmap.pitch, bitmap.buffer, bitmap.num_grays, bitmap.pixel_mode, bitmap.palette_mode, bitmap.palette};
}

struct Outline {
    FT_Outline native{};
    std::vector<FT_Vector> points;

    Outline(const GuestOutline& outline, const char* function) {
        native = {Count<decltype(native.n_contours)>(outline.n_contours, function), Count<decltype(native.n_points)>(outline.n_points, function), nullptr,
                  reinterpret_cast<decltype(native.tags)>(outline.tags), reinterpret_cast<decltype(native.contours)>(outline.contours), outline.flags};
        if (outline.n_contours > outline.n_points) return;
        points.resize(outline.n_points);
        native.points = points.empty() ? nullptr : points.data();
    }
};

Glyph& Find(GuestGlyphRec* glyph, const char* function, bool requireLibrary = true) {
    const auto found = glyphs.find(glyph);
    if (found == glyphs.end()) NotImplemented_nid_no_patch(function);
    auto& owner = *found->second;
    if ((requireLibrary && glyph->library != owner.library) || glyph->clazz != owner.host->clazz) NotImplemented_nid_no_patch(function);
    return owner;
}

Glyph& Register(GuestLibrary* library, bool isBitmap, std::size_t pointCount) {
    auto owner = std::make_unique<Glyph>();
    owner->library = library;
    owner->isBitmap = isBitmap;
    owner->points.resize(pointCount);
    auto* address = owner->Root();
    return *glyphs.emplace(address, std::move(owner)).first->second;
}

}

int FreeType::FreeTypeNewGlyph_nid_no_patch(GuestLibrary* library, std::uint32_t format, GuestGlyphRec** target) {
    if (!library || !library->host || !target) return FT_Err_Invalid_Argument;
    if (!InGlyphFormatRange(format)) return FT_Err_Invalid_Glyph_Format;
    const auto nativeFormat = static_cast<FT_Glyph_Format>(format);
    if (nativeFormat != FT_GLYPH_FORMAT_BITMAP && nativeFormat != FT_GLYPH_FORMAT_OUTLINE) {
        FT_Glyph probe = nullptr;
        const FT_Error error = FT_New_Glyph(library->host, nativeFormat, &probe);
        if (error == FT_Err_Invalid_Glyph_Format || error == FT_Err_Invalid_Argument) return error;
        if (probe) FT_Done_Glyph(probe);
        NotImplemented_nid_no_patch(__func__);
    }
    GuestArena::HostWrite write(target, sizeof(*target));
    if (!write.Open()) return FT_Err_Invalid_Argument;
    *target = nullptr;
    std::lock_guard lock(glyphMutex);
    auto& owner = Register(library, nativeFormat == FT_GLYPH_FORMAT_BITMAP, 0);
    auto* address = owner.Root();
    FT_Glyph result = nullptr;
    const FT_Error error = FT_New_Glyph(library->host, nativeFormat, &result);
    if (error) {
        glyphs.erase(address);
        return error;
    }
    owner.host.reset(result);
    owner.Publish({}, __func__);
    *target = address;
    return 0;
}

int FreeType::FreeTypeStrokeGlyph_nid_no_patch(GuestGlyphRec** target, void* stroker, std::int32_t border, bool destroy) {
    constexpr const char* function = "FreeTypeStrokeGlyph_nid_no_patch";
    GuestGlyphRec* glyph = nullptr;
    if (!target || !GuestCopyIn_nid_no_patch(&glyph, target, sizeof(glyph))) return FT_Err_Invalid_Argument;
    if (!glyph) return FT_Err_Invalid_Argument;
    {
        std::lock_guard lock(glyphMutex);
        if (Find(glyph, function, false).isBitmap) return FT_Err_Invalid_Argument;
    }
    GuestGlyphRec* copy = nullptr;
    FT_Error error = FT_Glyph_Copy_nid_postfix(glyph, &copy);
    if (error) return error;
    auto& image = reinterpret_cast<GuestOutlineGlyphRec*>(copy)->outline;
    error = FT_Stroker_ParseOutline_nid_postfix(stroker, &image, 0);
    std::uint32_t points = 0;
    std::uint32_t contours = 0;
    if (!error) {
        if (border < 0) error = FT_Stroker_GetCounts_nid_postfix(stroker, &points, &contours);
        else FT_Stroker_GetBorderCounts_nid_postfix(stroker, border, &points, &contours);
    }
    if (!error) {
        std::lock_guard lock(glyphMutex);
        auto& owner = Find(copy, function);
        auto& native = reinterpret_cast<FT_OutlineGlyph>(owner.host.get())->outline;
        FT_Outline_Done(owner.host->library, &native);
        error = FT_Outline_New(owner.host->library, Count<FT_UInt>(points, function), Count<FT_Int>(contours, function), &native);
        if (error) native = {};
        owner.points.assign(error ? 0 : points, GuestVector{});
        image = {0, 0, owner.points.empty() ? nullptr : owner.points.data(), reinterpret_cast<unsigned char*>(native.tags),
                 reinterpret_cast<std::uint16_t*>(native.contours), native.flags};
    }
    if (!error) {
        if (border < 0) StrokerExport_nid_no_patch(stroker, &image);
        else FT_Stroker_ExportBorder_nid_postfix(stroker, border, &image);
        std::lock_guard lock(glyphMutex);
        auto& owner = Find(copy, function);
        auto& native = reinterpret_cast<FT_OutlineGlyph>(owner.host.get())->outline;
        native.n_points = Count<decltype(native.n_points)>(image.n_points, function);
        native.n_contours = Count<decltype(native.n_contours)>(image.n_contours, function);
        const GuestVector advance = copy->advance;
        owner.Publish(image, function);
        copy->advance = advance;
    }
    GuestGlyphRec* result = copy;
    if (error) {
        FT_Done_Glyph_nid_postfix(copy);
        if (destroy) return error;
        result = nullptr;
    } else if (destroy) {
        FT_Done_Glyph_nid_postfix(glyph);
    }
    if (!GuestCopyOut_nid_no_patch(target, &result, sizeof(result))) throw std::invalid_argument(std::string(function) + ": unwritable glyph pointer");
    return error;
}

extern "C" {

int APS5_VABI FT_Get_Glyph_nid_postfix(GuestGlyphSlotRec* slot, GuestGlyphRec** target) {
    if (!slot) return FT_Err_Invalid_Slot_Handle;
    if (!target) return FT_Err_Invalid_Argument;
    auto* library = static_cast<GuestLibrary*>(slot->library);
    FT_GlyphSlotRec native{};
    native.library = library ? library->host : nullptr;
    native.format = static_cast<FT_Glyph_Format>(slot->format);
    native.advance = Advance(slot->advance);
    if (!library || !ValidAdvance(slot->advance)) {
        FT_Glyph result = reinterpret_cast<FT_Glyph>(&native);
        const FT_Error error = FT_Get_Glyph(&native, &result);
        if (!result) *target = nullptr;
        return error;
    }
    if (native.format != FT_GLYPH_FORMAT_BITMAP && native.format != FT_GLYPH_FORMAT_OUTLINE) {
        FT_Glyph probe = nullptr;
        const FT_Error error = FT_New_Glyph(native.library, native.format, &probe);
        if (error) return error;
        FT_Done_Glyph(probe);
        NotImplemented_nid_no_patch(__func__);
    }
    Outline outline(native.format == FT_GLYPH_FORMAT_OUTLINE ? slot->outline : GuestOutline{}, __func__);
    native.outline = outline.native;
    native.bitmap = Bitmap(slot->bitmap);
    native.bitmap_left = slot->bitmap_left;
    native.bitmap_top = slot->bitmap_top;
    if (native.format == FT_GLYPH_FORMAT_BITMAP) {
        const auto* face = FaceOf(slot);
        if (!face || slot != &face->slot || slot->library != face->library) NotImplemented_nid_no_patch(__func__);
        const auto host = face->host->glyph;
        if (slot->bitmap.buffer != host->bitmap.buffer) NotImplemented_nid_no_patch(__func__);
        native.internal = host->internal;
    }
    std::lock_guard lock(glyphMutex);
    auto& owner = Register(library, native.format == FT_GLYPH_FORMAT_BITMAP, native.outline.n_points);
    auto* address = owner.Root();
    FT_Glyph result = reinterpret_cast<FT_Glyph>(&native);
    const FT_Error error = FT_Get_Glyph(&native, &result);
    if (error) {
        glyphs.erase(address);
        if (!result) *target = nullptr;
        return error;
    }
    owner.host.reset(result);
    owner.Publish(slot->outline, __func__);
    *target = address;
    return 0;
}

int APS5_VABI FT_Glyph_Copy_nid_postfix(GuestGlyphRec* source, GuestGlyphRec** target) {
    if (!target || !source || !source->clazz) return FT_Err_Invalid_Argument;
    std::lock_guard lock(glyphMutex);
    auto& original = Find(source, __func__);
    *target = nullptr;
    if (!source->clazz) return FT_Err_Invalid_Argument;
    if (source->library != original.library || source->clazz != original.host->clazz) NotImplemented_nid_no_patch(__func__);
    const GuestVector advance = source->advance;
    FT_OutlineGlyphRec nativeOutline{};
    FT_BitmapGlyphRec nativeBitmap{};
    FT_Glyph native = original.isBitmap ? &nativeBitmap.root : &nativeOutline.root;
    *native = *original.host;
    native->format = static_cast<FT_Glyph_Format>(source->format);
    Outline outline(original.isBitmap ? GuestOutline{} : original.outline.outline, __func__);
    nativeOutline.outline = outline.native;
    nativeBitmap.bitmap = Bitmap(original.bitmap.bitmap);
    nativeBitmap.left = original.bitmap.left;
    nativeBitmap.top = original.bitmap.top;
    auto& copy = Register(original.library, original.isBitmap, outline.native.n_points);
    auto* address = copy.Root();
    FT_Glyph result = nullptr;
    const FT_Error error = FT_Glyph_Copy(native, &result);
    if (error) {
        glyphs.erase(address);
        return error;
    }
    copy.host.reset(result);
    copy.Publish(original.outline.outline, __func__);
    address->advance = advance;
    *target = address;
    return 0;
}

void APS5_VABI FT_Glyph_Get_CBox_nid_postfix(GuestGlyphRec* glyph, std::uint32_t mode, GuestBBox* box) {
    if (!box) return;
    *box = {};
    if (!glyph || !glyph->clazz) return;
    std::lock_guard lock(glyphMutex);
    auto& owner = Find(glyph, __func__, false);
    if (owner.isBitmap) {
        const auto& image = owner.bitmap;
        const auto fits = [](std::int32_t bearing) {
            return bearing >= std::numeric_limits<std::int32_t>::min() / 64 && bearing <= std::numeric_limits<std::int32_t>::max() / 64;
        };
        if (!fits(image.left) || !fits(image.top)) NotImplemented_nid_no_patch(__func__);
        box->xMin = static_cast<std::int64_t>(image.left) * 64;
        box->xMax = box->xMin + static_cast<std::int64_t>(image.bitmap.width * std::uint32_t{64});
        box->yMax = static_cast<std::int64_t>(image.top) * 64;
        box->yMin = box->yMax - static_cast<std::int64_t>(image.bitmap.rows * std::uint32_t{64});
    } else {
        FT_Outline_Get_CBox_nid_postfix(&owner.outline.outline, box);
    }
    if (mode == FT_GLYPH_BBOX_GRIDFIT || mode == FT_GLYPH_BBOX_PIXELS) {
        const auto ceiling = [](std::int64_t value) {
            return std::bit_cast<std::int64_t>((std::bit_cast<std::uint64_t>(value) + 63) & ~std::uint64_t{63});
        };
        box->xMin &= ~std::int64_t{63};
        box->yMin &= ~std::int64_t{63};
        box->xMax = ceiling(box->xMax);
        box->yMax = ceiling(box->yMax);
    }
    if (mode == FT_GLYPH_BBOX_TRUNCATE || mode == FT_GLYPH_BBOX_PIXELS) {
        box->xMin >>= 6;
        box->yMin >>= 6;
        box->xMax >>= 6;
        box->yMax >>= 6;
    }
}

void APS5_VABI FT_Done_Glyph_nid_postfix(GuestGlyphRec* glyph) {
    if (!glyph) return;
    std::lock_guard lock(glyphMutex);
    Find(glyph, __func__);
    glyphs.erase(glyph);
}

}
