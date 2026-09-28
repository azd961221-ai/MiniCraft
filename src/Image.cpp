#include "Image.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <cstdio>

namespace {

template <class T> struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
};

IWICImagingFactory* factory() {
    static IWICImagingFactory* f = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IWICImagingFactory* fac = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
        return fac;
    }();
    return f;
}

} // namespace

std::string exeDirectory() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring w(buf, n);
    w = w.substr(0, w.find_last_of(L"\\/") + 1);
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

bool loadImage(const std::string& path, Image& out) {
    IWICImagingFactory* fac = factory();
    if (!fac) return false;

    int len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), len);

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(fac->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, &decoder))) {
        std::fprintf(stderr, "Cannot open image: %s\n", path.c_str());
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    ComPtr<IWICBitmapSource> converted;
    if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame.p, &converted))) return false;

    UINT w = 0, h = 0;
    converted->GetSize(&w, &h);
    out.width = (int)w;
    out.height = (int)h;
    out.rgba.resize(size_t(w) * h * 4);
    return SUCCEEDED(converted->CopyPixels(nullptr, w * 4, (UINT)out.rgba.size(), out.rgba.data()));
}
