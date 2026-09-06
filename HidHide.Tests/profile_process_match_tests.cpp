// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include "../HidHideClient/src/ProfileProcessMatch.h"

using HidHide::MatchProfileProcess;
using HidHide::ProfileProcessMatch;

TEST(ProfileProcessMatch, SameBasenameInDifferentDirectoryNeverMatches)
{
    EXPECT_EQ(ProfileProcessMatch::Different,
        MatchProfileProcess(L"C:\\Games\\game.exe", L"D:\\Other\\game.exe"));
    EXPECT_EQ(ProfileProcessMatch::Exact,
        MatchProfileProcess(L"D:\\Other\\game.exe", L"D:\\Other\\game.exe"));
}

TEST(ProfileProcessMatch, ExactSupportedPathIsCaseInsensitive)
{
    EXPECT_EQ(ProfileProcessMatch::Exact,
        MatchProfileProcess(L"C:\\Games\\Game.exe", L"c:\\GAMES\\GAME.EXE"));
}

TEST(ProfileProcessMatch, UnresolvedProcessCannotActivateEvenOneCandidate)
{
    EXPECT_EQ(ProfileProcessMatch::Unresolved, MatchProfileProcess(L"C:\\Games\\game.exe", {}));
    EXPECT_EQ(ProfileProcessMatch::Unresolved, MatchProfileProcess(L"D:\\Other\\game.exe", {}));
}

TEST(ProfileProcessMatch, FailedProfilePathConversionCannotMatch)
{
    EXPECT_EQ(ProfileProcessMatch::Unresolved, MatchProfileProcess({}, L"C:\\Games\\game.exe"));
    EXPECT_EQ(ProfileProcessMatch::Unresolved, MatchProfileProcess({}, {}));
}

TEST(ProfileProcessMatch, OpenProcessDeniedCannotActivate)
{
    int queries{}, closes{};
    auto const path = HidHide::ResolveProfileProcessPath(123,
        [](DWORD access, BOOL inherit, DWORD pid) -> HANDLE {
            EXPECT_EQ(static_cast<DWORD>(PROCESS_QUERY_LIMITED_INFORMATION), access);
            EXPECT_FALSE(inherit);
            EXPECT_EQ(123u, pid);
            return nullptr;
        },
        [&](HANDLE, DWORD, WCHAR*, DWORD*) -> BOOL { ++queries; return TRUE; },
        [&](HANDLE) { ++closes; });
    EXPECT_EQ(0, queries);
    EXPECT_EQ(0, closes);
    EXPECT_EQ(ProfileProcessMatch::Unresolved, MatchProfileProcess(L"C:\\Games\\game.exe", path));
}

TEST(ProfileProcessMatch, FailedPathQueryCannotActivateAndClosesHandle)
{
    int closes{};
    HANDLE const handle = reinterpret_cast<HANDLE>(1);
    auto const path = HidHide::ResolveProfileProcessPath(123,
        [&](DWORD, BOOL, DWORD) { return handle; },
        [&](HANDLE received, DWORD flags, WCHAR* buffer, DWORD* size) -> BOOL {
            EXPECT_EQ(handle, received);
            EXPECT_EQ(0u, flags);
            // Even a partially written buffer must not establish identity.
            wcscpy_s(buffer, *size, L"C:\\Games\\game.exe");
            return FALSE;
        },
        [&](HANDLE received) { EXPECT_EQ(handle, received); ++closes; });
    EXPECT_EQ(1, closes);
    EXPECT_EQ(ProfileProcessMatch::Unresolved, MatchProfileProcess(L"C:\\Games\\game.exe", path));
}

TEST(ProfileProcessMatch, SuccessfulQueryActivatesOnlyTheExactCandidate)
{
    int closes{};
    auto const path = HidHide::ResolveProfileProcessPath(123,
        [](DWORD, BOOL, DWORD) { return reinterpret_cast<HANDLE>(1); },
        [](HANDLE, DWORD, WCHAR* buffer, DWORD* size) -> BOOL {
            std::wstring const image = L"C:\\Games\\GAME.exe";
            wcscpy_s(buffer, *size, image.c_str());
            *size = static_cast<DWORD>(image.size());
            return TRUE;
        },
        [&](HANDLE) { ++closes; });
    EXPECT_EQ(1, closes);
    EXPECT_EQ(ProfileProcessMatch::Exact, MatchProfileProcess(L"c:\\games\\game.exe", path));
    EXPECT_EQ(ProfileProcessMatch::Different, MatchProfileProcess(L"D:\\Other\\game.exe", path));
}
