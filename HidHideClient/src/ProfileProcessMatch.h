// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>
#include <filesystem>
#include <vector>

namespace HidHide
{
    enum class ProfileProcessMatch { Different, Exact, Unresolved };

    // A snapshot filename is only a candidate filter, never executable identity.
    inline ProfileProcessMatch MatchProfileProcess(std::filesystem::path const& profilePath,
        std::filesystem::path const& processPath)
    {
        if (profilePath.empty() || processPath.empty()) return ProfileProcessMatch::Unresolved;
        return 0 == _wcsicmp(profilePath.c_str(), processPath.c_str())
            ? ProfileProcessMatch::Exact : ProfileProcessMatch::Different;
    }

    // Inject only the OS boundary so inaccessible processes can be tested without
    // requiring a protected process or changing driver configuration.
    template<class Open, class Query, class Close>
    std::filesystem::path ResolveProfileProcessPath(DWORD processId, Open open, Query query, Close close)
    {
        std::vector<WCHAR> path(32768);
        HANDLE const process = open(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (!process) return {};
        DWORD size = static_cast<DWORD>(path.size());
        bool const resolved = 0 != query(process, 0, path.data(), &size);
        close(process);
        if (!resolved || !size || size >= path.size()) return {};
        return std::wstring(path.data(), size);
    }
}
