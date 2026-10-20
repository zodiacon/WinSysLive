#pragma once

#include <WinSys/FileSignature.h>

// a signature as the UI shows it: "Valid (catalog): Microsoft Windows", "Not signed", "Invalid: ... (the error)"
std::wstring FormatSignature(WinSys::SignatureInfo const& info);
