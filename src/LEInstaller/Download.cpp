// src/LEInstaller/Download.cpp
#include "Download.h"

#include <urlmon.h>

namespace LeInstaller
{

namespace
{

// Minimal IBindStatusCallback: reports progress, supports cancelling and asks for a fresh copy.
// It lives on the stack of DownloadFile, so reference counting is not used.
class BindCallback final : public IBindStatusCallback
{
public:
    BindCallback(const std::atomic<bool>& cancel, const ProgressCallback& onProgress)
        : m_cancel(cancel), m_onProgress(onProgress)
    {
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IBindStatusCallback)
        {
            *ppv = static_cast<IBindStatusCallback*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // IBindStatusCallback
    STDMETHODIMP OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    STDMETHODIMP GetPriority(LONG*) override { return E_NOTIMPL; }
    STDMETHODIMP OnLowResource(DWORD) override { return S_OK; }

    STDMETHODIMP OnProgress(ULONG progress, ULONG progressMax, ULONG, LPCWSTR) override
    {
        if (m_cancel.load())
            return E_ABORT;
        if (progressMax > 0 && m_onProgress)
            m_onProgress(static_cast<int>(static_cast<ULONGLONG>(progress) * 100 / progressMax));
        return S_OK;
    }

    STDMETHODIMP OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }

    STDMETHODIMP GetBindInfo(DWORD* bindf, BINDINFO* bindinfo) override
    {
        if (!bindf || !bindinfo)
            return E_POINTER;
        // Always fetch from the server: aka.ms points at the newest runtime.
        *bindf = BINDF_GETNEWESTVERSION | BINDF_NOWRITECACHE | BINDF_PRAGMA_NO_CACHE;
        return S_OK;
    }

    STDMETHODIMP OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    STDMETHODIMP OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }

private:
    const std::atomic<bool>& m_cancel;
    const ProgressCallback& m_onProgress;
};

}  // namespace

HRESULT DownloadFile(const wchar_t* url, const std::filesystem::path& file, const std::atomic<bool>& cancel,
                     const ProgressCallback& onProgress)
{
    BindCallback callback(cancel, onProgress);
    HRESULT hr = URLDownloadToFileW(nullptr, url, file.c_str(), 0, &callback);
    if (cancel.load())
        hr = E_ABORT;
    if (FAILED(hr))
    {
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }
    return hr;
}

}  // namespace LeInstaller
