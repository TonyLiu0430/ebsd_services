#pragma once
#include <string>
#include <string_view>

namespace ebsd {
// CommandLineToArgvW / MSVC argument quoting, including trailing backslashes.
inline std::wstring quote_argument(std::wstring_view argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
}
