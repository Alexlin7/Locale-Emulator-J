// src/LEInstaller/Strings.cpp
#include "Strings.h"

namespace LeInstaller
{

namespace
{

constexpr Strings kEnglish{
    L"Locale Emulator Setup",
    L"Install the .NET 10 Desktop Runtime",
    L"Locale Emulator needs the following parts of the Microsoft .NET 10 Desktop Runtime, which are not installed on this computer:",
    L"x86 (32-bit): used by LEProc to start applications",
    L"x64 (64-bit): used by LEGUI, the settings window",
    L"Download and install automatically\nDownloads the official installer from Microsoft and runs it. Requires administrator permission.",
    L"Open the download page\nDownload and install it yourself, then run LEInstaller again.",
    L"Open the download page",
    L"Downloading the .NET 10 Desktop Runtime ({})",
    L"Downloading from Microsoft.",
    L"The download failed ({}).\nCheck your Internet connection, or open the download page and install it yourself.",
    L"The downloaded file is not signed by Microsoft. It was deleted and not run.\nOpen the download page and install it yourself.",
    L"The installer ended with error code {}.\nOpen the download page and install it yourself.",
    L"This version of Locale Emulator requires 64-bit Windows.",
    L"LEGUI.exe was not found next to LEInstaller.exe.\nExtract all files of the zip into one folder, then run LEInstaller.exe again.",
    L"LEGUI could not be started ({}).",
};

constexpr Strings kTraditionalChinese{
    L"Locale Emulator 安裝程式",
    L"需要安裝 .NET 10 桌面執行階段",
    L"Locale Emulator 需要下列 Microsoft .NET 10 桌面執行階段（Desktop Runtime），這台電腦還沒有安裝：",
    L"x86（32 位元）：LEProc 啟動程式時使用",
    L"x64（64 位元）：LEGUI 設定視窗使用",
    L"自動下載並安裝\n從微軟下載官方安裝程式並執行，需要系統管理員權限。",
    L"開啟下載頁\n自行下載安裝後，再執行一次 LEInstaller。",
    L"開啟下載頁",
    L"正在下載 .NET 10 桌面執行階段（{}）",
    L"從微軟官方網站下載。",
    L"下載失敗（{}）。\n請確認網路連線，或開啟下載頁自行安裝。",
    L"下載的檔案沒有微軟的數位簽章，已刪除且沒有執行。\n請開啟下載頁自行安裝。",
    L"安裝程式結束時回傳錯誤碼 {}。\n請開啟下載頁自行安裝。",
    L"這個版本的 Locale Emulator 需要 64 位元的 Windows。",
    L"在 LEInstaller.exe 旁邊找不到 LEGUI.exe。\n請把 zip 的所有檔案解壓縮到同一個資料夾，再執行一次 LEInstaller.exe。",
    L"無法啟動 LEGUI（{}）。",
};

constexpr Strings kSimplifiedChinese{
    L"Locale Emulator 安装程序",
    L"需要安装 .NET 10 桌面运行时",
    L"Locale Emulator 需要以下 Microsoft .NET 10 桌面运行时（Desktop Runtime），这台电脑尚未安装：",
    L"x86（32 位）：LEProc 启动程序时使用",
    L"x64（64 位）：LEGUI 设置窗口使用",
    L"自动下载并安装\n从微软下载官方安装程序并运行，需要管理员权限。",
    L"打开下载页面\n自行下载安装后，再运行一次 LEInstaller。",
    L"打开下载页面",
    L"正在下载 .NET 10 桌面运行时（{}）",
    L"从微软官方网站下载。",
    L"下载失败（{}）。\n请检查网络连接，或打开下载页面自行安装。",
    L"下载的文件没有微软的数字签名，已删除且未运行。\n请打开下载页面自行安装。",
    L"安装程序结束时返回错误代码 {}。\n请打开下载页面自行安装。",
    L"此版本的 Locale Emulator 需要 64 位 Windows。",
    L"在 LEInstaller.exe 旁边找不到 LEGUI.exe。\n请把 zip 中的所有文件解压到同一个文件夹，再运行一次 LEInstaller.exe。",
    L"无法启动 LEGUI（{}）。",
};

constexpr Strings kJapanese{
    L"Locale Emulator セットアップ",
    L".NET 10 デスクトップ ランタイムのインストールが必要です",
    L"Locale Emulator には次の Microsoft .NET 10 デスクトップ ランタイム（Desktop Runtime）が必要ですが、このコンピューターにはインストールされていません：",
    L"x86（32 ビット）：LEProc がアプリケーションを起動するときに使用",
    L"x64（64 ビット）：設定画面 LEGUI で使用",
    L"自動でダウンロードしてインストール\nMicrosoft の公式インストーラーをダウンロードして実行します。管理者権限が必要です。",
    L"ダウンロード ページを開く\nご自身でダウンロードしてインストールした後、もう一度 LEInstaller を実行してください。",
    L"ダウンロード ページを開く",
    L".NET 10 デスクトップ ランタイム（{}）をダウンロードしています",
    L"Microsoft の公式サイトからダウンロードしています。",
    L"ダウンロードに失敗しました（{}）。\nインターネット接続を確認するか、ダウンロード ページを開いてご自身でインストールしてください。",
    L"ダウンロードしたファイルに Microsoft のデジタル署名がないため、削除して実行しませんでした。\nダウンロード ページを開いてご自身でインストールしてください。",
    L"インストーラーがエラー コード {} で終了しました。\nダウンロード ページを開いてご自身でインストールしてください。",
    L"このバージョンの Locale Emulator には 64 ビット版の Windows が必要です。",
    L"LEInstaller.exe と同じ場所に LEGUI.exe が見つかりません。\nzip ファイルのすべてのファイルを同じフォルダーに展開してから、もう一度 LEInstaller.exe を実行してください。",
    L"LEGUI を起動できませんでした（{}）。",
};

}  // namespace

Lang PickLanguage(LANGID uiLanguage)
{
    switch (PRIMARYLANGID(uiLanguage))
    {
    case LANG_JAPANESE:
        return Lang::Ja;
    case LANG_CHINESE:
        switch (SUBLANGID(uiLanguage))
        {
        case SUBLANG_CHINESE_TRADITIONAL:  // zh-TW
        case SUBLANG_CHINESE_HONGKONG:     // zh-HK
        case SUBLANG_CHINESE_MACAU:        // zh-MO
        case 0x1F:                         // zh-Hant (0x7C04)
            return Lang::ZhTw;
        default:                           // zh-CN, zh-SG, zh-Hans
            return Lang::ZhCn;
        }
    default:
        return Lang::En;
    }
}

const Strings& GetStrings(Lang lang)
{
    switch (lang)
    {
    case Lang::ZhTw:
        return kTraditionalChinese;
    case Lang::ZhCn:
        return kSimplifiedChinese;
    case Lang::Ja:
        return kJapanese;
    default:
        return kEnglish;
    }
}

}  // namespace LeInstaller
