#include "pch.h"
#include "Signature.h"
#include <WinSys/Helpers.h>

std::wstring FormatSignature(WinSys::SignatureInfo const& info) {
	auto signer = info.Signer.empty() ? L"" : std::format(L": {}", info.Signer);
	auto source = info.Catalog ? L" (catalog)" : L"";
	using enum WinSys::SignatureStatus;
	switch (info.Status) {
		case Valid: return std::format(L"Valid{}{}", source, signer);
		case NotSigned: return L"Not signed";
		case Invalid:
			return std::format(L"Invalid{}{} ({})", source, signer, WinSys::Helpers::GetErrorText(info.Error));
	}
	return std::format(L"Can't read the file ({})", WinSys::Helpers::GetErrorText(info.Error));
}
