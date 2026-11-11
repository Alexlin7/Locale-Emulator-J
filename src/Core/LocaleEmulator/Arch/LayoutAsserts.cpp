// Compile-time checks that ml.h's NT structure definitions match the offsets in
// LeArch.h for the architecture being built. This file generates no code.

#include "ml.h"
#include "Arch\LeArch.h"

static_assert(FIELD_OFFSET(TEB_BASE, NtTib.Self)              == LE_TEB_SELF_OFFSET,    "TEB_BASE.NtTib.Self");
static_assert(FIELD_OFFSET(TEB_BASE, ProcessEnvironmentBlock) == LE_TEB_PEB_OFFSET,     "TEB_BASE.ProcessEnvironmentBlock");
static_assert(FIELD_OFFSET(PEB_BASE, AnsiCodePageData)        == LE_PEB_ANSI_CP_OFFSET, "PEB_BASE.AnsiCodePageData");
static_assert(FIELD_OFFSET(PEB_BASE, OemCodePageData)         == LE_PEB_OEM_CP_OFFSET,  "PEB_BASE.OemCodePageData");
static_assert(FIELD_OFFSET(PEB_BASE, UnicodeCaseTableData)    == LE_PEB_CASE_OFFSET,    "PEB_BASE.UnicodeCaseTableData");

#if defined(_M_IX86)
static_assert(LE_PEB32_ANSI_CP_OFFSET == LE_PEB_ANSI_CP_OFFSET &&
              LE_PEB32_OEM_CP_OFFSET  == LE_PEB_OEM_CP_OFFSET  &&
              LE_PEB32_CASE_OFFSET    == LE_PEB_CASE_OFFSET,    "PEB32 constants");
#elif defined(_M_X64)
static_assert(LE_PEB64_ANSI_CP_OFFSET == LE_PEB_ANSI_CP_OFFSET &&
              LE_PEB64_OEM_CP_OFFSET  == LE_PEB_OEM_CP_OFFSET  &&
              LE_PEB64_CASE_OFFSET    == LE_PEB_CASE_OFFSET,    "PEB64 constants");
#endif
