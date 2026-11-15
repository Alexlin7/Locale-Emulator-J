// src/LEInstaller/Download.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <filesystem>
#include <functional>

namespace LeInstaller
{

// Called with the percentage (0-100) as data arrives; may be called from the download thread.
using ProgressCallback = std::function<void(int percent)>;

// Downloads url to file, following redirects and bypassing the cache.
// Returns S_OK, E_ABORT when cancel became true, or the failure HRESULT.
HRESULT DownloadFile(const wchar_t* url, const std::filesystem::path& file, const std::atomic<bool>& cancel,
                     const ProgressCallback& onProgress);

}  // namespace LeInstaller
