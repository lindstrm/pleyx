#pragma once

#ifdef _WIN32
#include <windows.h>
#include <string>

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    bool load(const std::wstring& pngPath);
    bool loadFromResource(HINSTANCE hInstance, int resourceId);
    bool loadFromIconResource(HINSTANCE hInstance, int iconId);
    HICON getColorIcon() const { return hColorIcon; }
    HICON getGrayIcon() const { return hGrayIcon; }

private:
    bool loadFromMemory(const void* data, size_t size);
    bool createIconsFromBitmap(void* bitmap);  // Gdiplus::Bitmap*
    HICON createIconFromBitmap(HBITMAP hBitmap, int width, int height);
    HBITMAP createGrayscaleBitmap(HBITMAP hSource, int width, int height);

    HICON hColorIcon = nullptr;
    HICON hGrayIcon = nullptr;
};

#endif
