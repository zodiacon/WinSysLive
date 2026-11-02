#pragma once

#include <string>

namespace XmlText {
	// XML as lines: one per element, indented two spaces per depth, an element's text kept on its line
	// (<Data Name='x'>value</Data>); declarations and comments stay as they are. The Event Details dialog's XML and the
	// Image views' manifest
	inline std::wstring Indent(std::wstring const& xml) {
		std::wstring result;
		int depth = 0;
		size_t i = 0;
		// the whitespace between tags is the source's layout: it's redone
		auto skipSpace = [&] {
			while (i < xml.size() && (xml[i] == L' ' || xml[i] == L'\t' || xml[i] == L'\r' || xml[i] == L'\n'))
				i++;
		};
		while (i < xml.size()) {
			if (xml[i] != L'<') {
				// text: up to the next tag (whitespace only between tags is dropped)
				auto next = xml.find(L'<', i);
				auto text = xml.substr(i, next == std::wstring::npos ? std::wstring::npos : next - i);
				if (text.find_first_not_of(L" \t\r\n") != std::wstring::npos)
					result += text;
				i = next == std::wstring::npos ? xml.size() : next;
				continue;
			}
			auto end = xml.find(L'>', i);
			if (end == std::wstring::npos) {
				result.append(xml, i);
				break;
			}
			auto tag = xml.substr(i, end - i + 1);
			bool closing = tag.starts_with(L"</");
			bool selfClosing = tag.ends_with(L"/>") || tag.starts_with(L"<?") || tag.starts_with(L"<!");
			// a closing tag right after its text stays on the text's line
			bool afterText = !result.empty() && result.back() != L'>' && result.back() != L'\n';
			if (closing)
				depth--;
			if (!(closing && afterText)) {
				if (!result.empty())
					result += L'\n';
				result.append((size_t)(depth > 0 ? depth : 0) * 2, L' ');
			}
			result += tag;
			if (!closing && !selfClosing)
				depth++;
			i = end + 1;
			skipSpace();
		}
		return result;
	}
}
