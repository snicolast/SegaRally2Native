#include "ui_draw.h"

#include <cstdio>
#include <cstring>

namespace ui_draw {

std::vector<char> read_file(const std::string& path) {
    std::vector<char> data;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        fseek(f, 0, SEEK_END);
        data.resize(size_t(ftell(f)));
        fseek(f, 0, SEEK_SET);
        if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
        fclose(f);
    }
    return data;
}

TextRaster rasterise_text(HDC dc, HFONT font, const std::wstring& s, int pad) {
    TextRaster r;
    SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), int(s.size()), &sz);
    GetTextMetricsW(dc, &r.tm);
    r.text_w = sz.cx;
    r.text_h = sz.cy;
    r.w = sz.cx + 2 * pad;
    r.h = sz.cy + 2 * pad;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = r.w;
    bi.bmiHeader.biHeight = -r.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return r;
    HGDIOBJ old = SelectObject(dc, bmp);
    memset(bits, 0, size_t(r.w) * r.h * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, pad, pad, s.c_str(), int(s.size()));
    GdiFlush();
    r.a.resize(size_t(r.w) * r.h);
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < r.a.size(); i++) r.a[i] = p[i * 4 + 1];
    SelectObject(dc, old);
    DeleteObject(bmp);
    return r;
}

}  // namespace ui_draw
