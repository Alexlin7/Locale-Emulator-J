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

        // Older CRTs (msvcr80 etc.) resolve setlocale(LC_ALL, "") through the ANSI
        // API with an explicit LCID, or with LOCALE_SYSTEM_DEFAULT. Under the
        // system UTF-8 option these may report 65001 instead of the locale's code page.
        const struct { LCID id; const char *check; } kAnsiQueries[] = {
            { lcid,                  "KERNELBASE_LOCALEINFO_A_LCID_ACP" },
            { LOCALE_SYSTEM_DEFAULT, "KERNELBASE_LOCALEINFO_A_SYSTEM_ACP" },
        };
        for (const auto &q : kAnsiQueries)
        {
            char valueA[32] = {};
            GetLocaleInfoA(q.id, LOCALE_IDEFAULTANSICODEPAGE, valueA, sizeof(valueA));
            sprintf_s(detail, "lcid=0x%04X expected=%u got=%s", q.id, acp, valueA);
            Check(q.check, (UINT)atoi(valueA) == acp, detail);
        }

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

    // kernelbase Unicode -> ANSI through CP_ACP / CP_THREAD_ACP. With the system
    // "Beta: Use Unicode UTF-8" option (ACP 65001) this direction can take a
    // UTF-8 path that ignores the emulated code page.
    {
        const struct { UINT cp; const char *check; } w2mb[] = {
            { CP_ACP,        "KERNELBASE_CP_ACP_W2MB" },
            { CP_THREAD_ACP, "KERNELBASE_CP_THREAD_ACP_W2MB" },
        };
        for (const auto &v : w2mb)
        {
            char got[512] = {};
            int n = WideCharToMultiByte(v.cp, 0, wide, wideChars, got, sizeof(got), NULL, NULL);
            bool ok = n == expectedMbLen && memcmp(got, expectedMb, n) == 0;
            Hex(got, n > 0 ? n : 0, hex, sizeof(hex));
            sprintf_s(detail, "cp=%u got=%s", acp, hex);
            Check(v.check, ok, detail);
        }
    }

    // DBCS lead-byte detection for the ANSI code page (engines walk ANSI text with it).
    {
        CPINFO info = {};
        GetCPInfo(acp, &info);
        bool isDbcs = info.MaxCharSize == 2;
        BYTE lead = isDbcs ? info.LeadByte[0] : 0;
        char pair[3] = { (char)lead, (char)0x40, 0 };
        BOOL isLead = isDbcs ? IsDBCSLeadByte(lead) : FALSE;
        BOOL isLeadEx = isDbcs ? IsDBCSLeadByteEx(CP_ACP, lead) : FALSE;
        int step = isDbcs ? (int)(CharNextA(pair) - pair) : 0;

        sprintf_s(detail, "cp=%u lead=0x%02X got=%d", acp, lead, isLead);
        Check("KERNELBASE_ISDBCSLEADBYTE", !isDbcs || isLead, detail);
        sprintf_s(detail, "cp=%u lead=0x%02X got=%d", acp, lead, isLeadEx);
        Check("KERNELBASE_ISDBCSLEADBYTEEX_CP_ACP", !isDbcs || isLeadEx, detail);
        sprintf_s(detail, "cp=%u lead=0x%02X step=%d", acp, lead, step);
        Check("USER32_CHARNEXTA_DBCS", !isDbcs || step == 2, detail);
    }

    // CRT conversions after setlocale(LC_ALL, "") (the user-default locale's ANSI code page).
    {
        char *prev = setlocale(LC_ALL, "");
        char got[512] = {};
        size_t n = wcstombs(got, wide, sizeof(got));
        bool ok = n == (size_t)expectedMbLen && memcmp(got, expectedMb, n) == 0;
        Hex(got, n != (size_t)-1 ? (int)n : 0, hex, sizeof(hex));
        sprintf_s(detail, "cp=%u locale=%s got=%s", acp, prev ? prev : "(null)", hex);
        Check("CRT_WCSTOMBS_USER_LOCALE", ok, detail);

        WCHAR back[256] = {};
        size_t m = mbstowcs(back, expectedMb, 256);
        ok = m == (size_t)wideChars && memcmp(back, wide, m * sizeof(WCHAR)) == 0;
        Hex(back, m != (size_t)-1 ? (int)(m * sizeof(WCHAR)) : 0, hex, sizeof(hex));
        sprintf_s(detail, "cp=%u got=%s", acp, hex);
        Check("CRT_MBSTOWCS_USER_LOCALE", ok, detail);

        setlocale(LC_ALL, "C");
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

    CheckConversions();

    fclose(out);
    return 0;
}
