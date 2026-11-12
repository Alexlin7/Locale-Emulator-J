// LocaleTestApp - Locale Emulator Smoke Test Target
// 將程序的 locale 相關 Windows API 回傳值寫入檔案，供 smoke test 腳本比對。
//
// 重要：LEProc 透過 LeCreateProcess 建立新程序，目標 app 的 stdout
// 不會回傳到 LEProc 的 stdout。因此必須寫檔而非 printf。
//
// 用法：LocaleTestApp.exe <output-file-path>
// 輸出格式為 KEY=VALUE，每行一個，便於 PowerShell 解析。
//
// CHECK_* 行：驗證各層 ANSI/OEM <-> Unicode 轉換是否使用程序的 code page
// （GetACP()/GetOEMCP()，在 LE 下即為 profile 的 code page）。
// 值為 OK 或 FAIL <說明>；smoke test 要求所有 CHECK_* 皆為 OK。
// 參考答案一律用 kernelbase 的 MultiByteToWideChar/WideCharToMultiByte 搭配
// 明確的 code page 編號計算（不受 LE 影響，永遠正確）。

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <mbctype.h>

typedef LONG NTSTATUS_;

typedef struct _UNICODE_STRING_ { USHORT Length, MaximumLength; PWSTR Buffer; } UNICODE_STRING_;
typedef struct _ANSI_STRING_ { USHORT Length, MaximumLength; PSTR Buffer; } ANSI_STRING_;

typedef NTSTATUS_ (NTAPI *MbToUnicodeN_t)(PWCH, ULONG, PULONG, const CHAR *, ULONG);
typedef NTSTATUS_ (NTAPI *UnicodeToMbN_t)(PCHAR, ULONG, PULONG, PCWCH, ULONG);
typedef NTSTATUS_ (NTAPI *MbToUnicodeSize_t)(PULONG, const CHAR *, ULONG);
typedef NTSTATUS_ (NTAPI *UnicodeToMbSize_t)(PULONG, PCWCH, ULONG);
typedef NTSTATUS_ (NTAPI *AnsiToUnicodeString_t)(UNICODE_STRING_ *, const ANSI_STRING_ *, BOOLEAN);
typedef NTSTATUS_ (NTAPI *UnicodeToAnsiString_t)(ANSI_STRING_ *, const UNICODE_STRING_ *, BOOLEAN);
typedef VOID (NTAPI *FreeUnicodeString_t)(UNICODE_STRING_ *);
typedef VOID (NTAPI *FreeAnsiString_t)(ANSI_STRING_ *);

// Sample characters from several scripts; each check keeps only those that the
// code page under test can represent exactly.
static const WCHAR kSample[] =
    L"ABC xyz 123 "
    L"日本語 "              // 日本語
    L"テストｱｲ "   // テスト + half-width katakana
    L"繁體中文 "         // 繁體中文
    L"简体中文 "         // 简体中文
    L"한국어 "               // 한국어
    L"éüß€£ "   // éüß€£
    L"ЖΩ";                     // ЖΩ

static FILE *g_Out;

static void Check(const char *name, bool ok, const char *detail)
{
    if (ok)
        fprintf(g_Out, "CHECK_%s=OK\n", name);
    else
        fprintf(g_Out, "CHECK_%s=FAIL %s\n", name, detail);
}

// Characters of kSample that code page `cp` maps exactly (no best fit, no default char).
static int RepresentableSample(UINT cp, WCHAR *out, int capacity)
{
    int n = 0;
    for (const WCHAR *p = kSample; *p && n < capacity - 1; ++p)
    {
        char buf[8];
        BOOL usedDefault = FALSE;
        BOOL *pUsedDefault = (cp == CP_UTF8) ? NULL : &usedDefault;
        int len = WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, p, 1, buf, sizeof(buf), NULL, pUsedDefault);
        if (len > 0 && !usedDefault)
            out[n++] = *p;
    }
    out[n] = 0;
    return n;
}

static void Hex(const void *data, int bytes, char *out, int capacity)
{
    const unsigned char *b = (const unsigned char *)data;
    int pos = 0;
    for (int i = 0; i < bytes && pos + 3 < capacity; ++i)
        pos += sprintf_s(out + pos, capacity - pos, "%02X", b[i]);
    out[pos] = 0;
}

// Verifies ntdll's conversion routines for one code page (ANSI or OEM).
static void CheckRtl(const char *prefix, UINT cp, MbToUnicodeN_t mbToU, UnicodeToMbN_t uToMb,
                     MbToUnicodeSize_t mbToUSize, UnicodeToMbSize_t uToMbSize)
{
    WCHAR wide[128];
    int wideChars = RepresentableSample(cp, wide, 128);

    char expectedMb[512];
    int expectedMbLen = WideCharToMultiByte(cp, 0, wide, wideChars, expectedMb, sizeof(expectedMb), NULL, NULL);

    char name[64], detail[600], hex[520];

    // Unicode -> multibyte
    if (uToMb)
    {
        char mb[512] = {};
        ULONG mbLen = 0;
        uToMb(mb, sizeof(mb), &mbLen, wide, wideChars * sizeof(WCHAR));
        bool ok = (int)mbLen == expectedMbLen && memcmp(mb, expectedMb, mbLen) == 0;
        Hex(mb, mbLen, hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", cp, hex);
        sprintf_s(name, "%s_U2MB", prefix);
        Check(name, ok, detail);
    }

    // multibyte -> Unicode
    if (mbToU)
    {
        WCHAR back[256] = {};
        ULONG backBytes = 0;
        mbToU(back, sizeof(back), &backBytes, expectedMb, expectedMbLen);
        bool ok = backBytes == wideChars * sizeof(WCHAR) && memcmp(back, wide, backBytes) == 0;
        Hex(back, backBytes, hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", cp, hex);
        sprintf_s(name, "%s_MB2U", prefix);
        Check(name, ok, detail);
    }

    // size queries
    if (mbToUSize)
    {
        ULONG bytes = 0;
        mbToUSize(&bytes, expectedMb, expectedMbLen);
        sprintf_s(detail, "cp=%u expected=%u got=%lu", cp, (unsigned)(wideChars * sizeof(WCHAR)), bytes);
        sprintf_s(name, "%s_MB2U_SIZE", prefix);
        Check(name, bytes == wideChars * sizeof(WCHAR), detail);
    }
    if (uToMbSize)
    {
        ULONG bytes = 0;
        uToMbSize(&bytes, wide, wideChars * sizeof(WCHAR));
        sprintf_s(detail, "cp=%u expected=%d got=%lu", cp, expectedMbLen, bytes);
        sprintf_s(name, "%s_U2MB_SIZE", prefix);
        Check(name, (int)bytes == expectedMbLen, detail);
    }
}

// Charset GDI associates with code page `cp` (gdi32's own table, not affected by LE).
static UINT ExpectedCharset(UINT cp)
{
    CHARSETINFO csi = {};
    if (!TranslateCharsetInfo((DWORD *)(UINT_PTR)cp, &csi, TCI_SRCCODEPAGE))
        return DEFAULT_CHARSET;
    return csi.ciCharset;
}

// Locale APIs beyond the ANSI/OEM conversions: code page queries, CP_THREAD_ACP /
// CP_OEMCP, locale info, registry redirection and GDI font charsets. The expected
// values are derived from GetACP()/GetOEMCP()/GetUserDefaultLCID(), which the
// smoke test already compares against the profile.
static void CheckLocaleApis(UINT acp, const WCHAR *wide, int wideChars, const char *acpMb, int acpMbLen)
{
    char detail[600], hex[520];
    UINT oemcp = GetOEMCP();
    LCID lcid = GetUserDefaultLCID();

    // GetCPInfoExW(CP_ACP / CP_OEMCP) must report the emulated code pages.
    {
        CPINFOEXW info = {};
        BOOL ok = GetCPInfoExW(CP_ACP, 0, &info);
        sprintf_s(detail, "expected=%u got=%u", acp, ok ? info.CodePage : 0);
        Check("KERNELBASE_GETCPINFO_ACP", ok && info.CodePage == acp, detail);

        CPINFOEXW oemInfo = {};
        ok = GetCPInfoExW(CP_OEMCP, 0, &oemInfo);
        sprintf_s(detail, "expected=%u got=%u", oemcp, ok ? oemInfo.CodePage : 0);
        Check("KERNELBASE_GETCPINFO_OEMCP", ok && oemInfo.CodePage == oemcp, detail);
    }

    // CP_THREAD_ACP follows the thread locale; LE makes it the profile's locale.
    {
        WCHAR back[256] = {};
        int n = MultiByteToWideChar(CP_THREAD_ACP, 0, acpMb, acpMbLen, back, 256);
        bool ok = n == wideChars && memcmp(back, wide, n * sizeof(WCHAR)) == 0;
        Hex(back, n * sizeof(WCHAR), hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", acp, hex);
        Check("KERNELBASE_CP_THREAD_ACP", ok, detail);
    }

    // CP_OEMCP through kernelbase.
    {
        WCHAR oemWide[128];
        int oemChars = RepresentableSample(oemcp, oemWide, 128);
        char oemMb[512];
        int oemMbLen = WideCharToMultiByte(oemcp, 0, oemWide, oemChars, oemMb, sizeof(oemMb), NULL, NULL);
        WCHAR back[256] = {};
        int n = MultiByteToWideChar(CP_OEMCP, 0, oemMb, oemMbLen, back, 256);
        bool ok = n == oemChars && memcmp(back, oemWide, n * sizeof(WCHAR)) == 0;
        Hex(back, n * sizeof(WCHAR), hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", oemcp, hex);
        Check("KERNELBASE_CP_OEMCP", ok, detail);
    }

    // Locale info of the user default locale must be consistent with it.
    {
        WCHAR value[32] = {};
        GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, value, 32);
        WCHAR expected[32];
        swprintf_s(expected, L"%u", acp);
        char got[64];
        WideCharToMultiByte(CP_UTF8, 0, value, -1, got, sizeof(got), NULL, NULL);
        sprintf_s(detail, "expected=%u got=%s", acp, got);
        Check("KERNELBASE_LOCALEINFO_ACP", wcscmp(value, expected) == 0, detail);

        WCHAR name[LOCALE_NAME_MAX_LENGTH] = {}, expectedName[LOCALE_NAME_MAX_LENGTH] = {};
        GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH);
        LCIDToLocaleName(lcid, expectedName, LOCALE_NAME_MAX_LENGTH, 0);
        char gotName[128], expName[128];
        WideCharToMultiByte(CP_UTF8, 0, name, -1, gotName, sizeof(gotName), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, expectedName, -1, expName, sizeof(expName), NULL, NULL);
        sprintf_s(detail, "expected=%s got=%s", expName, gotName);
        Check("KERNELBASE_USER_DEFAULT_LOCALE_NAME", wcscmp(name, expectedName) == 0, detail);
    }

    // Registry redirection (the smoke test profile sets RedirectRegistry=true).
    {
        static const struct { const char *check; const WCHAR *value; } kValues[] =
        {
            { "REGISTRY_NLS_ACP",   L"ACP"   },
            { "REGISTRY_NLS_OEMCP", L"OEMCP" },
        };
        for (const auto &v : kValues)
        {
            WCHAR data[32] = {};
            DWORD size = sizeof(data);
            LSTATUS st = RegGetValueW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\Nls\\CodePage",
                                      v.value, RRF_RT_REG_SZ, NULL, data, &size);
            WCHAR expected[32];
            swprintf_s(expected, L"%u", v.value[0] == L'A' ? acp : oemcp);
            char got[64];
            WideCharToMultiByte(CP_UTF8, 0, data, -1, got, sizeof(got), NULL, NULL);
            sprintf_s(detail, "status=%ld expected=%u got=%s", st, v.value[0] == L'A' ? acp : oemcp, got);
            Check(v.check, st == ERROR_SUCCESS && wcscmp(data, expected) == 0, detail);
        }
    }

    // GDI: stock fonts and the default font of a new memory DC use the charset of
    // the emulated code page.
    {
        UINT expected = ExpectedCharset(acp);

        LOGFONTW lf = {};
        int n = GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
        sprintf_s(detail, "cp=%u expected=%u got=%u", acp, expected, n ? lf.lfCharSet : 0xFFFF);
        Check("GDI32_STOCK_FONT_CHARSET", n != 0 && lf.lfCharSet == expected, detail);

        HDC dc = CreateCompatibleDC(NULL);
        int cs = dc ? GetTextCharset(dc) : -1;
        sprintf_s(detail, "cp=%u expected=%u got=%d", acp, expected, cs);
        Check("GDI32_COMPATIBLE_DC_CHARSET", cs == (int)expected, detail);
        if (dc) DeleteDC(dc);
    }
}

static void CheckConversions()
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    UINT acp = GetACP();
    UINT oemcp = GetOEMCP();

    CheckRtl("RTL_ANSI", acp,
             (MbToUnicodeN_t)GetProcAddress(ntdll, "RtlMultiByteToUnicodeN"),
             (UnicodeToMbN_t)GetProcAddress(ntdll, "RtlUnicodeToMultiByteN"),
             (MbToUnicodeSize_t)GetProcAddress(ntdll, "RtlMultiByteToUnicodeSize"),
             (UnicodeToMbSize_t)GetProcAddress(ntdll, "RtlUnicodeToMultiByteSize"));

    CheckRtl("RTL_OEM", oemcp,
             (MbToUnicodeN_t)GetProcAddress(ntdll, "RtlOemToUnicodeN"),
             (UnicodeToMbN_t)GetProcAddress(ntdll, "RtlUnicodeToOemN"),
             NULL, NULL);

    WCHAR wide[128];
    int wideChars = RepresentableSample(acp, wide, 128);
    char expectedMb[512];
    int expectedMbLen = WideCharToMultiByte(acp, 0, wide, wideChars, expectedMb, sizeof(expectedMb), NULL, NULL);
    expectedMb[expectedMbLen] = 0;
    char detail[600], hex[520];

    // RtlAnsiStringToUnicodeString / RtlUnicodeStringToAnsiString (used by kernelbase *A APIs)
    auto ansiToU = (AnsiToUnicodeString_t)GetProcAddress(ntdll, "RtlAnsiStringToUnicodeString");
    auto uToAnsi = (UnicodeToAnsiString_t)GetProcAddress(ntdll, "RtlUnicodeStringToAnsiString");
    auto freeU = (FreeUnicodeString_t)GetProcAddress(ntdll, "RtlFreeUnicodeString");
    auto freeA = (FreeAnsiString_t)GetProcAddress(ntdll, "RtlFreeAnsiString");
    {
        ANSI_STRING_ a = { (USHORT)expectedMbLen, (USHORT)(expectedMbLen + 1), expectedMb };
        UNICODE_STRING_ u = {};
        ansiToU(&u, &a, TRUE);
        bool ok = u.Length == wideChars * sizeof(WCHAR) && memcmp(u.Buffer, wide, u.Length) == 0;
        Hex(u.Buffer, u.Length, hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", acp, hex);
        Check("RTL_ANSI_STRING_TO_UNICODE", ok, detail);
        freeU(&u);
    }
    {
        UNICODE_STRING_ u = { (USHORT)(wideChars * sizeof(WCHAR)), (USHORT)(wideChars * sizeof(WCHAR)), wide };
        ANSI_STRING_ a = {};
        uToAnsi(&a, &u, TRUE);
        bool ok = a.Length == expectedMbLen && memcmp(a.Buffer, expectedMb, a.Length) == 0;
        Hex(a.Buffer, a.Length, hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", acp, hex);
        Check("RTL_UNICODE_TO_ANSI_STRING", ok, detail);
        freeA(&a);
    }

    // Other ntdll string helpers that may convert internally without going through
    // RtlMultiByteToUnicodeN & co.
    {
        typedef NTSTATUS_ (NTAPI *OemToUnicodeString_t)(UNICODE_STRING_ *, const ANSI_STRING_ *, BOOLEAN);
        typedef NTSTATUS_ (NTAPI *UnicodeToOemString_t)(ANSI_STRING_ *, const UNICODE_STRING_ *, BOOLEAN);
        typedef ULONG (NTAPI *AnsiStringToUnicodeSize_t)(const ANSI_STRING_ *);
        typedef ULONG (NTAPI *UnicodeStringToAnsiSize_t)(const UNICODE_STRING_ *);
        typedef NTSTATUS_ (NTAPI *UpcaseToMbN_t)(PCHAR, ULONG, PULONG, PCWCH, ULONG);
        typedef NTSTATUS_ (NTAPI *UpcaseStringToAnsi_t)(ANSI_STRING_ *, const UNICODE_STRING_ *, BOOLEAN);
        typedef WCHAR (NTAPI *AnsiCharToUnicodeChar_t)(PUCHAR *);

        UINT oemcp = GetOEMCP();
        WCHAR oemWide[128];
        int oemChars = RepresentableSample(oemcp, oemWide, 128);
        char oemMb[512];
        int oemMbLen = WideCharToMultiByte(oemcp, 0, oemWide, oemChars, oemMb, sizeof(oemMb), NULL, NULL);
        oemMb[oemMbLen] = 0;

        auto oemToU = (OemToUnicodeString_t)GetProcAddress(ntdll, "RtlOemStringToUnicodeString");
        {
            ANSI_STRING_ a = { (USHORT)oemMbLen, (USHORT)(oemMbLen + 1), oemMb };
            UNICODE_STRING_ u = {};
            oemToU(&u, &a, TRUE);
            bool ok = u.Length == oemChars * sizeof(WCHAR) && memcmp(u.Buffer, oemWide, u.Length) == 0;
            Hex(u.Buffer, u.Length, hex, sizeof(hex));
            sprintf_s(detail, "cp=%u got=%s", oemcp, hex);
            Check("RTL_OEM_STRING_TO_UNICODE", ok, detail);
            freeU(&u);
        }
        auto uToOem = (UnicodeToOemString_t)GetProcAddress(ntdll, "RtlUnicodeStringToOemString");
        {
            UNICODE_STRING_ u = { (USHORT)(oemChars * sizeof(WCHAR)), (USHORT)(oemChars * sizeof(WCHAR)), oemWide };
            ANSI_STRING_ a = {};
            uToOem(&a, &u, TRUE);
            bool ok = a.Length == oemMbLen && memcmp(a.Buffer, oemMb, a.Length) == 0;
            Hex(a.Buffer, a.Length, hex, sizeof(hex));
            sprintf_s(detail, "cp=%u got=%s", oemcp, hex);
            Check("RTL_UNICODE_TO_OEM_STRING", ok, detail);
            freeA(&a);
        }

        auto ansiSize = (AnsiStringToUnicodeSize_t)GetProcAddress(ntdll, "RtlxAnsiStringToUnicodeSize");
        {
            ANSI_STRING_ a = { (USHORT)expectedMbLen, (USHORT)(expectedMbLen + 1), expectedMb };
            ULONG got = ansiSize(&a);
            sprintf_s(detail, "cp=%u expected=%u got=%lu", acp, (unsigned)((wideChars + 1) * sizeof(WCHAR)), got);
            Check("RTL_X_ANSI_STRING_TO_UNICODE_SIZE", got == (wideChars + 1) * sizeof(WCHAR), detail);
        }
        auto uniSize = (UnicodeStringToAnsiSize_t)GetProcAddress(ntdll, "RtlxUnicodeStringToAnsiSize");
        {
            UNICODE_STRING_ u = { (USHORT)(wideChars * sizeof(WCHAR)), (USHORT)(wideChars * sizeof(WCHAR)), wide };
            ULONG got = uniSize(&u);
            sprintf_s(detail, "cp=%u expected=%d got=%lu", acp, expectedMbLen + 1, got);
            Check("RTL_X_UNICODE_STRING_TO_ANSI_SIZE", (int)got == expectedMbLen + 1, detail);
        }

        // Upcase variants: compare against converting the upcased string.
        WCHAR upper[128];
        memcpy(upper, wide, (wideChars + 1) * sizeof(WCHAR));
        CharUpperBuffW(upper, wideChars);
        char upperMb[512];
        int upperMbLen = WideCharToMultiByte(acp, 0, upper, wideChars, upperMb, sizeof(upperMb), NULL, NULL);

        auto upToMb = (UpcaseToMbN_t)GetProcAddress(ntdll, "RtlUpcaseUnicodeToMultiByteN");
        {
            char mb[512] = {};
            ULONG mbLen = 0;
            upToMb(mb, sizeof(mb), &mbLen, wide, wideChars * sizeof(WCHAR));
            bool ok = (int)mbLen == upperMbLen && memcmp(mb, upperMb, mbLen) == 0;
            Hex(mb, mbLen, hex, sizeof(hex));
            sprintf_s(detail, "cp=%u got=%s", acp, hex);
            Check("RTL_UPCASE_U2MB", ok, detail);
        }
        auto upToOem = (UpcaseToMbN_t)GetProcAddress(ntdll, "RtlUpcaseUnicodeToOemN");
        {
            WCHAR oemUpper[128];
            memcpy(oemUpper, oemWide, (oemChars + 1) * sizeof(WCHAR));
            CharUpperBuffW(oemUpper, oemChars);
            char oemUpperMb[512];
            int oemUpperMbLen = WideCharToMultiByte(oemcp, 0, oemUpper, oemChars, oemUpperMb, sizeof(oemUpperMb), NULL, NULL);

            char mb[512] = {};
            ULONG mbLen = 0;
            upToOem(mb, sizeof(mb), &mbLen, oemWide, oemChars * sizeof(WCHAR));
            bool ok = (int)mbLen == oemUpperMbLen && memcmp(mb, oemUpperMb, mbLen) == 0;
            Hex(mb, mbLen, hex, sizeof(hex));
            sprintf_s(detail, "cp=%u got=%s", oemcp, hex);
            Check("RTL_UPCASE_U2OEM", ok, detail);
        }

        auto upToAnsi = (UpcaseStringToAnsi_t)GetProcAddress(ntdll, "RtlUpcaseUnicodeStringToAnsiString");
        {
            UNICODE_STRING_ u = { (USHORT)(wideChars * sizeof(WCHAR)), (USHORT)(wideChars * sizeof(WCHAR)), wide };
            ANSI_STRING_ a = {};
            upToAnsi(&a, &u, TRUE);
            bool ok = a.Length == upperMbLen && memcmp(a.Buffer, upperMb, a.Length) == 0;
            Hex(a.Buffer, a.Length, hex, sizeof(hex));
            sprintf_s(detail, "cp=%u got=%s", acp, hex);
            Check("RTL_UPCASE_UNICODE_TO_ANSI_STRING", ok, detail);
            freeA(&a);
        }

        auto charToU = (AnsiCharToUnicodeChar_t)GetProcAddress(ntdll, "RtlAnsiCharToUnicodeChar");
        {
            // Convert the first non-ASCII character of the sample one character at a time.
            int i = 0;
            while (i < wideChars && wide[i] < 0x80) ++i;
            if (i < wideChars)
            {
                char one[8];
                int oneLen = WideCharToMultiByte(acp, 0, &wide[i], 1, one, sizeof(one), NULL, NULL);
                PUCHAR p = (PUCHAR)one;
                WCHAR got = charToU(&p);
                sprintf_s(detail, "cp=%u expected=%04X got=%04X consumed=%d", acp, wide[i], got, (int)(p - (PUCHAR)one));
                Check("RTL_ANSI_CHAR_TO_UNICODE_CHAR", got == wide[i] && (p - (PUCHAR)one) == oneLen, detail);
            }
        }
    }

    // File APIs with a non-ASCII file name passed as ANSI (code page bytes).
    // The file is created with the Unicode API, then opened through the *A APIs.
    {
        WCHAR nameChars[16];
        int nameLen = 0;
        for (int i = 0; i < wideChars && nameLen < 6; ++i)
            if (wide[i] >= 0x80) nameChars[nameLen++] = wide[i];
        nameChars[nameLen] = 0;

        WCHAR tempDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tempDir);
        WCHAR dirW[MAX_PATH], fileW[MAX_PATH];
        swprintf_s(dirW, L"%sLETest_%lu", tempDir, GetCurrentProcessId());
        swprintf_s(fileW, L"%s\\le_%s.tmp", dirW, nameChars);
        CreateDirectoryW(dirW, NULL);
        HANDLE h = CreateFileW(fileW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

        char dirA[MAX_PATH * 2], fileA[MAX_PATH * 2], nameA[64];
        WideCharToMultiByte(acp, 0, dirW, -1, dirA, sizeof(dirA), NULL, NULL);
        WideCharToMultiByte(acp, 0, fileW, -1, fileA, sizeof(fileA), NULL, NULL);
        WCHAR nameW[32];
        swprintf_s(nameW, L"le_%s.tmp", nameChars);
        WideCharToMultiByte(acp, 0, nameW, -1, nameA, sizeof(nameA), NULL, NULL);

        HANDLE hA = CreateFileA(fileA, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        sprintf_s(detail, "cp=%u error=%lu", acp, hA == INVALID_HANDLE_VALUE ? GetLastError() : 0);
        Check("FILEAPI_CREATEFILE_A", hA != INVALID_HANDLE_VALUE, detail);
        if (hA != INVALID_HANDLE_VALUE) CloseHandle(hA);

        DWORD attr = GetFileAttributesA(fileA);
        sprintf_s(detail, "cp=%u error=%lu", acp, attr == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0);
        Check("FILEAPI_GETFILEATTRIBUTES_A", attr != INVALID_FILE_ATTRIBUTES, detail);

        char pattern[MAX_PATH * 2];
        sprintf_s(pattern, "%s\\*.tmp", dirA);
        WIN32_FIND_DATAA fd;
        HANDLE hf = FindFirstFileA(pattern, &fd);
        bool found = hf != INVALID_HANDLE_VALUE && strcmp(fd.cFileName, nameA) == 0;
        Hex(hf != INVALID_HANDLE_VALUE ? fd.cFileName : "", hf != INVALID_HANDLE_VALUE ? (int)strlen(fd.cFileName) : 0, hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", acp, hex);
        Check("FILEAPI_FINDFIRSTFILE_A", found, detail);
        if (hf != INVALID_HANDLE_VALUE) FindClose(hf);

        FILE *fp = NULL;
        fopen_s(&fp, fileA, "rb");
        sprintf_s(detail, "cp=%u", acp);
        Check("FILEAPI_FOPEN", fp != NULL, detail);
        if (fp) fclose(fp);

        DeleteFileW(fileW);
        RemoveDirectoryW(dirW);
    }

    // kernelbase CP_ACP (reads the code page LE installs through the PEB)
    {
        WCHAR back[256] = {};
        int n = MultiByteToWideChar(CP_ACP, 0, expectedMb, expectedMbLen, back, 256);
        bool ok = n == wideChars && memcmp(back, wide, n * sizeof(WCHAR)) == 0;
        Hex(back, n * sizeof(WCHAR), hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", acp, hex);
        Check("KERNELBASE_CP_ACP", ok, detail);
    }

    // user32: window title set through an ANSI API, read back as Unicode
    {
        HWND w = CreateWindowExA(0, "STATIC", expectedMb, 0, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
        WCHAR title[256] = {};
        int n = GetWindowTextW(w, title, 256);
        bool ok = w != NULL && n == wideChars && memcmp(title, wide, n * sizeof(WCHAR)) == 0;
        Hex(title, n * sizeof(WCHAR), hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", acp, hex);
        Check("USER32_WINDOW_TEXT_A", ok, detail);
        if (w) DestroyWindow(w);
    }

    CheckLocaleApis(acp, wide, wideChars, expectedMb, expectedMbLen);
}

// ============================================================================
// PROBE_* / INFO_*: candidate APIs for the system-ACP-65001 (UTF-8 beta) leak
// (docs/roadmap.md). PROBE_* uses the same OK / FAIL format as CHECK_* but the
// smoke test only reports it; INFO_* is plain diagnostic output. The expected
// values follow the CHECK_* rules: derived from GetACP() with explicit code
// page numbers.
// ============================================================================

static void Probe(const char *name, bool ok, const char *detail)
{
    if (ok)
        fprintf(g_Out, "PROBE_%s=OK\n", name);
    else
        fprintf(g_Out, "PROBE_%s=FAIL %s\n", name, detail);
}

static void Info(const char *name, const char *value)
{
    fprintf(g_Out, "INFO_%s=%s\n", name, (value && *value) ? value : "(empty)");
}

static void ProbeBytes(const char *name, UINT cp, const char *got, int gotLen, const char *expected, int expectedLen)
{
    char hex[520], detail[600];
    bool ok = gotLen == expectedLen && memcmp(got, expected, expectedLen) == 0;
    Hex(got, gotLen > 0 ? gotLen : 0, hex, sizeof(hex));
    sprintf_s(detail, "cp=%u got=%s", cp, hex);
    Probe(name, ok, detail);
}

static void ProbeWide(const char *name, UINT cp, const WCHAR *got, int gotChars, const WCHAR *expected, int expectedChars)
{
    char hex[520], detail[600];
    bool ok = gotChars == expectedChars && memcmp(got, expected, expectedChars * sizeof(WCHAR)) == 0;
    Hex(got, gotChars > 0 ? gotChars * (int)sizeof(WCHAR) : 0, hex, sizeof(hex));
    sprintf_s(detail, "cp=%u got=%s", cp, hex);
    Probe(name, ok, detail);
}

// Code page named by a CRT locale string such as "Japanese_Japan.932" or ".utf8".
static UINT CodePageOfLocaleString(const char *locale)
{
    if (!locale) return 0;
    const char *dot = strrchr(locale, '.');
    if (!dot) return 0;
    if (_stricmp(dot + 1, "utf8") == 0 || _stricmp(dot + 1, "utf-8") == 0) return CP_UTF8;
    return (UINT)atoi(dot + 1);
}

struct ProbeData
{
    UINT  acp;
    WCHAR wide[128];      // sample characters the ACP can represent
    int   wideChars;
    char  mb[512];        // the same text in the ACP (NUL-terminated)
    int   mbLen;
    int   firstNonAscii;  // index into wide, or -1
};

static void ProbeKernelbase(const ProbeData &d)
{
    char buf[512];

    // W -> MB through CP_ACP / CP_THREAD_ACP (CHECK_KERNELBASE_* only covers MB -> W).
    int n = WideCharToMultiByte(CP_ACP, 0, d.wide, d.wideChars, buf, sizeof(buf), NULL, NULL);
    ProbeBytes("KERNELBASE_CP_ACP_W2MB", d.acp, buf, n, d.mb, d.mbLen);
    n = WideCharToMultiByte(CP_THREAD_ACP, 0, d.wide, d.wideChars, buf, sizeof(buf), NULL, NULL);
    ProbeBytes("KERNELBASE_CP_THREAD_ACP_W2MB", d.acp, buf, n, d.mb, d.mbLen);

    // IsDBCSLeadByte (ACP) must agree with IsDBCSLeadByteEx(<explicit ACP>) for every byte.
    {
        int mismatch = -1;
        for (int b = 0x80; b <= 0xFF && mismatch < 0; ++b)
            if ((IsDBCSLeadByte((BYTE)b) != FALSE) != (IsDBCSLeadByteEx(d.acp, (BYTE)b) != FALSE))
                mismatch = b;
        char detail[64];
        sprintf_s(detail, "cp=%u first mismatching byte=%02X", d.acp, mismatch);
        Probe("KERNELBASE_ISDBCSLEADBYTE", mismatch < 0, detail);
    }

    // GetLocaleInfoA with LOCALE_USE_CP_ACP converts through CP_ACP.
    {
        WCHAR nameW[128] = {};
        int wn = GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SNATIVELANGUAGENAME, nameW, 128);
        char expected[256];
        int en = WideCharToMultiByte(d.acp, 0, nameW, wn > 0 ? wn - 1 : 0, expected, sizeof(expected), NULL, NULL);
        char got[256] = {};
        int gn = GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SNATIVELANGUAGENAME | LOCALE_USE_CP_ACP, got, sizeof(got));
        ProbeBytes("KERNELBASE_GETLOCALEINFO_A", d.acp, got, gn > 0 ? gn - 1 : 0, expected, en);
    }

    // Environment variable set as Unicode, read back as ANSI.
    {
        WCHAR value[129];
        memcpy(value, d.wide, d.wideChars * sizeof(WCHAR));
        value[d.wideChars] = 0;
        SetEnvironmentVariableW(L"LE_PROBE_VALUE", value);
        char got[512] = {};
        DWORD gn = GetEnvironmentVariableA("LE_PROBE_VALUE", got, sizeof(got));
        ProbeBytes("KERNELBASE_ENVIRONMENT_A", d.acp, got, (int)gn, d.mb, d.mbLen);
        SetEnvironmentVariableW(L"LE_PROBE_VALUE", NULL);
    }

    // Current directory set as Unicode (non-ASCII name), read back as ANSI.
    {
        WCHAR nameChars[8];
        int nameLen = 0;
        for (int i = 0; i < d.wideChars && nameLen < 4; ++i)
            if (d.wide[i] >= 0x80) nameChars[nameLen++] = d.wide[i];
        nameChars[nameLen] = 0;

        WCHAR tempDir[MAX_PATH], dirW[MAX_PATH], oldDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tempDir);
        swprintf_s(dirW, L"%sLEProbe_%lu_%s", tempDir, GetCurrentProcessId(), nameChars);
        CreateDirectoryW(dirW, NULL);
        GetCurrentDirectoryW(MAX_PATH, oldDir);

        if (SetCurrentDirectoryW(dirW))
        {
            WCHAR curW[MAX_PATH];
            int cn = (int)GetCurrentDirectoryW(MAX_PATH, curW);
            char expected[MAX_PATH * 2];
            int en = WideCharToMultiByte(d.acp, 0, curW, cn, expected, sizeof(expected), NULL, NULL);
            char got[MAX_PATH * 2] = {};
            DWORD gn = GetCurrentDirectoryA(sizeof(got), got);
            ProbeBytes("KERNELBASE_CURRENT_DIRECTORY_A", d.acp, got, (int)gn, expected, en);
            SetCurrentDirectoryW(oldDir);
        }
        else
        {
            Info("KERNELBASE_CURRENT_DIRECTORY_A", "skipped: cannot enter test directory");
        }
        RemoveDirectoryW(dirW);
    }
}

static void ProbeUser32(const ProbeData &d)
{
    char got[512];

    HWND w = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    if (w)
    {
        WCHAR title[129];
        memcpy(title, d.wide, d.wideChars * sizeof(WCHAR));
        title[d.wideChars] = 0;

        // Unicode title read back through the ANSI paths.
        SetWindowTextW(w, title);
        memset(got, 0, sizeof(got));
        int n = GetWindowTextA(w, got, sizeof(got));
        ProbeBytes("USER32_GETWINDOWTEXT_A", d.acp, got, n, d.mb, d.mbLen);

        memset(got, 0, sizeof(got));
        n = (int)SendMessageA(w, WM_GETTEXT, sizeof(got), (LPARAM)got);
        ProbeBytes("USER32_WM_GETTEXT_A", d.acp, got, n, d.mb, d.mbLen);

        // ANSI title set through a message, read back as Unicode.
        SetWindowTextW(w, L"");
        SendMessageA(w, WM_SETTEXT, 0, (LPARAM)d.mb);
        WCHAR back[256] = {};
        int bn = GetWindowTextW(w, back, 256);
        ProbeWide("USER32_WM_SETTEXT_A", d.acp, back, bn, d.wide, d.wideChars);

        DestroyWindow(w);
    }
    else
    {
        Info("USER32_WINDOW", "skipped: CreateWindowExW failed");
    }

    // wsprintfA with a Unicode string argument.
    {
        WCHAR arg[129];
        memcpy(arg, d.wide, d.wideChars * sizeof(WCHAR));
        arg[d.wideChars] = 0;
        char out[1024] = {};
        int n = wsprintfA(out, "%ls", arg);
        ProbeBytes("USER32_WSPRINTF_LS", d.acp, out, n, d.mb, d.mbLen);
    }

    // CharNextA must step over one ACP character at a time (two bytes for DBCS).
    {
        int steps = 0;
        const char *p = d.mb;
        while (*p && steps < 1000)
        {
            p = CharNextA(p);
            ++steps;
        }
        char detail[64];
        sprintf_s(detail, "cp=%u expected=%d got=%d", d.acp, d.wideChars, steps);
        Probe("USER32_CHARNEXT_A", steps == d.wideChars, detail);
    }
}

typedef char  *(__cdecl *crt_setlocale_t)(int, const char *);
typedef size_t (__cdecl *crt_wcstombs_t)(char *, const wchar_t *, size_t);
typedef size_t (__cdecl *crt_mbstowcs_t)(wchar_t *, const char *, size_t);
typedef int    (__cdecl *crt_getmbcp_t)(void);
typedef int    (__cdecl *crt_setmbcp_t)(int);
typedef int    (__cdecl *crt_ismbblead_t)(unsigned int);

struct CrtFunctions
{
    crt_setlocale_t setlocale_;
    crt_wcstombs_t  wcstombs_;
    crt_mbstowcs_t  mbstowcs_;
    crt_getmbcp_t   getmbcp_;
    crt_setmbcp_t   setmbcp_;
    crt_ismbblead_t ismbblead_;
};

// Runs the same probes against one CRT: this program's static UCRT ("CRT") or
// the system msvcrt.dll ("MSVCRT"), which is closer to what old games link.
static void ProbeCrt(const char *prefix, const CrtFunctions &f, const ProbeData &d)
{
    char name[64], detail[256], value[128];

    sprintf_s(value, "%d", f.getmbcp_());
    sprintf_s(name, "%s_MBCP_INITIAL", prefix);
    Info(name, value);

    // Old CRTs call _setmbcp(_MB_CP_ANSI) at startup; the _mbs* functions use it.
    f.setmbcp_(-3 /* _MB_CP_ANSI */);
    int mbcp = f.getmbcp_();
    sprintf_s(detail, "expected=%u got=%d", d.acp, mbcp);
    sprintf_s(name, "%s_SETMBCP_ANSI", prefix);
    Probe(name, mbcp == (int)d.acp, detail);

    {
        int mismatch = -1;
        for (int b = 0x80; b <= 0xFF && mismatch < 0; ++b)
            if ((f.ismbblead_((unsigned)b) != 0) != (IsDBCSLeadByteEx(d.acp, (BYTE)b) != FALSE))
                mismatch = b;
        sprintf_s(detail, "cp=%u first mismatching byte=%02X", d.acp, mismatch);
        sprintf_s(name, "%s_ISMBBLEAD", prefix);
        Probe(name, mismatch < 0, detail);
    }

    // setlocale(LC_ALL, "") picks the user default locale and its code page.
    const char *loc = f.setlocale_(LC_ALL, "");
    sprintf_s(name, "%s_SETLOCALE_DEFAULT", prefix);
    Info(name, loc ? loc : "(null)");
    UINT locCp = CodePageOfLocaleString(loc);
    sprintf_s(detail, "expected=%u got=%u (%s)", d.acp, locCp, loc ? loc : "null");
    sprintf_s(name, "%s_SETLOCALE_DEFAULT_CP", prefix);
    Probe(name, locCp == d.acp, detail);

    WCHAR wideZ[129];
    memcpy(wideZ, d.wide, d.wideChars * sizeof(WCHAR));
    wideZ[d.wideChars] = 0;
    {
        char out[512] = {};
        size_t n = f.wcstombs_(out, wideZ, sizeof(out));
        sprintf_s(name, "%s_WCSTOMBS", prefix);
        ProbeBytes(name, d.acp, out, n == (size_t)-1 ? 0 : (int)n, d.mb, d.mbLen);
    }
    {
        WCHAR out[256] = {};
        size_t n = f.mbstowcs_(out, d.mb, 256);
        sprintf_s(name, "%s_MBSTOWCS", prefix);
        ProbeWide(name, d.acp, out, n == (size_t)-1 ? 0 : (int)n, d.wide, d.wideChars);
    }

    loc = f.setlocale_(LC_ALL, ".ACP");
    sprintf_s(name, "%s_SETLOCALE_ACP", prefix);
    Info(name, loc ? loc : "(null)");
    locCp = CodePageOfLocaleString(loc);
    sprintf_s(detail, "expected=%u got=%u (%s)", d.acp, locCp, loc ? loc : "null");
    sprintf_s(name, "%s_SETLOCALE_ACP_CP", prefix);
    Probe(name, locCp == d.acp, detail);

    f.setlocale_(LC_ALL, "C");
}

static void ProbeCrts(const ProbeData &d)
{
    CrtFunctions ucrt = { setlocale, wcstombs, mbstowcs, _getmbcp, _setmbcp,
                          [](unsigned c) -> int { return _ismbblead(c); } };
    ProbeCrt("CRT", ucrt, d);

    // sprintf("%ls") converts with the CRT locale's code page.
    setlocale(LC_ALL, "");
    {
        WCHAR arg[129];
        memcpy(arg, d.wide, d.wideChars * sizeof(WCHAR));
        arg[d.wideChars] = 0;
        char out[512] = {};
        int n = sprintf_s(out, "%ls", arg);
        ProbeBytes("CRT_SPRINTF_LS", d.acp, out, n < 0 ? 0 : n, d.mb, d.mbLen);
    }
    setlocale(LC_ALL, "C");

    HMODULE msvcrt = LoadLibraryW(L"msvcrt.dll");
    if (!msvcrt)
    {
        Info("MSVCRT", "skipped: msvcrt.dll not loaded");
        return;
    }
    CrtFunctions old = {
        (crt_setlocale_t)GetProcAddress(msvcrt, "setlocale"),
        (crt_wcstombs_t)GetProcAddress(msvcrt, "wcstombs"),
        (crt_mbstowcs_t)GetProcAddress(msvcrt, "mbstowcs"),
        (crt_getmbcp_t)GetProcAddress(msvcrt, "_getmbcp"),
        (crt_setmbcp_t)GetProcAddress(msvcrt, "_setmbcp"),
        (crt_ismbblead_t)GetProcAddress(msvcrt, "_ismbblead"),
    };
    if (old.setlocale_ && old.wcstombs_ && old.mbstowcs_ && old.getmbcp_ && old.setmbcp_ && old.ismbblead_)
        ProbeCrt("MSVCRT", old, d);
    else
        Info("MSVCRT", "skipped: missing exports");
}

// 32-bit top-down DIB to render text into and compare pixels.
struct Canvas
{
    HDC     dc;
    HBITMAP bitmap;
    HGDIOBJ oldBitmap;
    UINT32 *bits;
    int     width, height;
};

static bool CreateCanvas(Canvas &c, int width, int height)
{
    c = {};
    c.width = width;
    c.height = height;
    c.dc = CreateCompatibleDC(NULL);
    if (!c.dc) return false;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    c.bitmap = CreateDIBSection(c.dc, &bi, DIB_RGB_COLORS, (void **)&c.bits, NULL, 0);
    if (!c.bitmap) { DeleteDC(c.dc); c.dc = NULL; return false; }
    c.oldBitmap = SelectObject(c.dc, c.bitmap);
    SetBkMode(c.dc, TRANSPARENT);
    SetTextColor(c.dc, RGB(0, 0, 0));
    return true;
}

static void ClearCanvas(Canvas &c)
{
    GdiFlush();
    memset(c.bits, 0xFF, (size_t)c.width * c.height * 4);
}

static void DestroyCanvas(Canvas &c)
{
    if (!c.dc) return;
    SelectObject(c.dc, c.oldBitmap);
    DeleteObject(c.bitmap);
    DeleteDC(c.dc);
}

static bool CanvasHasInk(const Canvas &c)
{
    for (int i = 0; i < c.width * c.height; ++i)
        if ((c.bits[i] & 0x00FFFFFF) != 0x00FFFFFF) return true;
    return false;
}

// Renders the sample once through the W API and once through the A API and
// compares the pixels; `draw` is called with ansi=false / true.
template <typename Draw>
static void ProbeRender(const char *name, Canvas &c, UINT cp, Draw draw)
{
    const size_t pixels = (size_t)c.width * c.height;
    UINT32 *wideRender = (UINT32 *)malloc(pixels * 4);
    if (!wideRender) return;

    ClearCanvas(c);
    draw(false);
    GdiFlush();
    bool ink = CanvasHasInk(c);
    memcpy(wideRender, c.bits, pixels * 4);

    ClearCanvas(c);
    draw(true);
    GdiFlush();
    size_t diff = 0;
    for (size_t i = 0; i < pixels; ++i)
        if (c.bits[i] != wideRender[i]) ++diff;
    free(wideRender);

    char detail[96];
    sprintf_s(detail, "cp=%u differing pixels=%zu ink=%d", cp, diff, ink ? 1 : 0);
    Probe(name, ink && diff == 0, detail);
}

static void ProbeGdiFont(const char *suffix, BYTE charset, Canvas &c, const ProbeData &d)
{
    char name[64], detail[256], value[64];

    HFONT font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, charset,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"");
    HGDIOBJ oldFont = SelectObject(c.dc, font);

    sprintf_s(value, "requested=%u resolved=%d", charset, GetTextCharset(c.dc));
    sprintf_s(name, "GDI32_FONT_CHARSET_%s", suffix);
    Info(name, value);

    // Text extent of the same text through A and W.
    {
        SIZE a = {}, w = {};
        GetTextExtentPoint32A(c.dc, d.mb, d.mbLen, &a);
        GetTextExtentPoint32W(c.dc, d.wide, d.wideChars, &w);
        sprintf_s(detail, "cp=%u A=%ldx%ld W=%ldx%ld", d.acp, a.cx, a.cy, w.cx, w.cy);
        sprintf_s(name, "GDI32_TEXTEXTENT_A_VS_W_%s", suffix);
        Probe(name, a.cx == w.cx && a.cy == w.cy && w.cx > 0, detail);
    }

    // TextOutA vs TextOutW, pixel by pixel.
    sprintf_s(name, "GDI32_TEXTOUT_A_VS_W_%s", suffix);
    ProbeRender(name, c, d.acp, [&](bool ansi) {
        if (ansi) TextOutA(c.dc, 2, 2, d.mb, d.mbLen);
        else      TextOutW(c.dc, 2, 2, d.wide, d.wideChars);
    });

    // GetGlyphOutlineA (character code in the ACP) vs GetGlyphOutlineW.
    if (d.firstNonAscii >= 0)
    {
        char bytes[8];
        int bn = WideCharToMultiByte(d.acp, 0, &d.wide[d.firstNonAscii], 1, bytes, sizeof(bytes), NULL, NULL);
        UINT code = bn == 2 ? (((BYTE)bytes[0] << 8) | (BYTE)bytes[1]) : (BYTE)bytes[0];
        MAT2 identity = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
        GLYPHMETRICS a = {}, w = {};
        DWORD ra = GetGlyphOutlineA(c.dc, code, GGO_METRICS, &a, 0, NULL, &identity);
        DWORD rw = GetGlyphOutlineW(c.dc, d.wide[d.firstNonAscii], GGO_METRICS, &w, 0, NULL, &identity);
        bool ok = ra != GDI_ERROR && rw != GDI_ERROR && memcmp(&a, &w, sizeof(a)) == 0;
        sprintf_s(detail, "cp=%u char=%04X code=%X A=%ux%u@%ld,%ld W=%ux%u@%ld,%ld",
                  d.acp, d.wide[d.firstNonAscii], code,
                  a.gmBlackBoxX, a.gmBlackBoxY, a.gmptGlyphOrigin.x, a.gmptGlyphOrigin.y,
                  w.gmBlackBoxX, w.gmBlackBoxY, w.gmptGlyphOrigin.x, w.gmptGlyphOrigin.y);
        sprintf_s(name, "GDI32_GLYPHOUTLINE_A_VS_W_%s", suffix);
        Probe(name, ok, detail);
    }

    // DrawTextA vs DrawTextW (user32 on top of GDI), pixel by pixel.
    sprintf_s(name, "USER32_DRAWTEXT_A_VS_W_%s", suffix);
    ProbeRender(name, c, d.acp, [&](bool ansi) {
        RECT rc = { 2, 2, c.width - 2, c.height - 2 };
        UINT flags = DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX;
        if (ansi) DrawTextA(c.dc, d.mb, d.mbLen, &rc, flags);
        else      DrawTextW(c.dc, d.wide, d.wideChars, &rc, flags);
    });

    SelectObject(c.dc, oldFont);
    DeleteObject(font);
}

static void ProbeGdi(const ProbeData &d)
{
    Canvas c;
    if (!CreateCanvas(c, 1600, 48))
    {
        Info("GDI32", "skipped: cannot create canvas");
        return;
    }
    // Font charset of the emulated code page, and DEFAULT_CHARSET, which may be
    // resolved from the system code page instead of the emulated one.
    ProbeGdiFont("CS", (BYTE)ExpectedCharset(d.acp), c, d);
    ProbeGdiFont("DEFCS", DEFAULT_CHARSET, c, d);
    DestroyCanvas(c);
}

static void RunProbes()
{
    ProbeData d = {};
    d.acp = GetACP();
    d.wideChars = RepresentableSample(d.acp, d.wide, 128);
    d.mbLen = WideCharToMultiByte(d.acp, 0, d.wide, d.wideChars, d.mb, sizeof(d.mb) - 1, NULL, NULL);
    d.mb[d.mbLen] = 0;
    d.firstNonAscii = -1;
    for (int i = 0; i < d.wideChars; ++i)
        if (d.wide[i] >= 0x80) { d.firstNonAscii = i; break; }

    // The sample actually used, so the log shows whether it contains DBCS characters.
    {
        char hex[520];
        Hex(d.wide, d.wideChars * (int)sizeof(WCHAR), hex, sizeof(hex));
        Info("SAMPLE_UTF16LE", hex);
        Hex(d.mb, d.mbLen, hex, sizeof(hex));
        Info("SAMPLE_ACP_BYTES", hex);
    }

    ProbeKernelbase(d);
    ProbeUser32(d);
    ProbeCrts(d);
    ProbeGdi(d);
}

static void WriteOsVersion()
{
    typedef LONG (NTAPI *RtlGetVersion_t)(OSVERSIONINFOW *);
    auto getVersion = (RtlGetVersion_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW v = { sizeof(v) };
    if (getVersion && getVersion(&v) == 0)
        fprintf(g_Out, "INFO_OS_VERSION=%lu.%lu.%lu\n", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
}

int main(int argc, char* argv[])
{
    // 決定輸出位置：若有參數則寫入指定檔案，否則寫入同目錄下的 locale_result.txt
    FILE* out = NULL;
    if (argc > 1)
    {
        out = fopen(argv[1], "w");
    }
    else
    {
        // 預設寫入 exe 同目錄下的 locale_result.txt
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        // 找到最後的反斜線，替換檔名
        char* lastSlash = strrchr(path, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        strcat_s(path, MAX_PATH, "locale_result.txt");
        out = fopen(path, "w");
    }

    if (!out) return 1;
    g_Out = out;

    fprintf(out, "ACP=%u\n", GetACP());
    fprintf(out, "OEMCP=%u\n", GetOEMCP());
    fprintf(out, "LCID=0x%04X\n", GetUserDefaultLCID());
    fprintf(out, "UILANG=0x%04X\n", GetUserDefaultUILanguage());
    fprintf(out, "THREADLOCALE=0x%04X\n", GetThreadLocale());
    {
        TIME_ZONE_INFORMATION tzi = {};
        GetTimeZoneInformation(&tzi);
        fprintf(out, "TZBIAS=%ld\n", tzi.Bias);
    }

    // Set only by the timing-experiment builds of LocaleEmulator.dll (D7).
    {
        char marker[64];
        DWORD len = GetEnvironmentVariableA("LE_EXPERIMENT", marker, sizeof(marker));
        if (len > 0 && len < sizeof(marker))
            fprintf(out, "LE_EXPERIMENT=%s\n", marker);
    }

    WriteOsVersion();
    CheckConversions();
    RunProbes();

    fclose(out);
    return 0;
}
