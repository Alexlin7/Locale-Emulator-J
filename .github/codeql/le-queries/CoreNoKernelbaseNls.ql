/**
 * @name Core uses a kernel32/kernelbase code page or locale API
 * @description LocaleEmulator.dll is injected before kernel32 is loaded, and the
 *              ANSI/OEM conversions it emulates are exactly what kernelbase's
 *              code page and locale APIs depend on. Core must convert with
 *              ntdll's Rtl* routines and its own NLS tables (AGENTS.md rules in
 *              src/Core/AGENTS.md). The build already rejects static kernel32
 *              imports; this query also catches calls resolved at run time
 *              (KERNEL32_API, LookupExportTable with a KERNEL32_* hash, or a
 *              lookup by export name). A deliberate use is exempted by a
 *              comment containing `LE-ALLOW-KERNELBASE: <reason>` on the same
 *              line or the line directly above.
 * @kind problem
 * @problem.severity error
 * @precision high
 * @id le/core/kernelbase-nls-call
 * @tags correctness
 *       locale-emulator
 */

import cpp

/** Code page, locale and time zone APIs exported by kernel32/kernelbase. */
predicate forbiddenApi(string name) {
  name =
    [
      // Code page conversion and queries
      "MultiByteToWideChar", "WideCharToMultiByte", "GetACP", "GetOEMCP", "GetCPInfo",
      "GetCPInfoExA", "GetCPInfoExW", "IsDBCSLeadByte", "IsDBCSLeadByteEx", "IsValidCodePage",
      // Locale
      "GetLocaleInfoA", "GetLocaleInfoW", "GetLocaleInfoEx", "GetUserDefaultLCID",
      "GetSystemDefaultLCID", "GetUserDefaultLangID", "GetSystemDefaultLangID",
      "GetUserDefaultUILanguage", "GetSystemDefaultUILanguage", "GetUserDefaultLocaleName",
      "GetSystemDefaultLocaleName", "GetThreadLocale", "SetThreadLocale", "LCIDToLocaleName",
      "LocaleNameToLCID", "CompareStringA", "CompareStringW", "CompareStringEx", "LCMapStringA",
      "LCMapStringW", "LCMapStringEx",
      // Time zone
      "GetTimeZoneInformation", "GetDynamicTimeZoneInformation"
    ]
}

/** Source files the rule applies to: Core, excluding the upstream ml library. */
predicate inCoreScope(Location loc) {
  loc.getFile().getAbsolutePath().regexpMatch("(?i).*[/\\\\]core[/\\\\].*") and
  not loc.getFile().getBaseName().toLowerCase() = ["ml.h", "ml.cpp"]
}

/** A Windows API function: declared, but not defined in the analyzed sources. */
predicate isExternalApi(Function f, string name) {
  f.hasGlobalName(name) and
  forbiddenApi(name) and
  not exists(Function def | def = f and def.hasDefinition() and def.getFile().fromSource())
}

/**
 * Holds if `e` uses the forbidden API `name`, described by `how`.
 * Macro-generated code is reported at the outermost macro invocation, so the
 * alert points at the line the developer wrote.
 */
predicate violation(Locatable e, string name, string how) {
  // Direct call written in the source (not produced by a macro).
  exists(FunctionCall c |
    e = c and
    isExternalApi(c.getTarget(), name) and
    not c.isInMacroExpansion() and
    how = "calls"
  )
  or
  // Run-time lookup through a macro: KERNEL32_API(Name) or a KERNEL32_Name hash constant.
  exists(MacroInvocation mi |
    e = mi and
    not exists(mi.getParentInvocation()) and
    (
      mi.getMacroName() = "KERNEL32_API" and name = mi.getUnexpandedArgument(0).trim()
      or
      mi.getMacroName() = "KERNEL32_" + name
    ) and
    forbiddenApi(name) and
    how = "looks up"
  )
  or
  // Run-time lookup by export name passed to a lookup routine.
  exists(FunctionCall c, StringLiteral s |
    e = c and
    s = c.getAnArgument() and
    name = s.getValue() and
    forbiddenApi(name) and
    c.getTarget().getName().regexpMatch("(?i).*(GetProcAddress|GetRoutineAddress|LookupExport|LookupRoutine).*") and
    how = "looks up"
  )
}

/**
 * Holds if `e` carries a reviewed exemption: a comment containing
 * `LE-ALLOW-KERNELBASE` on the same line or on the line directly above.
 */
predicate exempted(Locatable e) {
  exists(Comment c, Location cl, Location el |
    cl = c.getLocation() and
    el = e.getLocation() and
    c.getContents().matches("%LE-ALLOW-KERNELBASE%") and
    cl.getFile() = el.getFile() and
    cl.getStartLine() = [el.getStartLine(), el.getStartLine() - 1]
  )
}

from Locatable e, string name, string how
where
  violation(e, name, how) and
  inCoreScope(e.getLocation()) and
  not exempted(e)
select e,
  "Core " + how + " the kernel32/kernelbase API '" + name +
    "'. Use ntdll (RtlCustomCPToUnicodeN / RtlUnicodeToCustomCPN with LE's own NLS tables) instead."
