// src/LEInstaller/Signature.cpp
#include "Signature.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

namespace LeInstaller
{

std::wstring GetSignerOrganization(const std::filesystem::path& file)
{
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = file.c_str();

    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &fileInfo;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_SAFER_FLAG;

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG trust = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);

    std::wstring organization;
    if (trust == ERROR_SUCCESS)
    {
        CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        if (signer && signer->csCertChain > 0 && signer->pasCertChain && signer->pasCertChain[0].pCert)
        {
            PCCERT_CONTEXT cert = signer->pasCertChain[0].pCert;
            char oid[] = szOID_ORGANIZATION_NAME;
            const DWORD len = CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, oid, nullptr, 0);
            if (len > 1)
            {
                organization.resize(len);
                CertGetNameStringW(cert, CERT_NAME_ATTR_TYPE, 0, oid, organization.data(), len);
                organization.resize(len - 1);  // drop the terminating null
            }
        }
    }

    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);

    return organization;
}

bool IsSignedByMicrosoft(const std::filesystem::path& file)
{
    return GetSignerOrganization(file) == L"Microsoft Corporation";
}

}  // namespace LeInstaller
