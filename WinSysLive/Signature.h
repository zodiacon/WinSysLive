#pragma once

//
// Authenticode verification of a file: its embedded signature, or, if it has none, a system catalog that lists its
// hash (most Windows binaries are catalog signed). No revocation checks, so nothing goes to the network.
//
struct SignatureInfo {
	enum class Status {
		Valid,		// signed and trusted
		NotSigned,	// no embedded signature and no catalog
		Invalid,	// signed, but the signature or the chain doesn't verify (Error)
		Error,		// the file couldn't be read (Error)
	};
	Status Status{ Status::Error };
	bool Catalog{ false };	// verified through a catalog
	std::wstring Signer;	// the signing certificate's subject
	DWORD Error{ 0 };

	std::wstring ToString() const;
};

SignatureInfo VerifySignature(PCWSTR path);
