#include "pch.h"
#include "IconSource.h"

CIconCache::~CIconCache() {
	if (!m_Images.IsNull())
		m_Images.Destroy();
}

void CIconCache::Reset(int size) {
	if (!m_Images.IsNull())
		m_Images.Destroy();
	m_Indices.clear();
	m_Size = size;
	m_Images.Create(size, size, ILC_COLOR32 | ILC_MASK, 8, 8);
}

int CIconCache::GetIconIndex(UINT iconId) const {
	if (auto it = m_Indices.find(iconId); it != m_Indices.end())
		return it->second;
	if (m_Images.IsNull())
		m_Images.Create(m_Size, m_Size, ILC_COLOR32 | ILC_MASK, 8, 8);
	int index = -1;
	if (auto icon = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(iconId), IMAGE_ICON, m_Size, m_Size, LR_DEFAULTCOLOR)) {
		index = m_Images.AddIcon(icon);
		::DestroyIcon(icon);
	}
	// a missing icon isn't looked for again
	m_Indices[iconId] = index;
	return index;
}
