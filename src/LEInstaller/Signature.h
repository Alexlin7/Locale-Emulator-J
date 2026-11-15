// src/LEInstaller/Signature.h
// Checks the Authenticode signature of a downloaded runtime installer before running it.
#pragma once

#include <filesystem>
#include <string>

namespace LeInstaller
{

// True when the file has a valid, trusted embedded Authenticode signature whose signing
// certificate's organization (O) is exactly "Microsoft Corporation". The certificate
// revocation check is skipped so that the result does not depend on reaching the CRL servers.
bool IsSignedByMicrosoft(const std::filesystem::path& file);

// The organization (O) of the signing certificate, or empty when there is no valid signature.
std::wstring GetSignerOrganization(const std::filesystem::path& file);

}  // namespace LeInstaller
