// src/LEInstaller/main.cpp
// LEInstaller.exe: makes sure the .NET 10 Desktop Runtime (x86 + x64) is installed,
// then opens LEGUI's installer window (LEGUI.exe --installer, same folder), which registers the
// context menu. Native and statically linked so that it runs before .NET is installed.
// Design: docs/superpowers/specs/2026-11-15-leinstaller-design.md

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <atomic>
#include <filesystem>
#include <format>
#include <string>
#include <thread>
#include <vector>

#include "Download.h"
#include "InstallSteps.h"
#include "RuntimeCheck.h"
#include "Signature.h"
#include "Strings.h"

namespace fs = std::filesystem;
using namespace LeInstaller;

namespace
{

constexpr wchar_t kDownloadPage[] = L"https://dotnet.microsoft.com/download/dotnet/10.0";

constexpr int kButtonAutoInstall = 100;
constexpr int kButtonOpenPage = 101;

const Strings* g_text = nullptr;

const wchar_t* ArchName(Arch arch) { return arch == Arch::X86 ? L"x86" : L"x64"; }

const wchar_t* RuntimeUrl(Arch arch)
{
    // Microsoft's permanent links to the newest 10.0.x Desktop Runtime installer.
    return arch == Arch::X86 ? L"https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-x86.exe"
                             : L"https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-x64.exe";
}

template <class... Args>
std::wstring Format(const wchar_t* pattern, Args&&... args)
{
    return std::vformat(std::wstring_view(pattern), std::make_wformat_args(args...));
}

std::wstring HexCode(unsigned long code) { return std::format(L"0x{:08X}", code); }

void OpenDownloadPage() { ShellExecuteW(nullptr, L"open", kDownloadPage, nullptr, nullptr, SW_SHOWNORMAL); }

// Error with an optional "Open the download page" button.
void ShowError(const std::wstring& message, bool offerDownloadPage)
{
    TASKDIALOG_BUTTON buttons[] = {{kButtonOpenPage, g_text->buttonOpenPageShort}};

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    config.pszWindowTitle = g_text->title;
    config.pszMainIcon = TD_ERROR_ICON;
    config.pszContent = message.c_str();
    if (offerDownloadPage)
    {
        config.pButtons = buttons;
        config.cButtons = ARRAYSIZE(buttons);
    }

    int clicked = 0;
    TaskDialogIndirect(&config, &clicked, nullptr, nullptr);
    if (clicked == kButtonOpenPage)
        OpenDownloadPage();
}

enum class Choice
{
    AutoInstall,
    OpenPage,
    Cancel
};

HRESULT CALLBACK MissingDialogCallback(HWND hwnd, UINT msg, WPARAM, LPARAM, LONG_PTR)
{
    if (msg == TDN_CREATED)  // UAC shield on the button that installs
        SendMessageW(hwnd, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, kButtonAutoInstall, TRUE);
    return S_OK;
}

Choice AskWhatToDo(const RuntimeStatus& status)
{
    std::wstring content = g_text->missingContent;
    content += L"\n";
    if (!status.hasX86)
        content += std::wstring(L"\n• ") + g_text->itemX86;
    if (status.NeedsX64())
        content += std::wstring(L"\n• ") + g_text->itemX64;

    TASKDIALOG_BUTTON buttons[] = {
        {kButtonAutoInstall, g_text->buttonAutoInstall},
        {kButtonOpenPage, g_text->buttonOpenPage},
    };

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = g_text->title;
    config.pszMainIcon = TD_INFORMATION_ICON;
    config.pszMainInstruction = g_text->missingMain;
    config.pszContent = content.c_str();
    config.pButtons = buttons;
    config.cButtons = ARRAYSIZE(buttons);
    config.nDefaultButton = kButtonAutoInstall;
    config.pfCallback = MissingDialogCallback;

    int clicked = 0;
    if (FAILED(TaskDialogIndirect(&config, &clicked, nullptr, nullptr)))
        return Choice::Cancel;
    if (clicked == kButtonAutoInstall)
        return Choice::AutoInstall;
    if (clicked == kButtonOpenPage)
        return Choice::OpenPage;
    return Choice::Cancel;
}

// ---- Download with a progress dialog ----

struct DownloadJob
{
    const wchar_t* url = nullptr;
    fs::path file;
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::atomic<int> percent{0};
    HRESULT result = E_FAIL;
    std::thread worker;
};

HRESULT CALLBACK DownloadDialogCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM, LONG_PTR refData)
{
    auto* job = reinterpret_cast<DownloadJob*>(refData);
    switch (msg)
    {
    case TDN_CREATED:
        SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
        job->worker = std::thread([job] {
            // URLDownloadToFile needs COM on this thread.
            const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            job->result = DownloadFile(job->url, job->file, job->cancel,
                                       [job](int p) { job->percent.store(p); });
            if (SUCCEEDED(co))
                CoUninitialize();
            job->done.store(true);
        });
        break;

    case TDN_TIMER:  // every ~200 ms: show progress, close when the download ends
        SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_POS, job->percent.load(), 0);
        if (job->done.load())
            PostMessageW(hwnd, TDM_CLICK_BUTTON, IDCANCEL, 0);
        break;

    case TDN_BUTTON_CLICKED:
        if (wParam == IDCANCEL && !job->done.load())
            job->cancel.store(true);
        break;
    }
    return S_OK;
}

HRESULT DownloadWithProgress(Arch arch, const fs::path& file)
{
    DownloadJob job;
    job.url = RuntimeUrl(arch);
    job.file = file;

    const std::wstring mainText = Format(g_text->downloadingMain, ArchName(arch));

    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = g_text->title;
    config.pszMainInstruction = mainText.c_str();
    config.pszContent = g_text->downloadingContent;
    config.pfCallback = DownloadDialogCallback;
    config.lpCallbackData = reinterpret_cast<LONG_PTR>(&job);

    TaskDialogIndirect(&config, nullptr, nullptr, nullptr);

    job.cancel.store(!job.done.load());  // closed early: stop the download
    if (job.worker.joinable())
        job.worker.join();
    return job.cancel.load() ? E_ABORT : job.result;
}

// ---- Install one runtime ----

StepResult DownloadAndInstall(Arch arch)
{
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    const fs::path dir = fs::path(temp) / L"LocaleEmulator-Setup";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / std::format(L"windowsdesktop-runtime-win-{}.exe", ArchName(arch));

    const HRESULT hr = DownloadWithProgress(arch, file);
    if (hr == E_ABORT)
        return StepResult::Cancelled;
    if (FAILED(hr))
    {
        ShowError(Format(g_text->errorDownload, HexCode(static_cast<unsigned long>(hr))), true);
        return StepResult::Failed;
    }

    if (!IsSignedByMicrosoft(file))
    {
        fs::remove(file, ec);
        ShowError(g_text->errorSignature, true);
        return StepResult::Failed;
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = file.c_str();
    sei.lpParameters = L"/install /passive /norestart";
    sei.nShow = SW_SHOWNORMAL;

    StepResult result = StepResult::Installed;
    if (!ShellExecuteExW(&sei))
    {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED)  // UAC prompt declined
            result = StepResult::Cancelled;
        else
        {
            ShowError(Format(g_text->errorInstall, HexCode(error)), true);
            result = StepResult::Failed;
        }
    }
    else
    {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);

        if (exitCode == ERROR_INSTALL_USEREXIT)  // 1602: cancelled in the installer
            result = StepResult::Cancelled;
        else if (exitCode != ERROR_SUCCESS && exitCode != ERROR_SUCCESS_REBOOT_REQUIRED &&
                 exitCode != ERROR_SUCCESS_REBOOT_INITIATED)
        {
            ShowError(Format(g_text->errorInstall, std::to_wstring(exitCode)), true);
            result = StepResult::Failed;
        }
    }

    fs::remove(file, ec);
    return result;
}

bool LaunchLegui(const fs::path& legui)
{
    const std::wstring dir = legui.parent_path().native();

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    // The user already chose to run LEInstaller from this folder: no second
    // "Open File - Security Warning" for LEGUI.exe when the zip was not unblocked.
    sei.fMask = SEE_MASK_NOZONECHECKS;
    sei.lpVerb = L"open";
    sei.lpFile = legui.c_str();
    sei.lpParameters = L"--installer";
    sei.lpDirectory = dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei))
        return true;

    ShowError(Format(g_text->errorLaunch, HexCode(GetLastError())), false);
    return false;
}

fs::path ExeDirectory()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;)
    {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len < buffer.size())
            return fs::path(std::wstring(buffer.data(), len)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    g_text = &GetStrings(PickLanguage(GetUserDefaultUILanguage()));

    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int exitCode = 0;
    // Single product folder (docs/superpowers/specs/2026-11-16-flat-product-layout.md).
    const fs::path legui = ExeDirectory() / L"LEGUI.exe";

    if (!Is64BitWindows())
    {
        // LEGUI.exe has an x64 apphost.
        ShowError(g_text->error32BitWindows, false);
        exitCode = 1;
    }
    else if (!fs::exists(legui))
    {
        // For example started from inside the zip without extracting it.
        ShowError(g_text->errorFilesMissing, false);
        exitCode = 1;
    }
    else
    {
        for (;;)
        {
            const RuntimeStatus status = CheckRuntimes();
            if (status.IsComplete())
            {
                exitCode = LaunchLegui(legui) ? 0 : 1;
                break;
            }

            const Choice choice = AskWhatToDo(status);
            if (choice == Choice::Cancel)
                break;
            if (choice == Choice::OpenPage)
            {
                OpenDownloadPage();
                break;
            }

            if (InstallMissingRuntimes(status, DownloadAndInstall) == StepResult::Failed)
            {
                exitCode = 1;
                break;
            }
            // Check again: shows the dialog again for anything still missing (e.g. cancelled).
        }
    }

    if (SUCCEEDED(co))
        CoUninitialize();
    return exitCode;
}
