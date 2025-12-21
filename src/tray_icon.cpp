#include "tray_icon.h"

#ifdef _WIN32
// GDI+ requires specific include order
#include <objidl.h>
#include <gdiplus.h>
#include <iostream>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

static ULONG_PTR gdiplusToken = 0;
static int gdiplusRefCount = 0;

static void initGdiPlus() {
    if (gdiplusRefCount++ == 0) {
        Gdiplus::GdiplusStartupInput startupInput;
        Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr);
    }
}

static void shutdownGdiPlus() {
    if (--gdiplusRefCount == 0 && gdiplusToken) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        gdiplusToken = 0;
    }
}

TrayIcon::TrayIcon() {
    initGdiPlus();
}

TrayIcon::~TrayIcon() {
    if (hColorIcon) DestroyIcon(hColorIcon);
    if (hGrayIcon) DestroyIcon(hGrayIcon);
    shutdownGdiPlus();
}

bool TrayIcon::load(const std::wstring& pngPath) {
    // Load PNG using GDI+
    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromFile(pngPath.c_str());
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        std::cerr << "[TrayIcon] Failed to load PNG from file" << std::endl;
        delete bitmap;
        return false;
    }

    bool result = createIconsFromBitmap(bitmap);
    delete bitmap;
    return result;
}

bool TrayIcon::loadFromResource(HINSTANCE hInstance, int resourceId) {
    HRSRC hRes = FindResource(hInstance, MAKEINTRESOURCE(resourceId), RT_RCDATA);
    if (!hRes) {
        std::cerr << "[TrayIcon] Resource not found: " << resourceId << std::endl;
        return false;
    }

    HGLOBAL hData = LoadResource(hInstance, hRes);
    if (!hData) {
        std::cerr << "[TrayIcon] Failed to load resource" << std::endl;
        return false;
    }

    void* pData = LockResource(hData);
    DWORD size = SizeofResource(hInstance, hRes);

    if (!pData || size == 0) {
        std::cerr << "[TrayIcon] Failed to lock resource" << std::endl;
        return false;
    }

    return loadFromMemory(pData, size);
}

bool TrayIcon::loadFromIconResource(HINSTANCE hInstance, int iconId) {
    int iconSize = GetSystemMetrics(SM_CXSMICON);  // Usually 16

    // Load the icon at the desired size
    HICON hLoadedIcon = (HICON)LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(iconId),
        IMAGE_ICON,
        iconSize, iconSize,
        LR_DEFAULTCOLOR
    );

    if (!hLoadedIcon) {
        std::cerr << "[TrayIcon] Failed to load icon resource: " << iconId << std::endl;
        return false;
    }

    // Get icon info to extract the bitmap
    ICONINFO iconInfo;
    if (!GetIconInfo(hLoadedIcon, &iconInfo)) {
        DestroyIcon(hLoadedIcon);
        return false;
    }

    // The color icon is ready to use
    hColorIcon = hLoadedIcon;

    // Create grayscale version from the color bitmap
    HBITMAP hGrayBitmap = createGrayscaleBitmap(iconInfo.hbmColor, iconSize, iconSize);
    hGrayIcon = createIconFromBitmap(hGrayBitmap, iconSize, iconSize);

    // Cleanup
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);
    DeleteObject(hGrayBitmap);

    std::cout << "[TrayIcon] Loaded icons from resource (" << iconSize << "x" << iconSize << ")" << std::endl;
    return hColorIcon != nullptr && hGrayIcon != nullptr;
}

bool TrayIcon::loadFromMemory(const void* data, size_t size) {
    // Create IStream from memory
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return false;

    void* pMem = GlobalLock(hMem);
    memcpy(pMem, data, size);
    GlobalUnlock(hMem);

    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
        GlobalFree(hMem);
        return false;
    }

    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();

    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        std::cerr << "[TrayIcon] Failed to load PNG from memory" << std::endl;
        delete bitmap;
        return false;
    }

    bool result = createIconsFromBitmap(bitmap);
    delete bitmap;
    return result;
}

bool TrayIcon::createIconsFromBitmap(void* bitmapPtr) {
    Gdiplus::Bitmap* bitmap = static_cast<Gdiplus::Bitmap*>(bitmapPtr);
    int width = bitmap->GetWidth();
    int height = bitmap->GetHeight();

    // For system tray, we want 16x16 or 32x32
    int iconSize = GetSystemMetrics(SM_CXSMICON);  // Usually 16

    // Create resized bitmap for tray
    Gdiplus::Bitmap* resized = new Gdiplus::Bitmap(iconSize, iconSize, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(resized);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(bitmap, 0, 0, iconSize, iconSize);

    // Get HBITMAP from color bitmap
    HBITMAP hColorBitmap = nullptr;
    resized->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hColorBitmap);

    // Create color icon
    hColorIcon = createIconFromBitmap(hColorBitmap, iconSize, iconSize);

    // Create grayscale bitmap
    HBITMAP hGrayBitmap = createGrayscaleBitmap(hColorBitmap, iconSize, iconSize);
    hGrayIcon = createIconFromBitmap(hGrayBitmap, iconSize, iconSize);

    // Cleanup
    DeleteObject(hColorBitmap);
    DeleteObject(hGrayBitmap);
    delete resized;

    std::cout << "[TrayIcon] Loaded icons (" << iconSize << "x" << iconSize << ")" << std::endl;
    return hColorIcon != nullptr && hGrayIcon != nullptr;
}

HICON TrayIcon::createIconFromBitmap(HBITMAP hBitmap, int width, int height) {
    // Create mask bitmap (all black = fully opaque when using 32-bit color)
    HBITMAP hMask = CreateBitmap(width, height, 1, 1, nullptr);

    // Get bitmap info
    BITMAP bm;
    GetObject(hBitmap, sizeof(bm), &bm);

    ICONINFO iconInfo = {0};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = hMask;
    iconInfo.hbmColor = hBitmap;

    HICON hIcon = CreateIconIndirect(&iconInfo);

    DeleteObject(hMask);
    return hIcon;
}

HBITMAP TrayIcon::createGrayscaleBitmap(HBITMAP hSource, int width, int height) {
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcSrc = CreateCompatibleDC(hdcScreen);
    HDC hdcDst = CreateCompatibleDC(hdcScreen);

    // Create 32-bit destination bitmap
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* srcBits = nullptr;
    void* dstBits = nullptr;

    HBITMAP hDstBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &dstBits, nullptr, 0);

    // Get source bits
    SelectObject(hdcSrc, hSource);
    SelectObject(hdcDst, hDstBitmap);

    // Copy and convert to grayscale
    BITMAPINFO srcBmi = bmi;
    std::vector<BYTE> srcBuffer(width * height * 4);
    GetDIBits(hdcSrc, hSource, 0, height, srcBuffer.data(), &srcBmi, DIB_RGB_COLORS);

    BYTE* src = srcBuffer.data();
    BYTE* dst = static_cast<BYTE*>(dstBits);

    for (int i = 0; i < width * height; i++) {
        BYTE b = src[i * 4 + 0];
        BYTE g = src[i * 4 + 1];
        BYTE r = src[i * 4 + 2];
        BYTE a = src[i * 4 + 3];

        // Convert to grayscale using luminance formula, then darken
        BYTE gray = static_cast<BYTE>((r * 0.299 + g * 0.587 + b * 0.114) * 0.6);

        dst[i * 4 + 0] = gray;  // B
        dst[i * 4 + 1] = gray;  // G
        dst[i * 4 + 2] = gray;  // R
        dst[i * 4 + 3] = a;     // A (preserve alpha)
    }

    DeleteDC(hdcSrc);
    DeleteDC(hdcDst);
    ReleaseDC(nullptr, hdcScreen);

    return hDstBitmap;
}

#endif
