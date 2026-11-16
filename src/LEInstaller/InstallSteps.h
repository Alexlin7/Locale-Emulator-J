// src/LEInstaller/InstallSteps.h
// Order of the runtime installs, kept apart from the dialogs so it can be unit tested.
#pragma once

#include <functional>

#include "RuntimeCheck.h"

namespace LeInstaller
{

enum class StepResult
{
    Installed,
    Cancelled,  // download cancelled, UAC prompt declined or the runtime installer cancelled
    Failed      // an error was already shown
};

// Installs the missing runtimes, x86 first. Stops at the first step that does not end with
// Installed: after a cancel the user is asked again instead of being taken to the next
// download and UAC prompt.
inline StepResult InstallMissingRuntimes(const RuntimeStatus& status,
                                         const std::function<StepResult(Arch)>& install)
{
    if (!status.hasX86)
    {
        const StepResult result = install(Arch::X86);
        if (result != StepResult::Installed)
            return result;
    }
    if (status.NeedsX64())
        return install(Arch::X64);
    return StepResult::Installed;
}

}  // namespace LeInstaller
