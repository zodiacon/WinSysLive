#pragma once

#include <unordered_map>

//
// Where a list's icons come from: the frame's shared image list (IMainFrame), or, on a property sheet's thread (the
// frame's belongs to the UI thread), an image list of the sheet's own (CIconCache). Lists using one should have
// LVS_SHAREIMAGELISTS.
//
struct IIconSource abstract {
	virtual HIMAGELIST GetImageList() const = 0;
	// the icon's index in the image list, -1 if there's none
	virtual int GetIconIndex(UINT iconId) const = 0;
};

//
// An image list of its own: each of the app's icons is loaded the first time its index is asked for. Reset (at
// first, and after a DPI change) takes the icons' size and starts over, so the lists showing it need it set again.
//
class CIconCache : public IIconSource {
public:
	CIconCache() = default;
	CIconCache(CIconCache const&) = delete;
	CIconCache& operator=(CIconCache const&) = delete;
	~CIconCache();

	void Reset(int size);

	HIMAGELIST GetImageList() const override {
		return m_Images;
	}
	int GetIconIndex(UINT iconId) const override;

private:
	mutable CImageList m_Images;
	mutable std::unordered_map<UINT, int> m_Indices;
	int m_Size{ 16 };
};
