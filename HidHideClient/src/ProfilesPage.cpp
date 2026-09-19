// SPDX-License-Identifier: MIT
#include "stdafx.h"
#include "ProfilesPage.h"
#include "HID.h"
#include "ProfileRepository.h"
#include <ShlObj.h>
#include <shellapi.h>
#include <oleacc.h>
#include <dlgs.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <cmath>
#pragma comment(lib, "oleacc.lib")
#pragma comment(lib, "uuid.lib")

IMPLEMENT_DYNAMIC(CProfilesPage, CDialogEx)

BEGIN_MESSAGE_MAP(CProfilesPage, CDialogEx)
    ON_LBN_SELCHANGE(IDC_PROFILE_LIST, &CProfilesPage::OnProfileSelection)
    ON_EN_CHANGE(IDC_PROFILE_SEARCH, &CProfilesPage::OnSearchChanged)
    ON_EN_CHANGE(IDC_PROFILE_NAME, &CProfilesPage::OnDraftChanged)
    ON_EN_CHANGE(IDC_PROFILE_PRIORITY, &CProfilesPage::OnDraftChanged)
    ON_BN_CLICKED(IDC_PROFILE_ENABLED, &CProfilesPage::OnDraftChanged)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_PROFILE_DEVICES, &CProfilesPage::OnDeviceSelection)
    ON_BN_CLICKED(IDC_PROFILE_HIDDEN, &CProfilesPage::OnHiddenVisibility)
    ON_BN_CLICKED(IDC_PROFILE_VISIBLE, &CProfilesPage::OnVisibleVisibility)
    ON_BN_CLICKED(IDC_PROFILE_NEW_APP, &CProfilesPage::OnNewApplication)
    ON_BN_CLICKED(IDC_PROFILE_NEW_GLOBAL, &CProfilesPage::OnNewGlobal)
    ON_BN_CLICKED(IDC_PROFILE_CHANGE_APP, &CProfilesPage::OnChangeApplication)
    ON_BN_CLICKED(IDC_PROFILE_APPLY, &CProfilesPage::OnApply)
    ON_BN_CLICKED(IDC_PROFILE_DISCARD, &CProfilesPage::OnDiscard)
    ON_BN_CLICKED(IDC_PROFILE_RETRY, &CProfilesPage::OnRetry)
    ON_CBN_SELCHANGE(IDC_PROFILE_MODE, &CProfilesPage::OnModeChanged)
    ON_CBN_SELCHANGE(IDC_PROFILE_GLOBAL, &CProfilesPage::OnGlobalChanged)
    ON_BN_CLICKED(IDC_PROFILE_PAUSE, &CProfilesPage::OnPause)
    ON_BN_CLICKED(IDC_PROFILE_ALLOWED_APPS, &CProfilesPage::OnAllowedApps)
    ON_BN_CLICKED(IDC_PROFILE_SETTINGS, &CProfilesPage::OnSettings)
    ON_BN_CLICKED(IDC_PROFILE_OPEN_FOLDER, &CProfilesPage::OnOpenFolder)
    ON_BN_CLICKED(IDC_PROFILE_IMPORT, &CProfilesPage::OnImport)
    ON_BN_CLICKED(IDC_PROFILE_BACKUP, &CProfilesPage::OnBackup)
    ON_BN_CLICKED(IDC_PROFILE_DELETE, &CProfilesPage::OnDelete)
    ON_BN_CLICKED(IDC_PROFILE_EXPORT, &CProfilesPage::OnExport)
    ON_BN_CLICKED(IDC_PROFILE_OPEN_JSON, &CProfilesPage::OnOpenJson)
    ON_BN_CLICKED(IDC_PROFILE_NEW_MENU, &CProfilesPage::OnNewMenu)
    ON_BN_CLICKED(IDC_PROFILE_MENU, &CProfilesPage::OnProfileMenu)
    ON_BN_CLICKED(IDC_PROFILE_AUTOMATIC, &CProfilesPage::OnAutomatic)
    ON_BN_CLICKED(IDC_PROFILE_USE_GLOBAL, &CProfilesPage::OnUseGlobal)
    ON_BN_CLICKED(IDC_PROFILE_VIEW_PATH, &CProfilesPage::OnViewPath)
    ON_BN_CLICKED(IDC_PROFILE_ADD_REMEMBERED, &CProfilesPage::OnAddRemembered)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_WM_SYSCOLORCHANGE()
    ON_MESSAGE(ProfilesView::ThemeChangedMessage, &CProfilesPage::OnThemeChanged)
    ON_MESSAGE(WM_DPICHANGED, &CProfilesPage::OnDpiChanged)
    ON_WM_SIZE()
END_MESSAGE_MAP()

std::vector<ProfilesDeviceItem> CProductionProfilesDeviceSource::Enumerate()
{
    std::lock_guard<std::mutex> lock(m_Mutex); if (!m_Known) throw std::runtime_error("Device enumeration is unavailable"); return m_Cached;
}

void CProductionProfilesDeviceSource::Refresh()
{
    try
    {
        auto result = HidHide::Profiles::ProjectProfileDeviceGroups(HidHide::HidDevices(false));
        std::lock_guard<std::mutex> lock(m_Mutex); m_Cached = std::move(result); m_Known = true;
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(m_Mutex); m_Cached.clear(); m_Known = false; throw;
    }
}

CProfilesPage::CProfilesPage(HidHide::Profiles::ProfileApplicationService& application, CProfilesCoordinator& coordinator, IProfilesDeviceSource& devices, CWnd* parent)
    : CDialogEx(IDD_DIALOG_PROFILES_FIRST, parent), m_Coordinator(coordinator), m_Application(application), m_DeviceSource(devices) {}

bool CProfilesPage::AcceptanceStageProfile()
{
    // The startup repository contains the required Global fallback. Its
    // application-only controls must stay unavailable even in writable mode.
    if (!m_Draft || m_Draft->kind != HidHide::Profiles::Kind::Global
        || m_ChangeApp.IsWindowEnabled() || m_Priority.IsWindowEnabled()) return false;
    auto executable = m_Application.Root() / L"F1_25.exe";
    HANDLE file = ::CreateFileW(executable.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false; ::CloseHandle(file);
    std::atomic_bool pickerCompleted{};
    std::thread picker([&]
    {
        struct Search { DWORD process{}; HWND dialog{}; } search{ ::GetCurrentProcessId() };
        auto findDialog = [](HWND window, LPARAM value)
        {
            auto& state = *reinterpret_cast<Search*>(value); DWORD process{}; ::GetWindowThreadProcessId(window, &process); wchar_t name[32]{}; ::GetClassNameW(window, name, static_cast<int>(std::size(name)));
            if (process == state.process && 0 == wcscmp(name, L"#32770") && ::IsWindowVisible(window)) { state.dialog = window; return FALSE; } return TRUE;
        };
        for (int attempt{}; attempt < 500 && !search.dialog; ++attempt) { ::EnumWindows(findDialog, reinterpret_cast<LPARAM>(&search)); if (!search.dialog) ::Sleep(20); }
        if (!search.dialog) return;
        auto edit = ::GetDlgItem(search.dialog, edt1);
        if (!edit)
        {
            ::EnumChildWindows(search.dialog, [](HWND child, LPARAM value)
            {
                wchar_t name[32]{}; ::GetClassNameW(child, name, static_cast<int>(std::size(name)));
                if (0 == _wcsicmp(name, L"Edit") && ::IsWindowEnabled(child)) { *reinterpret_cast<HWND*>(value) = child; return FALSE; } return TRUE;
            }, reinterpret_cast<LPARAM>(&edit));
        }
        if (!edit) { ::PostMessageW(search.dialog, WM_COMMAND, IDCANCEL, 0); return; }
        ::SetWindowTextW(edit, executable.c_str()); ::PostMessageW(search.dialog, WM_COMMAND, IDOK, 0); pickerCompleted = true;
    });
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_NEW_APP, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(IDC_PROFILE_NEW_APP)->GetSafeHwnd())); picker.join();
    if (!pickerCompleted || !m_Draft || m_Draft->executable != HidHide::Profiles::NormalizeExecutable(executable)
        || !m_ChangeApp.IsWindowEnabled() || !m_Priority.IsWindowEnabled()) return false;
    m_Name.SetWindowTextW(L"F1 25"); m_Priority.SetWindowTextW(L"-1");
    if (m_DeviceRows.size() != 2) return false;
    for (int index{}; index < 2; ++index)
    {
        m_Devices.SetItemState(index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        m_Hidden.SendMessageW(BM_CLICK);
    }
    CString status; m_Result.GetWindowTextW(status);
    return m_Draft && m_Draft->rules.size() == 2 && Dirty() && status.Find(L"3 changes ready to Apply") >= 0;
}

bool CProfilesPage::AcceptanceApply()
{
    if (!GetSafeHwnd()) return false;
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_APPLY, BN_CLICKED), reinterpret_cast<LPARAM>(m_Apply.GetSafeHwnd()));
    return std::any_of(m_Coordinator.Snapshot().profiles.begin(), m_Coordinator.Snapshot().profiles.end(), [](auto const& item)
        { return item.second.kind == HidHide::Profiles::Kind::Application && item.second.executable.filename() == L"f1_25.exe"; }) && !Dirty();
}

bool CProfilesPage::AcceptanceLoaded()
{
    if (!m_Coordinator.AcceptanceScanNow()) return false;
    auto found = std::find_if(m_Coordinator.Snapshot().profiles.begin(), m_Coordinator.Snapshot().profiles.end(), [](auto const& item)
    { return item.second.kind == HidHide::Profiles::Kind::Application && item.second.executable.filename() == L"f1_25.exe"; });
    if (found == m_Coordinator.Snapshot().profiles.end()) return false; RefreshProfiles(found->first);
    CString name, path; m_Name.GetWindowTextW(name); m_Path.GetWindowTextW(path);
    return found->second.revision == 1 && found->second.rules.size() == 2 && m_Saved && m_Draft
        && m_Saved->id == found->first && m_Draft->id == found->first && std::wstring(name) == L"F1 25"
        && std::wstring(path) == HidHide::Profiles::NormalizeExecutable(m_Application.Root() / L"F1_25.exe").native()
        && m_DeviceRows.size() == 2 && m_Coordinator.RepositoryWriteCount() == 0;
}

bool CProfilesPage::AcceptanceRepositoryBlocked() const
{
    if (m_Coordinator.Issues().empty() || m_RepositoryWritable || m_Apply.IsWindowEnabled()
        || m_Coordinator.RepositoryWriteCount() != 0) return false;
    CString result; m_Result.GetWindowTextW(result);
    return result.Find(L"preserved") >= 0 && result.Find(L"Apply is blocked") >= 0;
}

bool CProfilesPage::AcceptanceBlockedCommandsSafe()
{
    if (m_RepositoryWritable) return false; auto writes = m_Coordinator.RepositoryWriteCount();
    for (auto id : { IDC_PROFILE_NEW_APP, IDC_PROFILE_NEW_GLOBAL, IDC_PROFILE_IMPORT, IDC_PROFILE_ALLOWED_APPS,
        IDC_PROFILE_CHANGE_APP, IDC_PROFILE_DELETE, IDC_PROFILE_APPLY })
        SendMessageW(WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(id)->GetSafeHwnd()));
    return !m_RepositoryWritable && writes == m_Coordinator.RepositoryWriteCount();
}

bool CProfilesPage::AcceptanceExerciseDraftOnlyEvents()
{
    auto writes = m_Coordinator.RepositoryWriteCount();
    DevicesChanged(); StagePause(true);
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_DISCARD, BN_CLICKED), reinterpret_cast<LPARAM>(m_Discard.GetSafeHwnd()));
    return !Dirty() && m_Coordinator.RepositoryWriteCount() == writes;
}

bool CProfilesPage::AcceptanceRestoreBackup(std::filesystem::path const& source)
{
    return RestoreBackup(source) && m_Coordinator.Issues().empty() && !m_Coordinator.HasDriverConflict() && !m_Coordinator.HasRepositoryDiagnostics()
        && m_RepositoryWritable && !m_Apply.IsWindowEnabled();
}

bool CProfilesPage::AcceptanceDirtyPromptSemantics()
{
    if (!m_Saved) return false; auto id = m_Saved->id; auto before = m_Application.Version(id); auto originalName = m_Saved->name;
    auto answer = [&](int button, auto action)
    {
        std::atomic_bool clicked{};
        std::thread automation([&]
        {
            struct Search { DWORD process{}; HWND dialog{}; } search{ ::GetCurrentProcessId() };
            auto find = [](HWND window, LPARAM value)
            {
                auto& state = *reinterpret_cast<Search*>(value); DWORD process{}; ::GetWindowThreadProcessId(window, &process); wchar_t name[32]{};
                ::GetClassNameW(window, name, static_cast<int>(std::size(name)));
                if (process == state.process && 0 == wcscmp(name, L"#32770") && ::IsWindowVisible(window)) { state.dialog = window; return FALSE; } return TRUE;
            };
            for (int attempt{}; attempt < 500 && !search.dialog; ++attempt) { ::EnumWindows(find, reinterpret_cast<LPARAM>(&search)); if (!search.dialog) ::Sleep(20); }
            if (search.dialog) { ::PostMessageW(search.dialog, WM_COMMAND, button, 0); clicked = true; }
        });
        action(); automation.join(); return clicked.load();
    };
    m_Name.SetWindowTextW(L"Dirty cancel"); if (!Dirty()) return false;
    if (!answer(IDCANCEL, [&] { GetParent()->SendMessageW(WM_CLOSE); }) || !Dirty()) return false;
    if (m_Application.Version(id).sha256 != before.sha256) return false;
    if (!answer(IDNO, [&] { GetParent()->SendMessageW(WM_COMMAND, IDCANCEL); }) || Dirty()) return false;
    CString restoredName; m_Name.GetWindowTextW(restoredName); if (std::wstring(restoredName) != originalName || m_Application.Version(id).sha256 != before.sha256) return false;
    m_Name.SetWindowTextW(L"Applied by prompt"); if (!answer(IDYES, [&] { GetParent()->SendMessageW(WM_COMMAND, IDOK); }) || Dirty()) return false;
    auto after = m_Application.Version(id); if (after.revision != before.revision + 1 || after.sha256 == before.sha256) return false;

    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_NEW_GLOBAL, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(IDC_PROFILE_NEW_GLOBAL)->GetSafeHwnd()));
    if (!m_Draft || m_Saved) return false; m_Name.SetWindowTextW(L"Old needle"); if (!ApplyDraft() || Dirty()) return false; auto secondId = m_Draft->id;
    m_Search.SetWindowTextW(L"Old"); m_Name.SetWindowTextW(L"New needle");
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_APPLY, BN_CLICKED), reinterpret_cast<LPARAM>(m_Apply.GetSafeHwnd()));
    CString search; m_Search.GetWindowTextW(search); if (Dirty() || m_Apply.IsWindowEnabled() || !search.IsEmpty()) return false;
    auto savedSecond = HidHide::Profiles::ParseProfile(HidHide::Profiles::ReadBytes(m_Application.Root() / (secondId + L".json"))); if (savedSecond.name != L"New needle") return false;

    m_Name.SetWindowTextW(L"Discard with empty filter"); m_Search.SetWindowTextW(L"matches nothing");
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_DISCARD, BN_CLICKED), reinterpret_cast<LPARAM>(m_Discard.GetSafeHwnd()));
    if (Dirty() || m_Apply.IsWindowEnabled() || !m_Draft || m_Draft->name != L"New needle") return false;

    m_Name.SetWindowTextW(L"Cancel draft must survive exactly"); auto target = std::find(m_ProfileIds.begin(), m_ProfileIds.end(), id);
    if (target == m_ProfileIds.end()) return false; m_Profiles.SetCurSel(static_cast<int>(std::distance(m_ProfileIds.begin(), target)));
    if (!answer(IDCANCEL, [&] { SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_LIST, LBN_SELCHANGE), reinterpret_cast<LPARAM>(m_Profiles.GetSafeHwnd())); })) return false;
    CString cancelledName; m_Name.GetWindowTextW(cancelledName); if (!Dirty() || !m_Draft || m_Draft->id != secondId || std::wstring(cancelledName) != L"Cancel draft must survive exactly" || SelectedId() != secondId) return false;
    OnDiscard(); m_Name.SetWindowTextW(L"Saved before navigating"); target = std::find(m_ProfileIds.begin(), m_ProfileIds.end(), id);
    m_Profiles.SetCurSel(static_cast<int>(std::distance(m_ProfileIds.begin(), target)));
    if (!answer(IDYES, [&] { SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_LIST, LBN_SELCHANGE), reinterpret_cast<LPARAM>(m_Profiles.GetSafeHwnd())); })) return false;
    return !Dirty() && m_Draft && m_Draft->id == id && SelectedId() == id
        && HidHide::Profiles::ParseProfile(HidHide::Profiles::ReadBytes(m_Application.Root() / (secondId + L".json"))).name == L"Saved before navigating";
}

bool CProfilesPage::AcceptanceObservationKnown(bool expectedKnown, bool expectedVerified)
{
    if (expectedVerified && !m_Coordinator.AcceptanceScanNow()) return false;
    RefreshDevices(); RefreshStatus(); if (m_DeviceRows.empty()) return false;
    for (int row{}; row < m_Devices.GetItemCount(); ++row)
    {
        auto text = std::wstring(m_Devices.GetItemText(row, 2));
        if ((text != L"Unknown") != expectedKnown) return false;
    }
    CString effective; m_Effective.GetWindowTextW(effective);
    return (effective.Find(L"is active") >= 0 || effective.Find(L"hiding paused") >= 0) == expectedVerified
        && (effective.Find(L"current state unknown or conflicting") >= 0 || effective.Find(L"Effective profile unknown") >= 0) != expectedVerified;
}

bool CProfilesPage::AcceptanceObservedPresentationAlreadyUpdated(bool expectedKnown) const
{
    if (m_DeviceRows.empty()) return false;
    for (int row{}; row < m_Devices.GetItemCount(); ++row)
        if ((std::wstring(m_Devices.GetItemText(row, 2)) != L"Unknown") != expectedKnown) return false;
    CString effective; m_Effective.GetWindowTextW(effective); bool rowUnconfirmed{};
    for (int row{}; row < m_Profiles.GetCount(); ++row)
    {
        CString text; m_Profiles.GetText(row, text);
        if (text.Find(L"Saved selection; current state unconfirmed") >= 0) rowUnconfirmed = true;
    }
    return effective.Find(L"current state unknown or conflicting") >= 0 && rowUnconfirmed;
}

void CProfilesPage::AcceptanceReloadSelectedProfile()
{
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_LIST, LBN_SELCHANGE), reinterpret_cast<LPARAM>(m_Profiles.GetSafeHwnd()));
}

bool CProfilesPage::AcceptanceDetachedDraftHasNoRepositoryConflict() const
{
    CString result; m_Result.GetWindowTextW(result); return Dirty() && m_Draft && m_Draft->name == L"Detached while hidden"
        && result.Find(L"Files changed outside this window") < 0;
}

bool CProfilesPage::AcceptanceStageDetachedStatusDraft()
{
    if (!m_Draft) return false; m_Name.SetWindowTextW(L"Detached while hidden"); return Dirty() && m_Draft->name == L"Detached while hidden";
}

bool CProfilesPage::AcceptanceStartupFailure()
{
    if (m_DeviceRows.empty()) return false; m_Devices.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    m_Hidden.SendMessageW(BM_CLICK); if (!ApplyDraft() || Dirty()) return false;
    CString result; m_Result.GetWindowTextW(result);
    auto observed = m_Coordinator.ObserveEnforcement();
    return observed.observedKnown && observed.observed.hidingEnabled
        && observed.observed.hiddenDevices.count(L"HID\\VID_1234&PID_0001\\CONNECTED") != 0
        && result.Find(L"Startup integration failed") >= 0;
}

bool CProfilesPage::AcceptanceSavedEnforcementFailure()
{
    if (!m_Draft || m_DeviceRows.empty()) return false; m_Devices.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    m_Hidden.SendMessageW(BM_CLICK); auto id = m_Draft->id;
    if (!ApplyDraft() || Dirty()) return false; CString result; m_Result.GetWindowTextW(result);
    auto saved = HidHide::Profiles::ParseProfile(HidHide::Profiles::ReadBytes(m_Application.Root() / (id + L".json")));
    return std::any_of(saved.rules.begin(), saved.rules.end(), [](auto const& rule) { return _wcsicmp(rule.identity.c_str(), L"HID\\VID_1234&PID_0001\\CONNECTED") == 0; })
        && result.Find(L"saved") >= 0 && result.Find(L"unknown") >= 0 && result.Find(L"Saved and applied") < 0;
}

bool CProfilesPage::AcceptanceRetryActivation()
{
    if (!m_Retry.IsWindowEnabled() || !m_Saved) return false; auto before = m_Coordinator.RepositoryWriteCount(); auto version = m_Application.Version(m_Saved->id);
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_RETRY, BN_CLICKED), reinterpret_cast<LPARAM>(m_Retry.GetSafeHwnd()));
    auto after = m_Application.Version(m_Saved->id); auto observed = m_Coordinator.ObserveEnforcement();
    return !m_Retry.IsWindowEnabled() && before == m_Coordinator.RepositoryWriteCount()
        && version.revision == after.revision && version.sha256 == after.sha256 && observed.success && observed.observedKnown
        && observed.observed.hiddenDevices.count(L"HID\\VID_1234&PID_0001\\CONNECTED") != 0;
}

bool CProfilesPage::AcceptanceRetryCurrentPolicy()
{
    if (!m_Retry.IsWindowEnabled() || Dirty()) return false; auto before = m_Coordinator.RepositoryWriteCount();
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_RETRY, BN_CLICKED), reinterpret_cast<LPARAM>(m_Retry.GetSafeHwnd()));
    auto observed = m_Coordinator.ObserveEnforcement();
    return !m_Retry.IsWindowEnabled() && m_Coordinator.EffectiveSelectionVerified() && observed.success && observed.observedKnown
        && before == m_Coordinator.RepositoryWriteCount();
}

bool CProfilesPage::AcceptanceSearchSelectsOtherProfile()
{
    if (!m_Draft || Dirty()) return false; auto before = m_Coordinator.RepositoryWriteCount(); auto original = m_Draft->id;
    auto target = std::find_if(m_Coordinator.Snapshot().profiles.begin(), m_Coordinator.Snapshot().profiles.end(),
        [&](auto const& item) { return item.first != original; });
    if (target == m_Coordinator.Snapshot().profiles.end()) return false;
    m_Search.SetWindowTextW(target->second.name.c_str());
    CString retained; m_Search.GetWindowTextW(retained); auto selected = SelectedId();
    return std::wstring(retained) == target->second.name && m_Profiles.GetCount() == 1 && selected && *selected == target->first
        && m_Draft && m_Draft->id == target->first && !Dirty() && before == m_Coordinator.RepositoryWriteCount();
}

bool CProfilesPage::AcceptanceDirtyRepositoryInvalidation()
{
    if (!m_Draft || Dirty()) return false; m_Name.SetWindowTextW((m_Draft->name + L" detached").c_str());
    if (!Dirty()) return false; auto detached = *m_Draft; auto beforeWrites = m_Coordinator.RepositoryWriteCount();
    auto settings = m_Application.Root() / L"settings.json"; auto original = HidHide::Profiles::ReadBytes(settings);
    { std::ofstream corrupt(settings, std::ios::binary | std::ios::trunc); corrupt << "{"; }
    if (!m_Coordinator.ReloadRepositoryIfChanged()) return false; RepositoryChanged();
    if (m_RepositoryWritable || m_Apply.IsWindowEnabled() || !m_Draft || !(*m_Draft == detached)
        || beforeWrites != m_Coordinator.RepositoryWriteCount() || HidHide::Profiles::ReadBytes(settings) != "{") return false;
    { std::ofstream repaired(settings, std::ios::binary | std::ios::trunc); repaired.write(original.data(), static_cast<std::streamsize>(original.size())); }
    if (!m_Coordinator.ReloadRepositoryIfChanged()) return false; RepositoryChanged();
    return m_RepositoryWritable && m_Apply.IsWindowEnabled() && m_Draft && *m_Draft == detached
        && beforeWrites == m_Coordinator.RepositoryWriteCount();
}

bool CProfilesPage::AcceptanceVerificationInvalidation(std::function<void(bool)> failScans)
{
    if (!m_Draft || Dirty() || !m_Coordinator.EffectiveSelectionVerified() || !AcceptanceStageProfile() || !ApplyDraft()
        || !m_Coordinator.EffectiveSelectionVerified()) throw std::runtime_error("verification invalidation precondition failed");
    m_Devices.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); m_Visible.SendMessageW(BM_CLICK);
    if (!Dirty()) throw std::runtime_error("policy edit did not create a draft"); failScans(true); auto saved = ApplyDraft(); failScans(false);
    RefreshStatus(); CString effective; m_Effective.GetWindowTextW(effective);
    if (!saved) throw std::runtime_error("policy save was not reported saved");
    if (m_Coordinator.EffectiveSelectionVerified()) throw std::runtime_error("saved scan failure retained effective verification");
    if (effective.Find(L"current state unknown") < 0) throw std::runtime_error("saved scan failure UI did not show unverified state");

    auto retried = m_Coordinator.RetryActivation(); RefreshStatus();
    if (!retried.observedKnown || !m_Coordinator.EffectiveSelectionVerified() || !m_Saved) throw std::runtime_error("verification retry failed");
    auto changed = *m_Saved;
    if (changed.rules.empty()) throw std::runtime_error("saved policy rule was absent after retry");
    changed.name = L"Unverified replacement";
    changed.rules.front().visibility = changed.rules.front().visibility == HidHide::Profiles::Visibility::Hidden
        ? HidHide::Profiles::Visibility::Visible : HidHide::Profiles::Visibility::Hidden;
    (void)m_Application.Apply(changed, m_Application.Version(changed.id));
    if (!m_Coordinator.ReloadRepositoryIfChanged()) throw std::runtime_error("external same-id policy edit was not reloaded");
    if (m_Coordinator.EffectiveSelectionVerified()) throw std::runtime_error("external same-id policy edit retained effective verification");
    RepositoryChanged(); m_Effective.GetWindowTextW(effective); bool rowNeutral{};
    for (int index{}; index < m_Profiles.GetCount(); ++index)
    {
        CString text; m_Profiles.GetText(index, text);
        if (text.Find(L"Unverified replacement") >= 0) rowNeutral = text.Find(L"Saved selection") >= 0 && text.Find(L"Last verified") < 0;
    }
    return !m_Coordinator.EffectiveSelectionVerified() && effective.Find(L"Saved selection: Unverified replacement") >= 0
        && effective.Find(L"Last verified") < 0 && rowNeutral;
}

bool CProfilesPage::AcceptanceMainDriverConflictAction(bool expectedAvailable, bool expectAllowedAppsDraft,
    std::function<void()> beforeConfirm, bool expectedAdopted)
{
    auto available = m_Coordinator.HasDriverConflict() && !m_Coordinator.HasRepositoryDiagnostics();
    if (available != expectedAvailable) return false;
    if (!expectedAvailable) return !AdoptCurrentDriverSettings();
    std::atomic_bool confirmed{};
    std::thread automation([&]
    {
        struct Search { DWORD process{}; HWND dialog{}; } search{ ::GetCurrentProcessId() };
        auto find = [](HWND window, LPARAM value)
        {
            auto& state = *reinterpret_cast<Search*>(value); DWORD process{}; ::GetWindowThreadProcessId(window, &process); wchar_t name[32]{}; ::GetClassNameW(window, name, static_cast<int>(std::size(name)));
            wchar_t title[128]{}; ::GetWindowTextW(window, title, static_cast<int>(std::size(title)));
            if (process == state.process && 0 == wcscmp(name, L"#32770") && 0 == wcscmp(title, L"Accept current driver settings") && ::IsWindowVisible(window)) { state.dialog = window; return FALSE; } return TRUE;
        };
        for (int attempt{}; attempt < 500 && !search.dialog; ++attempt) { ::EnumWindows(find, reinterpret_cast<LPARAM>(&search)); if (!search.dialog) ::Sleep(20); }
        if (search.dialog) { if (beforeConfirm) beforeConfirm(); ::PostMessageW(search.dialog, WM_COMMAND, IDYES, 0); confirmed = true; }
    });
    auto adopted = AdoptCurrentDriverSettings(); automation.join();
    if (!expectedAdopted) return confirmed && !adopted && (m_Coordinator.HasRepositoryDiagnostics() || m_Retry.IsWindowEnabled());
    return confirmed && adopted && !m_Coordinator.HasDriverConflict()
        && (expectAllowedAppsDraft ? Dirty() : (!Dirty() && m_Coordinator.EffectiveSelectionVerified()));
}

bool CProfilesPage::AcceptanceAdoptionFailureOffersRetry()
{
    if (Dirty() || !m_Retry.IsWindowEnabled() || m_Coordinator.HasDriverConflict()) return false;
    CString result; m_Result.GetWindowTextW(result); return result.Find(L"Retry activation") >= 0;
}

bool CProfilesPage::AcceptanceGlobalPresentation(std::wstring const& rowState, std::wstring const& topState, std::wstring const& reasonState) const
{
    auto global = m_Coordinator.Snapshot().settings.selectedGlobalId; bool row{};
    for (std::size_t index{}; index < m_ProfileIds.size(); ++index) if (m_ProfileIds[index] == global)
    {
        CString text; m_Profiles.GetText(static_cast<int>(index), text); row = std::wstring(text).find(rowState) != std::wstring::npos; break;
    }
    CString top, reason; m_Effective.GetWindowTextW(top); m_Reason.GetWindowTextW(reason);
    return row && (std::wstring(top)+L" "+std::wstring(reason)).find(topState) != std::wstring::npos && std::wstring(reason).find(reasonState) != std::wstring::npos;
}

bool CProfilesPage::AcceptanceSetGlobalMode(bool manual, bool paused)
{
    m_Mode.SetCurSel(manual ? 1 : 0);
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_MODE, CBN_SELCHANGE), reinterpret_cast<LPARAM>(m_Mode.GetSafeHwnd()));
    if (m_DraftSettings.paused != paused) StagePause(paused);
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_APPLY, BN_CLICKED), reinterpret_cast<LPARAM>(m_Apply.GetSafeHwnd()));
    return !Dirty() && m_Coordinator.EffectiveSelectionVerified();
}

bool CProfilesPage::AcceptanceAbandonAdoptionDraft(unsigned action)
{
    if (!Dirty() || !m_Coordinator.AdoptionAwaitingSave()) return false;
    auto answerNo = [&](auto invoke)
    {
        std::atomic_bool clicked{};
        std::thread automation([&]
        {
            struct Search { DWORD process{}; HWND dialog{}; } search{ ::GetCurrentProcessId() };
            auto find = [](HWND window, LPARAM value)
            {
                auto& state = *reinterpret_cast<Search*>(value); DWORD process{}; ::GetWindowThreadProcessId(window, &process); wchar_t name[32]{};
                ::GetClassNameW(window, name, static_cast<int>(std::size(name)));
                wchar_t title[128]{}; ::GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (process == state.process && 0 == wcscmp(name, L"#32770") && 0 == wcscmp(title, L"Unsaved profile changes") && ::IsWindowVisible(window)) { state.dialog = window; return FALSE; } return TRUE;
            };
            for (int attempt{}; attempt < 500 && !search.dialog; ++attempt) { ::EnumWindows(find, reinterpret_cast<LPARAM>(&search)); if (!search.dialog) ::Sleep(20); }
            if (search.dialog) { ::PostMessageW(search.dialog, WM_COMMAND, IDNO, 0); clicked = true; }
        });
        invoke(); automation.join(); return clicked.load();
    };
    if (action == 0)
        SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_DISCARD, BN_CLICKED), reinterpret_cast<LPARAM>(m_Discard.GetSafeHwnd()));
    else if (action == 1)
    {
        auto selected = SelectedId(); auto target = std::find_if(m_ProfileIds.begin(), m_ProfileIds.end(), [&](auto const& id) { return !selected || id != *selected; });
        if (target == m_ProfileIds.end()) return false; m_Profiles.SetCurSel(static_cast<int>(std::distance(m_ProfileIds.begin(), target)));
        if (!answerNo([&] { SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_LIST, LBN_SELCHANGE), reinterpret_cast<LPARAM>(m_Profiles.GetSafeHwnd())); })) return false;
    }
    else if (action == 2)
    {
        if (!answerNo([&] { GetParent()->SendMessageW(WM_CLOSE); })) return false;
    }
    else return false;
    return !Dirty() && !m_Coordinator.AdoptionAwaitingSave();
}

bool CProfilesPage::AcceptanceApplyAdoptedSettingsFailure()
{
    if (!Dirty() || !m_Coordinator.AdoptionAwaitingSave()) return false;
    SendMessageW(WM_COMMAND, MAKEWPARAM(IDC_PROFILE_APPLY, BN_CLICKED), reinterpret_cast<LPARAM>(m_Apply.GetSafeHwnd()));
    CString result; m_Result.GetWindowTextW(result);
    return !Dirty() && !m_Coordinator.AdoptionAwaitingSave() && m_Retry.IsWindowEnabled()
        && result.Find(L"saved") >= 0 && result.Find(L"unknown") >= 0;
}

bool CProfilesPage::AcceptanceLiveStatus(std::wstring const& highState, std::wstring const& lowState, std::wstring const& effectiveName) const
{
    bool high{}, low{};
    for (std::size_t index{}; index < m_ProfileIds.size(); ++index)
    {
        CString text; m_Profiles.GetText(static_cast<int>(index), text); auto value = std::wstring(text);
        if (value.find(L"Status high") != std::wstring::npos && value.find(highState) != std::wstring::npos) high = true;
        if (value.find(L"Status low") != std::wstring::npos && value.find(lowState) != std::wstring::npos) low = true;
    }
    CString effective, search, name; m_Effective.GetWindowTextW(effective); m_Search.GetWindowTextW(search); m_Name.GetWindowTextW(name);
    return high && low && effective.Find(effectiveName.c_str()) >= 0 && Dirty() && m_Draft
        && std::wstring(name) == L"Detached while hidden" && std::wstring(search) == L"Status";
}

bool CProfilesPage::AcceptanceBeginLiveStatusDraft()
{
    if (!AcceptanceStageDetachedStatusDraft()) return false;
    m_Search.SetWindowTextW(L"Status");
    return Dirty();
}

bool CProfilesPage::AdoptCurrentDriverSettings()
{
    if (!m_Coordinator.HasDriverConflict() || m_Coordinator.HasRepositoryDiagnostics()) return false;
    if (MessageBoxW(L"Accept the currently observed driver settings as the new restoration baseline?\n\nThis clears reviewed driver-recovery evidence and stages the observed global Allowed apps. Nothing in the JSON profile catalog changes until Apply.",
        L"Accept current driver settings", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
    try
    {
        auto allowed = m_Coordinator.AdoptExternalState(); auto allowedChanged = allowed != m_Coordinator.Snapshot().settings.allowedApplications;
        StageAllowedApplications(allowed);
        if (!allowedChanged)
        {
            auto outcome = m_Coordinator.CompleteAdoptionWithoutSettingsChange(allowed); RefreshStatus();
            if (outcome.applied)
            {
                m_RetryAvailable = false;
                m_Result.SetWindowTextW((L"Current driver settings accepted as the restoration baseline. Saved profile policy was verified and applied without changing JSON. " + outcome.message).c_str());
                RefreshDirtyState(); return true;
            }
            m_RetryAvailable = !m_Coordinator.HasDriverConflict() && !m_Coordinator.HasRepositoryDiagnostics();
            RefreshDirtyState();
            m_Result.SetWindowTextW((L"Current driver settings accepted as the restoration baseline, but the saved profile policy was not applied. "
                + outcome.message + (m_RetryAvailable ? L" Retry activation without saving." : L" Resolve the reported conflict before retrying.")).c_str());
            return false;
        }
        m_Result.SetWindowTextW(L"Current driver settings accepted as the restoration baseline. Changed observed Allowed apps are staged; review and Apply to save them."); return true;
    }
    catch (std::exception const& error)
    {
        if (m_Coordinator.HasRepositoryDiagnostics()) RepositoryChanged();
        CString message(L"Current driver settings could not be accepted: "); message += error.what(); m_Result.SetWindowTextW(message); return false;
    }
}

bool CProfilesPage::AcceptanceEditorShows(std::wstring const& name) const
{
    CString current; m_Name.GetWindowTextW(current); return m_Draft && m_Draft->name == name && std::wstring(current) == name;
}

void CProfilesPage::DoDataExchange(CDataExchange* exchange)
{
    CDialogEx::DoDataExchange(exchange);
    DDX_Control(exchange, IDC_PROFILE_EFFECTIVE, m_Effective); DDX_Control(exchange, IDC_PROFILE_EFFECTIVE_REASON, m_Reason);
    DDX_Control(exchange, IDC_PROFILE_MODE, m_Mode); DDX_Control(exchange, IDC_PROFILE_GLOBAL, m_Global); DDX_Control(exchange, IDC_PROFILE_PAUSE, m_Pause);
    DDX_Control(exchange, IDC_PROFILE_SEARCH, m_Search); DDX_Control(exchange, IDC_PROFILE_LIST, m_Profiles); DDX_Control(exchange, IDC_PROFILE_NAME, m_Name);
    DDX_Control(exchange, IDC_PROFILE_PATH, m_Path); DDX_Control(exchange, IDC_PROFILE_CHANGE_APP, m_ChangeApp); DDX_Control(exchange, IDC_PROFILE_DEVICES, m_Devices);
    DDX_Control(exchange, IDC_PROFILE_HIDDEN, m_Hidden); DDX_Control(exchange, IDC_PROFILE_VISIBLE, m_Visible); DDX_Control(exchange, IDC_PROFILE_IDENTITY, m_Identity);
    DDX_Control(exchange, IDC_PROFILE_RESULT, m_Result); DDX_Control(exchange, IDC_PROFILE_APPLY, m_Apply); DDX_Control(exchange, IDC_PROFILE_DISCARD, m_Discard); DDX_Control(exchange, IDC_PROFILE_RETRY, m_Retry);
    DDX_Control(exchange, IDC_PROFILE_ENABLED, m_Enabled); DDX_Control(exchange, IDC_PROFILE_PRIORITY, m_Priority);
}

BOOL CProfilesPage::OnInitDialog()
{
    CDialogEx::OnInitDialog(); m_Refreshing = true;
    m_Hidden.ModifyStyle(0, WS_TABSTOP | WS_GROUP); m_Visible.ModifyStyle(0, WS_TABSTOP);
    m_Mode.AddString(L"Automatic"); m_Mode.AddString(L"Use Global");
    m_Devices.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    m_Devices.InsertColumn(0, L"Device", LVCFMT_LEFT, 235); m_Devices.InsertColumn(1, L"Connection", LVCFMT_LEFT, 92);
    m_Devices.InsertColumn(2, L"Currently", LVCFMT_LEFT, 88); m_Devices.InsertColumn(3, L"After Apply", LVCFMT_LEFT, 105);
    ModifyStyle(0, WS_CLIPCHILDREN);
    // Keep existing command controls as hidden acceptance seams. Layout owns the visible surface.
    for (CWnd* child=GetWindow(GW_CHILD); child; child=child->GetNextWindow()) child->ShowWindow(SW_HIDE);
    auto button=[&](int id, wchar_t const* label, bool link=false, DWORD style=BS_PUSHBUTTON|WS_TABSTOP)
    {
        auto value=std::make_unique<ProfilesView::Button>();
        value->Create(label, WS_CHILD|WS_VISIBLE|style, CRect(), this, id);
        value->link=link; m_StyledButtons.emplace(id,std::move(value));
    };
    button(IDC_PROFILE_NEW_MENU,L"+  New"); button(IDC_PROFILE_MENU,L"Profile menu ...");
    button(IDC_PROFILE_AUTOMATIC,L"Automatic",false,BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP);
    button(IDC_PROFILE_USE_GLOBAL,L"Use Global",false,BS_AUTORADIOBUTTON);
    button(IDC_PROFILE_VIEW_PATH,L"View path",true); button(IDC_PROFILE_ADD_REMEMBERED,L"Add remembered device...",true);
    for(auto id:{IDC_PROFILE_IMPORT,IDC_PROFILE_BACKUP,IDC_PROFILE_OPEN_FOLDER,IDC_PROFILE_SETTINGS,IDC_PROFILE_ALLOWED_APPS})
    {
        auto value=std::make_unique<ProfilesView::Button>();value->SubclassDlgItem(id,this);value->link=true;value->rail=id!=IDC_PROFILE_ALLOWED_APPS;m_StyledButtons.emplace(id,std::move(value));
    }
    m_Apply.prominent=true;m_Global.ModifyStyle(0,WS_GROUP);
    m_Profiles.ModifyStyle(WS_BORDER,0);m_Profiles.ModifyStyleEx(WS_EX_CLIENTEDGE,0);
    m_Effective.ModifyStyle(SS_TYPEMASK,SS_LEFT|SS_ENDELLIPSIS);m_Reason.ModifyStyle(SS_TYPEMASK,SS_LEFT|SS_ENDELLIPSIS);
    for(auto id:{IDC_PROFILE_TITLE,IDC_PROFILE_DIRTY,IDC_PROFILE_SUBTITLE,IDC_PROFILE_TRIGGER_TITLE,IDC_PROFILE_TRIGGER_STATE,
        IDC_PROFILE_DEVICE_HELP,IDC_PROFILE_DEVICE_COUNT,IDC_PROFILE_SAVE_HINT,IDC_PROFILE_ALLOWED_HINT,
        IDC_PROFILE_HEADER_DEVICE,IDC_PROFILE_HEADER_CURRENT,IDC_PROFILE_HEADER_AFTER})
    {
        auto label=std::make_unique<CStatic>(); label->Create(L"",WS_CHILD|WS_VISIBLE|SS_ENDELLIPSIS,CRect(),this,id);m_Labels.emplace(id,std::move(label));
    }
    m_Labels[IDC_PROFILE_SUBTITLE]->SetWindowTextW(L"Choose which controllers apps can see when this profile is active.");
    m_Labels[IDC_PROFILE_DEVICE_HELP]->SetWindowTextW(L"Hidden = hidden from all apps except allowed apps. Visible = not hidden by HidHide.");
    m_Labels[IDC_PROFILE_ALLOWED_HINT]->SetWindowTextW(L"Allowed apps can always read hidden devices.");
    m_Labels[IDC_PROFILE_HEADER_DEVICE]->SetWindowTextW(L"DEVICE");
    m_Labels[IDC_PROFILE_HEADER_CURRENT]->SetWindowTextW(L"CURRENTLY");
    m_Labels[IDC_PROFILE_HEADER_AFTER]->SetWindowTextW(L"AFTER APPLY");
    m_DeviceTable.Create(this,IDC_PROFILE_DEVICE_TABLE);
    m_DeviceTable.choose=[this](size_t index,bool hidden){ChooseVisibility(index,hidden);};
    m_DeviceTable.details=[this](size_t index){ShowDeviceDetails(index);};
    m_Search.SendMessageW(EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Find a profile"));
    SetDpi(::GetDpiForWindow(m_hWnd));
    IAccPropServices* accessibility{};
    constexpr CLSID AccPropServices{ 0xb5f8350b, 0x0548, 0x48b1, { 0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7 } };
    constexpr MSAAPROPID AccName{ 0x608d3df8, 0x8128, 0x4aa7, { 0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91 } };
    if (SUCCEEDED(::CoCreateInstance(AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&accessibility))))
    {
        for (auto const& item : std::vector<std::pair<int, wchar_t const*>>{ { IDC_PROFILE_MODE, L"Profile selection mode" },
            { IDC_PROFILE_GLOBAL, L"Selected Global fallback" }, { IDC_PROFILE_SEARCH, L"Search profiles" }, { IDC_PROFILE_NAME, L"Profile name" },
            { IDC_PROFILE_LIST, L"Saved profiles" }, { IDC_PROFILE_PRIORITY, L"Application profile priority from minus 100000 to 100000" },
            { IDC_PROFILE_DEVICES, L"Profile device visibility rules" }, { IDC_PROFILE_HIDDEN, L"After Apply Hidden" },
            { IDC_PROFILE_VISIBLE, L"After Apply Visible" }, { IDC_PROFILE_ALLOWED_APPS, L"Manage global Allowed apps" },
            { IDC_PROFILE_SETTINGS, L"Settings and recovery" }, { IDC_PROFILE_PATH, L"Exact application executable path" },
            { IDC_PROFILE_IDENTITY, L"Exact expanded device policy identities" }, { IDC_PROFILE_RESULT, L"Save and activation status" },
            { IDC_PROFILE_RETRY, L"Retry activation without saving" } })
            accessibility->SetHwndPropStr(GetDlgItem(item.first)->GetSafeHwnd(), 0xfffffffcu, CHILDID_SELF, AccName, item.second);
        accessibility->Release();
    }
    m_Refreshing = false; RefreshProfiles(); RefreshStatus(); Layout(); return TRUE;
}

std::optional<std::wstring> CProfilesPage::SelectedId() const
{
    auto selected = m_Profiles.GetCurSel(); if (selected == LB_ERR || selected < 0 || static_cast<std::size_t>(selected) >= m_ProfileIds.size()) return std::nullopt;
    return m_ProfileIds[static_cast<std::size_t>(selected)];
}

void CProfilesPage::RefreshProfiles(std::optional<std::wstring> select, bool revealSelection)
{
    m_Refreshing = true; CString filter; m_Search.GetWindowTextW(filter); auto lowered = std::wstring(filter); std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::towlower);
    auto const& snapshot = m_Coordinator.Snapshot();
    if (!select) select = SelectedId();
    if (select && revealSelection)
    {
        auto selected = snapshot.profiles.find(*select);
        if (selected != snapshot.profiles.end())
        {
            auto selectedName = selected->second.name; std::transform(selectedName.begin(), selectedName.end(), selectedName.begin(), ::towlower);
            if (!lowered.empty() && selectedName.find(lowered) == std::wstring::npos)
            { m_Search.SetWindowTextW(L""); lowered.clear(); }
        }
    }
    m_Profiles.ResetContent(); m_ProfileIds.clear(); m_Global.ResetContent(); m_GlobalIds.clear();
    std::vector<std::pair<std::wstring,HidHide::Profiles::Profile>> ordered(snapshot.profiles.begin(),snapshot.profiles.end());
    std::sort(ordered.begin(),ordered.end(),[](auto const& a,auto const& b){if(a.second.priority!=b.second.priority)return a.second.priority>b.second.priority;return a.second.name<b.second.name;});
    for (auto kind : { HidHide::Profiles::Kind::Application, HidHide::Profiles::Kind::Global })
        for (auto const& [id, profile] : ordered)
        {
            if (profile.kind != kind) continue; auto nameLower = profile.name; std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
            if (lowered.empty() || nameLower.find(lowered) != std::wstring::npos)
            {
                m_Profiles.AddString(ProfileRowText(id, profile).c_str()); m_ProfileIds.push_back(id);
            }
            if (profile.kind == HidHide::Profiles::Kind::Global && profile.enabled) { auto index = m_Global.AddString(profile.name.c_str()); m_GlobalIds.push_back(id); if (id == m_DraftSettings.selectedGlobalId) m_Global.SetCurSel(index); }
        }
    int selection = 0; if (select) for (std::size_t i{}; i < m_ProfileIds.size(); ++i) if (m_ProfileIds[i] == *select) selection = static_cast<int>(i);
    if (!m_ProfileIds.empty()) { m_Profiles.SetCurSel(selection); LoadProfile(m_ProfileIds[selection]); }
    else
    {
        m_Saved.reset(); m_Draft.reset(); m_Expected.reset(); m_DeleteStaged = false;
        m_Devices.DeleteAllItems(); m_DeviceRows.clear(); m_Name.SetWindowTextW(L""); m_Path.SetWindowTextW(L""); m_Identity.SetWindowTextW(L"");
    }
    m_Mode.SetCurSel(m_DraftSettings.mode == HidHide::Profiles::Mode::Automatic ? 0 : 1); m_Refreshing = false; RefreshDraftControls(); UpdateRail();
}

std::wstring CProfilesPage::ProfileRowText(std::wstring const& id, HidHide::Profiles::Profile const& profile) const
{
    std::wstring state;
    if (!profile.enabled) state = L" — Disabled";
    else if (id == m_Coordinator.EffectiveSelection().profileId)
    {
        if (!m_Coordinator.EffectiveSelectionVerified()) state = L" — Saved selection; current state unconfirmed";
        else if (profile.kind == HidHide::Profiles::Kind::Application) state = L" — Active application";
        else if (m_Coordinator.EffectiveSelection().reason == HidHide::Profiles::SelectionReason::Paused) state = L" — Selected; hiding paused";
        else if (m_Coordinator.EffectiveSelection().reason == HidHide::Profiles::SelectionReason::ManualGlobal) state = L" — Active manual Global";
        else state = L" — Active Automatic fallback";
    }
    else if (profile.kind == HidHide::Profiles::Kind::Global)
        state = id == m_Coordinator.Snapshot().settings.selectedGlobalId ? L" — Selected fallback" : L" — Global";
    else if (m_Coordinator.IsVerifiedRunning(id)) state = L" — Running (lower priority)";
    else if (m_Coordinator.IsApplicationMissing(id)) state = L" — App missing";
    else state = L" — Waiting";
    return profile.name + state;
}

void CProfilesPage::RefreshCoordinatorPresentation()
{
    auto selected = SelectedId(); auto const& snapshot = m_Coordinator.Snapshot(); m_Refreshing = true;
    for (std::size_t index{}; index < m_ProfileIds.size(); ++index)
    {
        auto found = snapshot.profiles.find(m_ProfileIds[index]); if (found == snapshot.profiles.end()) continue;
        m_Profiles.DeleteString(static_cast<int>(index));
        m_Profiles.InsertString(static_cast<int>(index), ProfileRowText(found->first, found->second).c_str());
    }
    if (selected) for (std::size_t index{}; index < m_ProfileIds.size(); ++index)
        if (m_ProfileIds[index] == *selected) { m_Profiles.SetCurSel(static_cast<int>(index)); break; }
    m_Refreshing = false; RefreshStatus(); UpdateRail();
}

void CProfilesPage::LoadProfile(std::wstring const& id)
{
    auto const& snapshot = m_Coordinator.Snapshot(); auto found = snapshot.profiles.find(id); if (found == snapshot.profiles.end()) return;
    m_Saved = found->second; m_Draft = *m_Saved; m_DeleteStaged = false; m_SavedSettings = snapshot.settings; m_DraftSettings = snapshot.settings;
    try { m_Expected = m_Application.Version(id); m_ExpectedSettings = m_Application.SettingsVersion(); m_RepositoryWritable = m_Coordinator.Issues().empty(); }
    catch (...) { m_Expected.reset(); m_ExpectedSettings = {}; m_RepositoryWritable = false; }
    RefreshDevices();
}

void CProfilesPage::RefreshDevices()
{
    if (!m_Draft) return; m_Refreshing = true; m_DeviceRows.clear();
    auto lessIdentity = [](std::wstring const& left, std::wstring const& right) { return _wcsicmp(left.c_str(), right.c_str()) < 0; };
    std::set<std::wstring, decltype(lessIdentity)> added(lessIdentity);
    auto contains = [](auto const& identities, std::wstring const& identity)
    { return std::any_of(identities.begin(), identities.end(), [&](auto const& candidate) { return _wcsicmp(candidate.c_str(), identity.c_str()) == 0; }); };
    auto stateOf = [&](auto const& identities, auto const& hidden) -> RowVisibility
    {
        auto count = std::count_if(identities.begin(), identities.end(), [&](auto const& identity) { return contains(hidden, identity); });
        return count == 0 ? RowVisibility::Visible : count == static_cast<decltype(count)>(identities.size()) ? RowVisibility::Hidden : RowVisibility::Mixed;
    };
    auto wasVerified = m_Coordinator.EffectiveSelectionVerified(); auto wasConflict = m_Coordinator.HasDriverConflict(); auto wasStatus = m_Coordinator.Status();
    auto observed = m_Coordinator.ObserveEnforcement(); auto observedKnown = observed.observedKnown;
    bool enumerationKnown{ true };
    try
    {
        for (auto device : m_DeviceSource.Enumerate())
        {
            if (device.policyIdentities.empty()) device.policyIdentities.push_back(device.identity);
            bool newGroup{}; for (auto const& identity : device.policyIdentities) newGroup = added.emplace(identity).second || newGroup;
            if (!newGroup) continue;
            auto current = observedKnown ? std::optional<RowVisibility>(observed.observed.hidingEnabled ? stateOf(device.policyIdentities, observed.observed.hiddenDevices) : RowVisibility::Visible) : std::nullopt;
            m_DeviceRows.push_back({ device.identity, device.friendly, device.connected, true, current, std::move(device.policyIdentities) });
        }
    }
    catch (...) { enumerationKnown = false; }
    for (auto const& rule : m_Draft->rules) if (added.emplace(rule.identity).second)
    {
        auto current = observedKnown ? std::optional<RowVisibility>(observed.observed.hidingEnabled && contains(observed.observed.hiddenDevices, rule.identity) ? RowVisibility::Hidden : RowVisibility::Visible) : std::nullopt;
        m_DeviceRows.push_back({ rule.identity, rule.friendlyName + L" — remembered exact path", false, enumerationKnown, current, { rule.identity } });
    }
    m_Devices.DeleteAllItems();
    for (std::size_t i{}; i < m_DeviceRows.size(); ++i)
    {
        auto const& row = m_DeviceRows[i]; auto after = stateOf(row.policyIdentities, HidHide::Profiles::HiddenDevices(*m_Draft));
        auto index = m_Devices.InsertItem(static_cast<int>(i), row.friendly.c_str()); m_Devices.SetItemText(index, 1, !row.connectionKnown ? L"Unknown" : row.connected ? L"Connected" : L"Disconnected — rule kept");
        m_Devices.SetItemText(index, 2, !row.currently ? L"Unknown" : *row.currently == RowVisibility::Hidden ? L"Hidden" : *row.currently == RowVisibility::Mixed ? L"Mixed — inspect paths" : L"Visible");
        m_Devices.SetItemText(index, 3, after == RowVisibility::Hidden ? L"Hidden" : after == RowVisibility::Mixed ? L"Mixed — inspect paths" : L"Visible");
    }
    if (!m_DeviceRows.empty()) { m_Devices.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); m_Devices.EnsureVisible(0, FALSE); }
    m_Refreshing = false; LRESULT ignored{}; OnDeviceSelection(nullptr, &ignored); UpdateView();
    if ((wasVerified != m_Coordinator.EffectiveSelectionVerified() || wasConflict != m_Coordinator.HasDriverConflict()
        || wasStatus != m_Coordinator.Status()) && GetParent() && GetParent()->GetSafeHwnd())
        GetParent()->PostMessageW(WM_PROFILE_CHANGED, 0, 0);
}

bool CProfilesPage::Dirty() const { return m_Draft && (m_DeleteStaged || !m_Saved || !(*m_Draft == *m_Saved) || !(m_DraftSettings == m_SavedSettings)); }

void CProfilesPage::RefreshDraftControls()
{
    m_Refreshing = true; if (m_Draft) { m_Name.SetWindowTextW(m_Draft->name.c_str()); m_Path.SetWindowTextW(m_Draft->kind == HidHide::Profiles::Kind::Application ? m_Draft->executable.c_str() : L"Global fallback: complete device visibility policy"); m_ChangeApp.EnableWindow(m_Draft->kind == HidHide::Profiles::Kind::Application); m_Enabled.SetCheck(m_Draft->enabled ? BST_CHECKED : BST_UNCHECKED); m_Priority.SetWindowTextW(std::to_wstring(m_Draft->priority).c_str()); m_Priority.EnableWindow(m_Draft->kind == HidHide::Profiles::Kind::Application); }
    m_Mode.SetCurSel(m_DraftSettings.mode == HidHide::Profiles::Mode::Automatic ? 0 : 1); m_Pause.SetWindowTextW(m_DraftSettings.paused ? L"Resume hiding..." : L"Pause hiding...");
    for (std::size_t i{}; i < m_GlobalIds.size(); ++i) if (m_GlobalIds[i] == m_DraftSettings.selectedGlobalId) m_Global.SetCurSel(static_cast<int>(i));
    for (auto id : { IDC_PROFILE_NEW_APP, IDC_PROFILE_NEW_GLOBAL, IDC_PROFILE_IMPORT, IDC_PROFILE_BACKUP, IDC_PROFILE_MODE,
        IDC_PROFILE_GLOBAL, IDC_PROFILE_NAME, IDC_PROFILE_ENABLED,
        IDC_PROFILE_HIDDEN, IDC_PROFILE_VISIBLE, IDC_PROFILE_ALLOWED_APPS, IDC_PROFILE_DELETE })
        if (auto control = GetDlgItem(id)) control->EnableWindow(m_RepositoryWritable);
    auto application = m_Draft && m_Draft->kind == HidHide::Profiles::Kind::Application;
    m_ChangeApp.EnableWindow(m_RepositoryWritable && application); m_Priority.EnableWindow(m_RepositoryWritable && application);
    m_Refreshing = false; RefreshDirtyState();
}

void CProfilesPage::RefreshDirtyState(std::wstring const& validation)
{
    UpdateView(); UpdateRail();
    Layout();
    auto dirty = Dirty(); m_Apply.EnableWindow(dirty && validation.empty() && m_RepositoryWritable); m_Discard.EnableWindow(dirty);
    m_Retry.EnableWindow(!dirty && m_RetryAvailable && m_RepositoryWritable);
    if (!m_RepositoryWritable) { m_Result.SetWindowTextW(L"Repository diagnostics require attention. Files are preserved; Apply is blocked until the JSON is repaired."); return; }
    if (!validation.empty()) { m_Result.SetWindowTextW(validation.c_str()); return; }
    if (dirty)
    {
        auto ruleChanges = [](auto const& left, auto const& right)
        {
            std::size_t count{}; std::vector<bool> matched(right.size());
            for (auto const& rule : left)
            {
                auto found = std::find_if(right.begin(), right.end(), [&](auto const& candidate) { return _wcsicmp(rule.identity.c_str(), candidate.identity.c_str()) == 0; });
                if (found == right.end()) { ++count; continue; }
                matched[static_cast<std::size_t>(std::distance(right.begin(), found))] = true;
                if (!(rule == *found)) ++count;
            }
            count += static_cast<std::size_t>(std::count(matched.begin(), matched.end(), false)); return count;
        };
        auto settingsChanges = [](auto const& left, auto const& right)
        {
            std::size_t count{}; count += left.selectedGlobalId != right.selectedGlobalId; count += left.mode != right.mode;
            count += left.paused != right.paused; count += left.startWithWindows != right.startWithWindows;
            for (auto const& value : left.allowedApplications) count += !right.allowedApplications.count(value);
            for (auto const& value : right.allowedApplications) count += !left.allowedApplications.count(value);
            return count;
        };
        std::size_t changes{ m_DeleteStaged ? 1u : 0u };
        if (!m_Saved) changes = 1 + (m_Draft ? m_Draft->rules.size() : 0);
        else if (!m_DeleteStaged)
        {
            changes += m_Draft->name != m_Saved->name; changes += m_Draft->enabled != m_Saved->enabled;
            changes += m_Draft->priority != m_Saved->priority; changes += m_Draft->executable != m_Saved->executable;
            changes += ruleChanges(m_Draft->rules, m_Saved->rules);
        }
        changes += settingsChanges(m_DraftSettings, m_SavedSettings);
        m_Result.SetWindowTextW((std::to_wstring(changes) + L" change" + (changes == 1 ? L"" : L"s") + L" ready to Apply.").c_str());
    }
}

void CProfilesPage::RefreshStatus()
{
    auto const& selection = m_Coordinator.EffectiveSelection(); auto const& profiles = m_Coordinator.Snapshot().profiles; auto found = profiles.find(selection.profileId);
    if (found == profiles.end()) m_Effective.SetWindowTextW(L"Effective profile unknown");
    else if (!m_Coordinator.EffectiveSelectionVerified()) m_Effective.SetWindowTextW((L"Saved selection: " + found->second.name + L" — current state unknown or conflicting").c_str());
    else if (selection.reason == HidHide::Profiles::SelectionReason::Paused)
        m_Effective.SetWindowTextW((found->second.name + L" selected — hiding paused; all devices are visible").c_str());
    else if (selection.reason == HidHide::Profiles::SelectionReason::ManualGlobal)
        m_Effective.SetWindowTextW((found->second.name + L" is active — verified Use Global selection").c_str());
    else if (selection.reason == HidHide::Profiles::SelectionReason::GlobalFallback
        || selection.reason == HidHide::Profiles::SelectionReason::DetectionUncertain)
        m_Effective.SetWindowTextW((found->second.name + L" is active — verified Automatic fallback").c_str());
    else m_Effective.SetWindowTextW((found->second.name + L" is active — verified running application").c_str());
    CString semantic;m_Effective.GetWindowTextW(semantic);
    ProfilesView::AccessibleName(m_Effective,semantic.GetString());
    if(found!=profiles.end()&&m_Coordinator.EffectiveSelectionVerified())
        m_Effective.SetWindowTextW((found->second.name+(selection.reason==HidHide::Profiles::SelectionReason::Paused?L" - hiding paused":L" is active")).c_str());
    auto reason=m_Coordinator.Status();
    if(m_Coordinator.EffectiveSelectionVerified())
        reason=selection.reason==HidHide::Profiles::SelectionReason::Paused?L"Hiding paused - profiles saved":
            selection.reason==HidHide::Profiles::SelectionReason::ManualGlobal?L"Verified - Manual Global policy":
            selection.reason==HidHide::Profiles::SelectionReason::GlobalFallback?L"Verified - Automatic Global fallback":L"Verified - application is running";
    m_Reason.SetWindowTextW(reason.c_str());ProfilesView::AccessibleName(m_Reason,m_Coordinator.Status());
    UpdateView();
}
void CProfilesPage::RepositoryChanged()
{
    m_RepositoryWritable = m_Coordinator.Issues().empty();
    if (Dirty())
    {
        RefreshDraftControls();
        m_Result.SetWindowTextW(m_RepositoryWritable
            ? L"Files changed outside this window. Your detached draft is preserved; Apply will require conflict resolution."
            : L"Repository diagnostics require attention. Your detached draft is preserved; Apply is blocked until the JSON is repaired.");
        RefreshStatus();
    }
    else { RefreshProfiles(); RefreshStatus(); }
}

bool CProfilesPage::ApplyDraft()
{
    if (!Dirty()) return true;
    try
    {
        CString name; m_Name.GetWindowTextW(name); if (m_Draft) m_Draft->name = std::wstring(name);
        CProfilesCoordinator::ApplyOutcome outcome;
        if (m_DeleteStaged && m_Saved && m_Expected) { auto saved = m_Application.Delete(m_Saved->id, *m_Expected, m_DraftSettings, m_ExpectedSettings); outcome = m_Coordinator.PublishSaved(std::move(saved.loaded), saved.version, L"Profile deletion", saved.cleanupPending); }
        else if (m_Draft && (!m_Saved || !(*m_Draft == *m_Saved)))
        {
            auto saved = m_Application.Apply(*m_Draft, m_Expected,
                m_DraftSettings == m_SavedSettings ? std::nullopt : std::optional<HidHide::Profiles::Settings>(m_DraftSettings),
                m_DraftSettings == m_SavedSettings ? std::nullopt : std::optional<HidHide::Profiles::SavedVersion>(m_ExpectedSettings));
            outcome = m_Coordinator.PublishSaved(std::move(saved.loaded), saved.version, L"Profile", saved.cleanupPending);
        }
        else { auto saved = m_Application.ApplySettings(m_DraftSettings, m_ExpectedSettings); outcome = m_Coordinator.PublishSaved(std::move(saved.loaded), saved.version, L"Settings", saved.cleanupPending); }
        m_RetryAvailable = outcome.saved && !m_Coordinator.EffectiveSelectionVerified() && !m_Coordinator.HasDriverConflict() && !m_Coordinator.HasRepositoryDiagnostics();
        m_Result.SetWindowTextW(outcome.message.c_str()); auto id = m_Draft ? std::optional<std::wstring>(m_Draft->id) : std::nullopt; RefreshProfiles(id); RefreshStatus(); return outcome.saved;
    }
    catch (HidHide::Profiles::RepositoryConflict const&) { m_Result.SetWindowTextW(L"This profile changed outside this window. Your draft was not overwritten."); return false; }
    catch (std::exception const& error) { CString text(L"Your changes could not be saved. Nothing new was activated. "); text += error.what(); m_Result.SetWindowTextW(text); return false; }
}

bool CProfilesPage::ConfirmAbandon(wchar_t const* action)
{
    if (!Dirty()) return true; auto result = MessageBoxW((std::wstring(L"Apply changes before ") + action + L"?\n\nYes: Apply\nNo: Discard\nCancel: Keep editing").c_str(), L"Unsaved profile changes", MB_YESNOCANCEL | MB_ICONWARNING);
    if (result == IDCANCEL) return false; if (result == IDYES) return ApplyDraft(); OnDiscard(); return true;
}

void CProfilesPage::OnProfileSelection()
{
    if (m_Refreshing) return; auto id = SelectedId(); if (!id) return;
    if (!ConfirmAbandon(L"switching profiles"))
    {
        auto draftId = m_Draft ? m_Draft->id : std::wstring{}; auto found = std::find(m_ProfileIds.begin(), m_ProfileIds.end(), draftId);
        m_Refreshing = true; m_Profiles.SetCurSel(found == m_ProfileIds.end() ? -1 : static_cast<int>(std::distance(m_ProfileIds.begin(), found))); m_Refreshing = false;
        return;
    }
    auto found = std::find(m_ProfileIds.begin(), m_ProfileIds.end(), *id);
    if (found == m_ProfileIds.end()) RefreshProfiles(*id);
    else { m_Refreshing = true; m_Profiles.SetCurSel(static_cast<int>(std::distance(m_ProfileIds.begin(), found))); m_Refreshing = false; LoadProfile(*id); RefreshDraftControls(); }
}
void CProfilesPage::OnSearchChanged() { if (!m_Refreshing && !Dirty()) RefreshProfiles(std::nullopt, false); }
void CProfilesPage::OnDraftChanged()
{
    if (m_Refreshing || !m_Draft || !m_RepositoryWritable) return; CString name, priority; m_Name.GetWindowTextW(name); m_Priority.GetWindowTextW(priority); m_Draft->name = std::wstring(name); m_Draft->enabled = m_Enabled.GetCheck() == BST_CHECKED;
    if (m_Draft->kind == HidHide::Profiles::Kind::Global) { RefreshDirtyState(); return; }
    try
    {
        std::size_t consumed{}; auto text = std::wstring(priority); auto value = std::stoll(text, &consumed);
        if (consumed != text.size() || value < HidHide::Profiles::MinPriority || value > HidHide::Profiles::MaxPriority) throw std::out_of_range("priority");
        m_Draft->priority = static_cast<std::int32_t>(value); RefreshDirtyState();
    }
    catch (...) { RefreshDirtyState(L"Priority must be a whole number from -100000 to 100000. Finish the value before Apply."); }
}
void CProfilesPage::OnDeviceSelection(NMHDR*, LRESULT* result)
{
    if (result) *result = 0; if (m_Refreshing || !m_Draft) return; auto selected = m_Devices.GetNextItem(-1, LVNI_SELECTED); if (selected < 0 || static_cast<std::size_t>(selected) >= m_DeviceRows.size()) return;
    auto const& row = m_DeviceRows[static_cast<std::size_t>(selected)]; std::size_t hiddenCount{};
    for (auto const& identity : row.policyIdentities)
    {
        auto found = std::find_if(m_Draft->rules.begin(), m_Draft->rules.end(), [&](auto const& rule) { return _wcsicmp(rule.identity.c_str(), identity.c_str()) == 0; });
        if (found != m_Draft->rules.end() && found->visibility == HidHide::Profiles::Visibility::Hidden) ++hiddenCount;
    }
    auto hidden = hiddenCount == row.policyIdentities.size(), visible = hiddenCount == 0;
    m_Hidden.SetCheck(hidden ? BST_CHECKED : BST_UNCHECKED); m_Visible.SetCheck(visible ? BST_CHECKED : BST_UNCHECKED);
    std::wostringstream identities;
    identities << (row.policyIdentities.size() > 1 ? L"Exact HID/XInput/container policy paths:" : L"Exact device identity:");
    for (auto const& identity : row.policyIdentities) identities << L"\r\n" << identity;
    if (!row.connectionKnown) identities << L"\r\nConnection state unknown"; else if (!row.connected) identities << L"\r\nDisconnected exact identity retained";
    m_Identity.SetWindowTextW(identities.str().c_str());
}
void CProfilesPage::OnVisibility()
{
    if (m_Refreshing || !m_Draft || !m_RepositoryWritable) return; auto selected = m_Devices.GetNextItem(-1, LVNI_SELECTED); if (selected < 0 || static_cast<std::size_t>(selected) >= m_DeviceRows.size()) return; auto const& row = m_DeviceRows[static_cast<std::size_t>(selected)];
    auto visibility = m_Hidden.GetCheck() == BST_CHECKED ? HidHide::Profiles::Visibility::Hidden : HidHide::Profiles::Visibility::Visible;
    for (auto const& identity : row.policyIdentities)
    {
        auto found = std::find_if(m_Draft->rules.begin(), m_Draft->rules.end(), [&](auto const& rule) { return _wcsicmp(rule.identity.c_str(), identity.c_str()) == 0; });
        if (found == m_Draft->rules.end()) m_Draft->rules.push_back({ identity, row.friendly, visibility }); else found->visibility = visibility;
    }
    RefreshDevices(); RefreshDraftControls();
}
void CProfilesPage::OnHiddenVisibility(){m_Hidden.SetCheck(BST_CHECKED);m_Visible.SetCheck(BST_UNCHECKED);OnVisibility();}
void CProfilesPage::OnVisibleVisibility(){m_Hidden.SetCheck(BST_UNCHECKED);m_Visible.SetCheck(BST_CHECKED);OnVisibility();}
void CProfilesPage::OnNewApplication()
{
    if (!m_RepositoryWritable) { RefreshDirtyState(); return; } if (!ConfirmAbandon(L"creating a profile")) return; CFileDialog dialog(TRUE, L"exe", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"Applications (*.exe)|*.exe||", this); if (dialog.DoModal() != IDOK) return;
    BeginNewApplication(dialog.GetPathName().GetString());
}
void CProfilesPage::BeginNewApplication(std::filesystem::path const& executable)
{
    HidHide::Profiles::Profile profile; profile.id = HidHide::Profiles::NewStableId(); profile.name = executable.stem().native(); profile.kind = HidHide::Profiles::Kind::Application; profile.enabled = true; profile.priority = 0; profile.executable = HidHide::Profiles::NormalizeExecutable(executable);
    m_Saved.reset(); m_Draft = profile; m_Expected.reset(); m_SavedSettings = m_Coordinator.Snapshot().settings; m_DraftSettings = m_SavedSettings; m_ExpectedSettings = m_Application.SettingsVersion(); RefreshDevices(); RefreshDraftControls(); m_Result.SetWindowTextW(L"New draft. Nothing is saved until Apply.");
}
void CProfilesPage::OnNewGlobal()
{
    if (!m_RepositoryWritable) { RefreshDirtyState(); return; } if (!ConfirmAbandon(L"creating a profile")) return; HidHide::Profiles::Profile profile; profile.id = HidHide::Profiles::NewStableId(); profile.name = L"New Global"; profile.kind = HidHide::Profiles::Kind::Global; profile.enabled = true; profile.revision = 0;
    m_Saved.reset(); m_Draft = profile; m_Expected.reset(); m_SavedSettings = m_Coordinator.Snapshot().settings; m_DraftSettings = m_SavedSettings; m_ExpectedSettings = m_Application.SettingsVersion(); RefreshDevices(); RefreshDraftControls(); m_Result.SetWindowTextW(L"New Global draft. Nothing is saved until Apply.");
}
void CProfilesPage::OnChangeApplication() { if (!m_RepositoryWritable || !m_Draft || m_Draft->kind != HidHide::Profiles::Kind::Application) { if (!m_RepositoryWritable) RefreshDirtyState(); return; } CFileDialog dialog(TRUE, L"exe", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"Applications (*.exe)|*.exe||", this); if (dialog.DoModal() == IDOK) { m_Draft->executable = HidHide::Profiles::NormalizeExecutable(dialog.GetPathName().GetString()); RefreshDraftControls(); } }
void CProfilesPage::OnApply() { ApplyDraft(); }
void CProfilesPage::OnDiscard()
{
    std::optional<CProfilesCoordinator::ApplyOutcome> adoptionOutcome;
    if (m_Coordinator.AdoptionAwaitingSave()) adoptionOutcome = m_Coordinator.AbandonAdoptionDraft();
    m_DeleteStaged = false; auto savedId = m_Saved ? std::optional<std::wstring>(m_Saved->id) : std::nullopt;
    if (savedId) RefreshProfiles(savedId); else RefreshProfiles(); m_DraftSettings = m_SavedSettings;
    if (adoptionOutcome) m_RetryAvailable = !adoptionOutcome->applied && !m_Coordinator.HasDriverConflict() && !m_Coordinator.HasRepositoryDiagnostics();
    RefreshDevices(); RefreshDraftControls();
    m_Result.SetWindowTextW(adoptionOutcome ? adoptionOutcome->message.c_str() : L"Draft discarded.");
}
void CProfilesPage::OnRetry()
{
    if (Dirty() || !m_RetryAvailable) return; auto outcome = m_Coordinator.RetryActivation(); m_RetryAvailable = !outcome.applied;
    m_Result.SetWindowTextW(outcome.message.c_str()); RefreshDevices(); RefreshStatus(); RefreshDirtyState();
}
void CProfilesPage::OnModeChanged() { if (!m_Refreshing && m_RepositoryWritable) { m_DraftSettings.mode = m_Mode.GetCurSel() == 0 ? HidHide::Profiles::Mode::Automatic : HidHide::Profiles::Mode::UseGlobal; RefreshDraftControls(); } }
void CProfilesPage::OnGlobalChanged() { if (!m_Refreshing && m_RepositoryWritable) { auto index = m_Global.GetCurSel(); if (index >= 0 && static_cast<std::size_t>(index) < m_GlobalIds.size()) m_DraftSettings.selectedGlobalId = m_GlobalIds[static_cast<std::size_t>(index)]; RefreshDraftControls(); } }
void CProfilesPage::OnPause() { if (!m_RepositoryWritable) { RefreshDirtyState(); return; } m_DraftSettings.paused = !m_DraftSettings.paused; RefreshDraftControls(); m_Result.SetWindowTextW(m_DraftSettings.paused ? L"Pause is staged until Apply." : L"Resume is staged until Apply."); }
void CProfilesPage::StagePause(bool paused) { if (!m_RepositoryWritable) { RefreshDirtyState(); return; } m_DraftSettings.paused = paused; RefreshDraftControls(); m_Result.SetWindowTextW(paused ? L"Pause is staged until Apply." : L"Resume is staged until Apply."); }
void CProfilesPage::StageAllowedApplications(std::set<std::filesystem::path> allowed)
{
    if (!m_RepositoryWritable) { RefreshDirtyState(); return; }
    m_DraftSettings.allowedApplications = std::move(allowed); RefreshDraftControls();
    m_Result.SetWindowTextW(L"The accepted driver Allowed apps are staged. Review and press Apply to save them; reconciliation remains paused until then.");
}
void CProfilesPage::OnAllowedApps()
{
    if (!m_RepositoryWritable) { RefreshDirtyState(); return; }
    CMenu menu; menu.CreatePopupMenu(); menu.AppendMenuW(MF_STRING, 1, L"Add allowed app...");
    if (!m_DraftSettings.allowedApplications.empty()) menu.AppendMenuW(MF_SEPARATOR);
    std::vector<std::filesystem::path> applications(m_DraftSettings.allowedApplications.begin(), m_DraftSettings.allowedApplications.end());
    for (std::size_t i{}; i < applications.size(); ++i) menu.AppendMenuW(MF_STRING, 100 + static_cast<UINT>(i), (L"Remove: " + applications[i].native()).c_str());
    CRect button; GetDlgItem(IDC_PROFILE_ALLOWED_APPS)->GetWindowRect(button); auto command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN, button.left, button.bottom, this);
    if (command == 1)
    {
        CFileDialog dialog(TRUE, L"exe", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"Applications (*.exe)|*.exe||", this);
        if (dialog.DoModal() == IDOK) m_DraftSettings.allowedApplications.emplace(HidHide::Profiles::NormalizeExecutable(dialog.GetPathName().GetString()));
    }
    else if (command >= 100 && static_cast<std::size_t>(command - 100) < applications.size()) m_DraftSettings.allowedApplications.erase(applications[command - 100]);
    if (command) { RefreshDraftControls(); m_Result.SetWindowTextW((std::to_wstring(m_DraftSettings.allowedApplications.size()) + L" global Allowed app(s) staged. They can always read hidden devices.").c_str()); }
}
void CProfilesPage::OnSettings()
{
    CMenu menu; menu.CreatePopupMenu();
    menu.AppendMenuW(MF_STRING | (m_DraftSettings.startWithWindows ? MF_CHECKED : 0) | (m_RepositoryWritable ? 0 : MF_GRAYED), 1, L"Start HidHide Profiles with Windows");
    menu.AppendMenuW(MF_SEPARATOR); menu.AppendMenuW(MF_STRING | (m_Coordinator.Issues().empty() ? MF_GRAYED : 0), 2, L"Show repository diagnostics");
    menu.AppendMenuW(MF_STRING, 3, L"Restore portable backup...");
    if (m_Coordinator.HasDriverConflict() && !m_Coordinator.HasRepositoryDiagnostics())
    {
        menu.AppendMenuW(MF_SEPARATOR);
        menu.AppendMenuW(MF_STRING, 4, L"Resolve driver conflict: accept current driver settings...");
    }
    CRect button; GetDlgItem(IDC_PROFILE_SETTINGS)->GetWindowRect(button); auto command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN, button.left, button.bottom, this);
    if (command == 1) { m_DraftSettings.startWithWindows = !m_DraftSettings.startWithWindows; RefreshDraftControls(); m_Result.SetWindowTextW(L"Startup preference staged. Nothing changes until Apply."); }
    else if (command == 2)
    {
        std::wostringstream text; for (auto const& issue : m_Coordinator.Issues()) text << issue.file.filename().native() << L": " << issue.message << L'\n';
        MessageBoxW(text.str().c_str(), L"Profile repository diagnostics", MB_OK | MB_ICONWARNING);
    }
    else if (command == 3)
    {
        if (!ConfirmAbandon(L"restoring a backup")) return;
        auto source = PickFolder(this, L"Choose a HidHide Profiles portable backup folder"); if (source.empty()) return;
        if (MessageBoxW(L"Replace the saved profile catalog with this validated backup? The replacement is transactional and does not restore runtime driver state.",
            L"Restore profile backup", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return;
        RestoreBackup(source);
    }
    else if (command == 4) (void)AdoptCurrentDriverSettings();
}

bool CProfilesPage::RestoreBackup(std::filesystem::path const& source)
{
    try
    {
        auto saved = m_Application.RestoreBackup(source);
        auto outcome = m_Coordinator.PublishSaved(std::move(saved.loaded), saved.version, L"Backup restore", saved.cleanupPending);
        RefreshProfiles(); RefreshStatus(); m_Result.SetWindowTextW((L"Backup restored. " + outcome.message).c_str()); return outcome.saved && outcome.applied;
    }
    catch (std::exception const& error) { CString text(L"Backup restore failed; the saved catalog was preserved. "); text += error.what(); m_Result.SetWindowTextW(text); return false; }
}
void CProfilesPage::OnOpenFolder() { ::ShellExecuteW(m_hWnd, L"open", m_Coordinator.RepositoryRoot().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void CProfilesPage::OnImport()
{
    if (!m_RepositoryWritable) { RefreshDirtyState(); return; } if (!ConfirmAbandon(L"importing")) return; CFileDialog dialog(TRUE, L"json", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"HidHide profile (*.json)|*.json||", this); if (dialog.DoModal() != IDOK) return;
    try { auto profile = HidHide::Profiles::ParseProfile(HidHide::Profiles::ReadBytes(dialog.GetPathName().GetString())); std::wostringstream preview; preview << L"Import as a disabled copy?\n\nName: " << profile.name << L"\nApplication: " << (profile.executable.empty() ? L"Global" : profile.executable.native()) << L"\nDevice rules: " << profile.rules.size() << L"\n\nThe source ID will not replace an existing profile."; if (MessageBoxW(preview.str().c_str(), L"Review profile import", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return; profile.id = HidHide::Profiles::NewStableId(); profile.revision = 0; profile.enabled = false; profile.name += L" (imported — review)"; m_Saved.reset(); m_Draft = std::move(profile); m_Expected.reset(); m_SavedSettings = m_Coordinator.Snapshot().settings; m_DraftSettings = m_SavedSettings; m_ExpectedSettings = m_Application.SettingsVersion(); RefreshDevices(); RefreshDraftControls(); m_Result.SetWindowTextW(L"Import validated. This profile is disabled until reviewed and Applied."); }
    catch (std::exception const& error) { CString text(L"Profile could not be imported: "); text += error.what(); m_Result.SetWindowTextW(text); }
}
std::filesystem::path CProfilesPage::PickFolder(CWnd* owner, wchar_t const* title) { BROWSEINFOW info{}; info.hwndOwner = owner->m_hWnd; info.lpszTitle = title; info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI; auto item = ::SHBrowseForFolderW(&info); if (!item) return {}; wchar_t path[MAX_PATH]{}; auto ok = ::SHGetPathFromIDListW(item, path); ::CoTaskMemFree(item); return ok ? std::filesystem::path(path) : std::filesystem::path{}; }
void CProfilesPage::OnBackup()
{
    auto folder = PickFolder(this, L"Choose a folder for the HidHide Profiles backup"); if (folder.empty()) return;
    try { auto target = folder / (L"HidHide Profiles Backup " + HidHide::Profiles::NewStableId()); m_Application.Backup(target); m_Result.SetWindowTextW((L"Saved revision backup and portable manifest created at " + target.native()).c_str()); }
    catch (std::exception const& error) { CString text(L"Backup failed: "); text += error.what(); m_Result.SetWindowTextW(text); }
}

void CProfilesPage::OnDelete()
{
    if (!m_RepositoryWritable) { RefreshDirtyState(); return; } if (!m_Saved || !m_Expected) { m_Result.SetWindowTextW(L"Discard this unsaved draft instead."); return; }
    if (MessageBoxW((L"Stage deletion of saved profile “" + m_Saved->name + L"”? It will not be deleted until Apply.").c_str(), L"Stage profile deletion", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return;
    m_DeleteStaged = true; RefreshDraftControls(); m_Result.SetWindowTextW(L"Deletion staged. The saved profile remains unchanged until Apply.");
}

void CProfilesPage::OnExport()
{
    if (!m_Saved) { m_Result.SetWindowTextW(L"Apply this new profile before exporting it."); return; }
    if (m_DeleteStaged) { m_Result.SetWindowTextW(L"This profile is staged for deletion. Discard that draft to export its last saved revision."); return; }
    if (Dirty()) { auto choice = MessageBoxW(L"This profile has unsaved changes.\n\nYes: Apply then export\nNo: Export the last saved revision\nCancel: Do nothing", L"Export saved profile", MB_YESNOCANCEL | MB_ICONINFORMATION); if (choice == IDCANCEL || (choice == IDYES && !ApplyDraft())) return; }
    CFileDialog dialog(FALSE, L"json", (m_Saved->name + L".json").c_str(), OFN_OVERWRITEPROMPT, L"HidHide profile (*.json)|*.json||", this); if (dialog.DoModal() != IDOK) return;
    try { m_Application.ExportProfile(m_Saved->id, dialog.GetPathName().GetString()); m_Result.SetWindowTextW(L"The last saved revision was exported."); }
    catch (std::exception const& error) { CString text(L"Export failed: "); text += error.what(); m_Result.SetWindowTextW(text); }
}

void CProfilesPage::OnOpenJson()
{
    if (!m_Saved) { m_Result.SetWindowTextW(L"Apply this new profile before opening its JSON."); return; }
    auto path = m_Coordinator.RepositoryRoot() / (m_Saved->id + L".json"); auto arguments = L"/select,\"" + path.native() + L"\""; ::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}
void CProfilesPage::DevicesChanged() { RefreshDevices(); }
void CProfilesPage::OnSize(UINT type, int width, int height)
{
    CDialogEx::OnSize(type,width,height); if (type!=SIZE_MINIMIZED) Layout();
}

void CProfilesPage::SetDpi(UINT dpi)
{
    m_Dpi=dpi; ProfilesView::Font(m_Font,14,dpi);ProfilesView::Font(m_SmallFont,12,dpi);
    ProfilesView::Font(m_BoldFont,14,dpi,FW_SEMIBOLD);ProfilesView::Font(m_TitleFont,26,dpi,FW_SEMIBOLD);
    m_BackgroundBrush.DeleteObject();m_BackgroundBrush.CreateSolidBrush(ProfilesView::Background());
    m_SurfaceBrush.DeleteObject();m_SurfaceBrush.CreateSolidBrush(ProfilesView::Surface());
    m_SubtleBrush.DeleteObject();m_SubtleBrush.CreateSolidBrush(ProfilesView::Subtle());
    m_DirtyBrush.DeleteObject();m_DirtyBrush.CreateSolidBrush(ProfilesView::Warning());
    ::SetWindowTheme(m_Search,ProfilesView::Dark()?L"":nullptr,ProfilesView::Dark()?L"":nullptr);
    m_Search.SendMessageW(EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(ProfilesView::Dark()?L"":L"Find a profile"));
    for(CWnd* child=GetWindow(GW_CHILD);child;child=child->GetNextWindow())child->SetFont(&m_Font);
    for(auto& [id,label]:m_Labels)label->SetFont(id==IDC_PROFILE_TITLE?&m_TitleFont:
        id==IDC_PROFILE_TRIGGER_TITLE?&m_BoldFont:&m_SmallFont);
    m_Reason.SetFont(&m_SmallFont);m_Result.SetFont(&m_BoldFont);m_Effective.SetFont(&m_BoldFont);
    m_Profiles.dpi=dpi;m_Profiles.normal=&m_Font;m_Profiles.smallFont=&m_SmallFont;m_Profiles.bold=&m_BoldFont;
    m_Global.SetItemHeight(-1,MulDiv(24,static_cast<int>(dpi),96));m_Global.SetItemHeight(0,MulDiv(24,static_cast<int>(dpi),96));
    m_Profiles.UpdateHeights();m_DeviceTable.SetMetrics(dpi,&m_Font,&m_SmallFont,&m_BoldFont);Layout();
}

void CProfilesPage::Layout()
{
    if(!m_DeviceTable.GetSafeHwnd())return;
    CRect client;GetClientRect(client);auto s=[&](int v){return MulDiv(v,static_cast<int>(m_Dpi),96);};
    int width=MulDiv(client.Width(),96,static_cast<int>(m_Dpi)),height=MulDiv(client.Height(),96,static_cast<int>(m_Dpi));
    auto place=[&](int id,int x,int y,int w,int h)
    {if(auto control=GetDlgItem(id)){control->SetWindowPos(nullptr,s(x),s(y),s(std::max(0,w)),s(std::max(0,h)),SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);}};
    auto rect=[&](int x,int y,int w,int h){return CRect(s(x),s(y),s(x+w),s(y+h));};
    const int left=304,right=width-40,footer=height-82;
    m_TopCard=rect(16,16,width-32,64);m_EditorCard=rect(280,96,width-296,height-112);
    m_TriggerCard=rect(left,182,right-left,70);m_TableHeader=rect(left,322,right-left,36);
    int mode=width-744;
    place(IDC_PROFILE_EFFECTIVE,54,29,mode-70,22);place(IDC_PROFILE_EFFECTIVE_REASON,54,53,mode-70,19);
    place(IDC_PROFILE_AUTOMATIC,mode,32,124,34);place(IDC_PROFILE_USE_GLOBAL,mode+124,32,124,34);
    place(IDC_PROFILE_GLOBAL_LABEL,mode+274,24,184,18);place(IDC_PROFILE_GLOBAL,mode+274,44,184,180);
    place(IDC_PROFILE_PAUSE,width-230,32,190,34);
    place(IDC_PROFILE_LIST_LABEL,28,108,120,24);place(IDC_PROFILE_NEW_MENU,184,103,72,32);
    place(IDC_PROFILE_SEARCH,24,150,232,34);place(IDC_PROFILE_LIST,16,192,248,height-374);
    place(IDC_PROFILE_IMPORT,24,height-169,232,28);place(IDC_PROFILE_BACKUP,24,height-136,232,28);
    place(IDC_PROFILE_OPEN_FOLDER,24,height-96,232,30);place(IDC_PROFILE_SETTINGS,24,height-52,232,30);
    GetDlgItem(IDC_PROFILE_IMPORT)->SetWindowTextW(L"Import profiles...");GetDlgItem(IDC_PROFILE_BACKUP)->SetWindowTextW(L"Back up all profiles...");GetDlgItem(IDC_PROFILE_OPEN_FOLDER)->SetWindowTextW(L"Open profiles folder");
    CClientDC measure(this);auto oldFont=measure.SelectObject(&m_TitleFont);CString title;m_Labels.at(IDC_PROFILE_TITLE)->GetWindowTextW(title);
    int titleWidth=std::clamp(MulDiv(measure.GetTextExtent(title).cx,96,static_cast<int>(m_Dpi))+6,80,std::max(80,right-left-330));measure.SelectObject(oldFont);
    place(IDC_PROFILE_TITLE,left,111,titleWidth,36);place(IDC_PROFILE_DIRTY,left+titleWidth+12,121,136,23);
    if(!Dirty())GetDlgItem(IDC_PROFILE_DIRTY)->ShowWindow(SW_HIDE);
    place(IDC_PROFILE_MENU,right-126,116,126,33);place(IDC_PROFILE_SUBTITLE,left,153,right-left,23);
    place(IDC_PROFILE_TRIGGER_TITLE,left+16,196,right-left-184,20);
    place(IDC_PROFILE_TRIGGER_STATE,left+16,225,right-left-300,19);
    place(IDC_PROFILE_VIEW_PATH,right-266,219,105,30);place(IDC_PROFILE_CHANGE_APP,right-150,213,132,34);
    place(IDC_PROFILE_DEVICES_LABEL,left,270,125,24);place(IDC_PROFILE_DEVICE_COUNT,left+132,274,right-left-132,20);
    place(IDC_PROFILE_DEVICE_HELP,left,298,right-left,20);
    place(IDC_PROFILE_HEADER_DEVICE,left+18,333,right-left-448,20);
    place(IDC_PROFILE_HEADER_CURRENT,right-415,333,140,20);place(IDC_PROFILE_HEADER_AFTER,right-265,333,210,20);
    place(IDC_PROFILE_DEVICE_TABLE,left,358,right-left,std::max(80,footer-400));
    CRect viewport;m_DeviceTable.GetClientRect(viewport);int tableWidth=MulDiv(viewport.Width(),96,static_cast<int>(m_Dpi));
    place(IDC_PROFILE_HEADER_CURRENT,left+tableWidth-398,333,140,20);place(IDC_PROFILE_HEADER_AFTER,left+tableWidth-248,333,210,20);
    place(IDC_PROFILE_ALLOWED_HINT,left,footer-33,std::max(0,right-left-510),20);
    if(width<1180)GetDlgItem(IDC_PROFILE_ALLOWED_HINT)->ShowWindow(SW_HIDE);
    place(IDC_PROFILE_ALLOWED_APPS,right-498,footer-41,260,30);place(IDC_PROFILE_ADD_REMEMBERED,right-228,footer-41,228,30);
    auto statusWidth=right-left-(m_RetryAvailable?388:258);
    place(IDC_PROFILE_RESULT,left,footer+8,statusWidth,34);place(IDC_PROFILE_SAVE_HINT,left,footer+46,statusWidth,18);
    place(IDC_PROFILE_DISCARD,right-230,footer+22,106,36);place(IDC_PROFILE_APPLY,right-112,footer+22,112,36);
    // Recovery is contextual, never an empty permanent slot in the footer.
    if(m_RetryAvailable){place(IDC_PROFILE_RETRY,right-360,footer+22,122,36);}else m_Retry.ShowWindow(SW_HIDE);
    // Explicit visual tab sequence; native dialog navigation descends into radio groups.
    HWND previous=HWND_TOP;
    for(auto id:{IDC_PROFILE_AUTOMATIC,IDC_PROFILE_USE_GLOBAL,IDC_PROFILE_GLOBAL,IDC_PROFILE_PAUSE,IDC_PROFILE_NEW_MENU,
        IDC_PROFILE_SEARCH,IDC_PROFILE_LIST,IDC_PROFILE_IMPORT,IDC_PROFILE_BACKUP,IDC_PROFILE_OPEN_FOLDER,IDC_PROFILE_SETTINGS,
        IDC_PROFILE_MENU,IDC_PROFILE_VIEW_PATH,IDC_PROFILE_CHANGE_APP,IDC_PROFILE_DEVICE_TABLE,IDC_PROFILE_ALLOWED_APPS,IDC_PROFILE_ADD_REMEMBERED,
        IDC_PROFILE_RETRY,IDC_PROFILE_DISCARD,IDC_PROFILE_APPLY})
    {auto control=GetDlgItem(id);if(control){::SetWindowPos(control->m_hWnd,previous,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);previous=control->m_hWnd;}}
    Invalidate(FALSE);
}

BOOL CProfilesPage::OnEraseBkgnd(CDC*) { return TRUE; }
void CProfilesPage::OnPaint()
{
    CPaintDC dc(this);CRect client;GetClientRect(client);dc.FillSolidRect(client,ProfilesView::Background());
    CPen pen(PS_SOLID,1,ProfilesView::Line());auto oldPen=dc.SelectObject(&pen);CBrush brush(ProfilesView::Surface());auto oldBrush=dc.SelectObject(&brush);
    dc.RoundRect(m_TopCard,CPoint(9,9));dc.RoundRect(m_EditorCard,CPoint(9,9));
    auto s=[&](int value){return MulDiv(value,static_cast<int>(m_Dpi),96);};
    CBrush indicator(m_Coordinator.EffectiveSelectionVerified()?ProfilesView::Success():ProfilesView::WarningText());dc.SelectObject(&indicator);dc.Ellipse(s(34),s(34),s(44),s(44));
    CBrush subtle(ProfilesView::Subtle());dc.SelectObject(&subtle);dc.RoundRect(m_TriggerCard,CPoint(7,7));dc.Rectangle(m_TableHeader);
    auto footer=client.bottom-MulDiv(82,static_cast<int>(m_Dpi),96);dc.FillSolidRect(m_EditorCard.left,footer,m_EditorCard.Width(),1,ProfilesView::Line());
    dc.SelectObject(oldBrush);dc.SelectObject(oldPen);
}
HBRUSH CProfilesPage::OnCtlColor(CDC* dc,CWnd* window,UINT type)
{
    int id=window->GetDlgCtrlID();bool rail=id==IDC_PROFILE_LIST||id==IDC_PROFILE_LIST_LABEL;
    bool subtle=id==IDC_PROFILE_TRIGGER_TITLE||id==IDC_PROFILE_TRIGGER_STATE||id==IDC_PROFILE_HEADER_DEVICE||id==IDC_PROFILE_HEADER_CURRENT||id==IDC_PROFILE_HEADER_AFTER;
    dc->SetBkMode(TRANSPARENT);dc->SetBkColor(rail?ProfilesView::Background():ProfilesView::Surface());dc->SetTextColor(id==IDC_PROFILE_DIRTY?ProfilesView::WarningText():
        type==CTLCOLOR_EDIT||id==IDC_PROFILE_TITLE||id==IDC_PROFILE_EFFECTIVE||id==IDC_PROFILE_RESULT||id==IDC_PROFILE_TRIGGER_TITLE?ProfilesView::Ink():ProfilesView::Muted());
    if(type==CTLCOLOR_EDIT||type==CTLCOLOR_LISTBOX&&!rail)return m_SurfaceBrush;
    if(subtle)return m_SubtleBrush;
    if(id==IDC_PROFILE_DIRTY)return m_DirtyBrush;
    return rail?m_BackgroundBrush:m_SurfaceBrush;
}
LRESULT CProfilesPage::OnDpiChanged(WPARAM value,LPARAM) {SetDpi(LOWORD(value));return 0;}
void CProfilesPage::OnSysColorChange(){CDialogEx::OnSysColorChange();OnThemeChanged(0,0);}
LRESULT CProfilesPage::OnThemeChanged(WPARAM,LPARAM)
{
    SetDpi(m_Dpi);
    // A theme change must not reload the editor, touch drafts, or invoke enforcement.
    RedrawWindow(nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);return 0;
}

void CProfilesPage::UpdateRail()
{
    if(!m_Profiles.GetSafeHwnd())return; m_Profiles.items.clear();auto const& profiles=m_Coordinator.Snapshot().profiles;
    std::optional<HidHide::Profiles::Kind> previous;
    for(auto const& id:m_ProfileIds)
    {
        auto found=profiles.find(id);if(found==profiles.end())continue;auto const& profile=found->second;
        auto full=ProfileRowText(id,profile);auto offset=full.find(L" \u2014 ");auto state=offset==std::wstring::npos?full:full.substr(offset+3);
        bool first=!previous||*previous!=profile.kind;int count=static_cast<int>(std::count_if(m_ProfileIds.begin(),m_ProfileIds.end(),[&](auto const& key){auto item=profiles.find(key);return item!=profiles.end()&&item->second.kind==profile.kind;}));
        m_Profiles.items.push_back({profile.name,state,first?(profile.kind==HidHide::Profiles::Kind::Application?L"APPLICATION PROFILES":L"GLOBAL PROFILES"):L"",count,m_Draft&&m_Draft->id==id&&Dirty()});previous=profile.kind;
    }
    m_Profiles.UpdateHeights();
    for(size_t i{};i<m_Profiles.items.size();++i){auto const& row=m_Profiles.items[i];ProfilesView::AccessibleName(m_Profiles,row.name+L". "+row.state+(row.dirty?L". Unsaved changes":L""),static_cast<DWORD>(i+1));}
}

void CProfilesPage::UpdateView()
{
    if(m_Labels.empty())return;
    auto label=[&](int id,std::wstring const& text){m_Labels.at(id)->SetWindowTextW(text.c_str());};
    label(IDC_PROFILE_TITLE,m_Draft?m_Draft->name:L"No matching profiles");label(IDC_PROFILE_DIRTY,Dirty()?L"Unsaved changes":L"");
    bool application=m_Draft&&m_Draft->kind==HidHide::Profiles::Kind::Application;
    label(IDC_PROFILE_TRIGGER_TITLE,application?L"Activate automatically when this application runs":L"Global profile \u2014 complete device visibility policy");
    label(IDC_PROFILE_TRIGGER_STATE,application?m_Draft->executable.filename().native()+L" \u00b7 "+
        (m_Coordinator.IsVerifiedRunning(m_Draft->id)?L"Running \u2014 exact executable match":m_Coordinator.IsApplicationMissing(m_Draft->id)?L"App missing":L"Waiting for exact executable match"):
        L"Used as the Automatic fallback or selected with Use Global.");
    m_StyledButtons.at(IDC_PROFILE_VIEW_PATH)->EnableWindow(application);
    m_StyledButtons.at(IDC_PROFILE_AUTOMATIC)->SetCheck(m_DraftSettings.mode==HidHide::Profiles::Mode::Automatic);
    m_StyledButtons.at(IDC_PROFILE_USE_GLOBAL)->SetCheck(m_DraftSettings.mode==HidHide::Profiles::Mode::UseGlobal);
    for(auto id:{IDC_PROFILE_AUTOMATIC,IDC_PROFILE_USE_GLOBAL,IDC_PROFILE_NEW_MENU,IDC_PROFILE_ADD_REMEMBERED})m_StyledButtons.at(id)->EnableWindow(m_RepositoryWritable);
    m_StyledButtons.at(IDC_PROFILE_MENU)->EnableWindow(m_Draft.has_value());
    auto active=m_Draft&&m_Coordinator.EffectiveSelection().profileId==m_Draft->id;
    auto activationChanged=application&&(!m_Saved||m_Draft->enabled!=m_Saved->enabled||m_Draft->priority!=m_Saved->priority||m_Draft->executable!=m_Saved->executable);
    label(IDC_PROFILE_HEADER_AFTER,active?L"AFTER APPLY":L"WHEN ACTIVE");
    label(IDC_PROFILE_SAVE_HINT,!Dirty()?L"Saved settings. Apply becomes available when you make a change.":
        !(m_DraftSettings==m_SavedSettings)?L"Apply saves settings and reconciles the selected policy.":activationChanged?L"Apply saves; activation is checked against running applications.":active?L"Apply saves this profile and updates its active policy.":L"Saves only; this profile is not active.");
    GetDlgItem(IDC_PROFILE_ALLOWED_APPS)->SetWindowTextW((L"Manage allowed apps ("+std::to_wstring(m_DraftSettings.allowedApplications.size())+L")").c_str());
    std::vector<ProfilesView::Device> devices;size_t connected{};
    auto less=[](std::wstring const& a,std::wstring const& b){return _wcsicmp(a.c_str(),b.c_str())<0;};
    std::map<std::wstring,HidHide::Profiles::DeviceRule const*,decltype(less)> savedRules(less),draftRules(less);
    if(m_Saved)for(auto const& rule:m_Saved->rules)savedRules.emplace(rule.identity,&rule);
    if(m_Draft)for(auto const& rule:m_Draft->rules)draftRules.emplace(rule.identity,&rule);
    for(auto const& row:m_DeviceRows)
    {
        size_t count{};bool changed{};
        for(auto const& path:row.policyIdentities)
        {
            auto saved=savedRules.find(path),draft=draftRules.find(path);
            if(draft!=draftRules.end()&&draft->second->visibility==HidHide::Profiles::Visibility::Hidden)++count;
            changed|=(saved==savedRules.end())!=(draft==draftRules.end())||
                (saved!=savedRules.end()&&draft!=draftRules.end()&&!(*saved->second==*draft->second));
        }
        auto state=count==0?0:count==row.policyIdentities.size()?1:2;
        auto split=row.friendly.find(L" \u2014 ");auto name=row.friendly.substr(0,split);auto type=split==std::wstring::npos?L"HID controller":row.friendly.substr(split+3);
        auto detail=!row.connectionKnown?L"Connection unknown":row.connected?L"Connected \u00b7 "+type:L"Disconnected \u00b7 Rule kept for reconnection";
        auto current=!row.currently?L"Unknown":!row.connected&&row.connectionKnown?L"Not connected":*row.currently==RowVisibility::Hidden?L"Hidden":*row.currently==RowVisibility::Mixed?L"Mixed":L"Visible";
        devices.push_back({name,detail,current,row.identity,state,changed,m_RepositoryWritable&&m_Draft.has_value()});connected+=row.connected;
    }
    label(IDC_PROFILE_DEVICE_COUNT,std::to_wstring(connected)+L" connected \u00b7 "+std::to_wstring(devices.size()-connected)+L" disconnected");
    m_DeviceTable.Update(devices);m_StyledButtons.at(IDC_PROFILE_AUTOMATIC)->Invalidate(FALSE);m_StyledButtons.at(IDC_PROFILE_USE_GLOBAL)->Invalidate(FALSE);
}

void CProfilesPage::ChooseVisibility(size_t index,bool hidden)
{
    if(index>=m_DeviceRows.size()||!m_RepositoryWritable)return;
    m_Devices.SetItemState(static_cast<int>(index),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    m_Hidden.SetCheck(hidden?BST_CHECKED:BST_UNCHECKED);m_Visible.SetCheck(hidden?BST_UNCHECKED:BST_CHECKED);OnVisibility();
}
void CProfilesPage::ShowDeviceDetails(size_t index)
{
    if(index>=m_DeviceRows.size())return;auto const& row=m_DeviceRows[index];std::wstring text=row.friendly+L"\n\nExact policy identities:";for(auto const& id:row.policyIdentities)text+=L"\n"+id;
    MessageBoxW(text.c_str(),L"Device details",MB_OK|MB_ICONINFORMATION);
}
BOOL CProfilesPage::PreTranslateMessage(MSG* message)
{
    if(message->message==WM_KEYDOWN&&message->wParam=='S'&&(::GetKeyState(VK_CONTROL)&0x8000)){if(m_Apply.IsWindowEnabled())OnApply();return TRUE;}
    if(m_DeviceTable.Keyboard(*message))return TRUE;
    return CDialogEx::PreTranslateMessage(message);
}
void CProfilesPage::OnAutomatic(){m_Mode.SetCurSel(0);OnModeChanged();}
void CProfilesPage::OnUseGlobal(){m_Mode.SetCurSel(1);OnModeChanged();}
void CProfilesPage::OnViewPath(){if(m_Draft&&m_Draft->kind==HidHide::Profiles::Kind::Application)MessageBoxW(m_Draft->executable.c_str(),L"Exact application executable path",MB_OK|MB_ICONINFORMATION);}
void CProfilesPage::OnNewMenu()
{
    CMenu menu;menu.CreatePopupMenu();menu.AppendMenuW(MF_STRING,1,L"Application profile...");menu.AppendMenuW(MF_STRING,2,L"Global profile");
    CRect rect;GetDlgItem(IDC_PROFILE_NEW_MENU)->GetWindowRect(rect);auto command=menu.TrackPopupMenu(TPM_RETURNCMD,rect.left,rect.bottom,this);if(command==1)OnNewApplication();else if(command==2)OnNewGlobal();
}

namespace
{
    class ProfileTextPrompt final:public CDialogEx
    {
    public:
        ProfileTextPrompt(CWnd* parent,wchar_t const* title,wchar_t const* first,std::wstring value,wchar_t const* second=L""):
            CDialogEx(IDD_PROFILE_TEXT_PROMPT,parent),caption(title),label1(first),label2(second),value1(value.c_str()){}
        CString value1,value2;
    protected:
        BOOL OnInitDialog() override {CDialogEx::OnInitDialog();SetWindowTextW(caption);SetDlgItemTextW(IDC_PROFILE_PROMPT_LABEL1,label1);SetDlgItemTextW(IDC_PROFILE_PROMPT_VALUE1,value1);SetDlgItemTextW(IDC_PROFILE_PROMPT_LABEL2,label2);
            if(label2.IsEmpty()){GetDlgItem(IDC_PROFILE_PROMPT_LABEL2)->ShowWindow(SW_HIDE);GetDlgItem(IDC_PROFILE_PROMPT_VALUE2)->ShowWindow(SW_HIDE);}
            ok.SubclassDlgItem(IDOK,this);cancel.SubclassDlgItem(IDCANCEL,this);ok.prominent=true;
            RefreshTheme();observer=std::make_unique<ProfilesView::ThemeObserver>(m_hWnd);return TRUE;}
        void OnOK() override {GetDlgItemTextW(IDC_PROFILE_PROMPT_VALUE1,value1);GetDlgItemTextW(IDC_PROFILE_PROMPT_VALUE2,value2);if(value1.IsEmpty())return;CDialogEx::OnOK();}
        LRESULT WindowProc(UINT message,WPARAM wParam,LPARAM lParam) override
        {
            if(message==WM_DESTROY)observer.reset();
            if(message==ProfilesView::ThemeChangedMessage||message==WM_SETTINGCHANGE||message==WM_SYSCOLORCHANGE)
            {ProfilesView::RefreshTheme();RefreshTheme();}
            if((message==WM_CTLCOLORDLG||message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT)&&surface.GetSafeHandle())
            {auto dc=reinterpret_cast<HDC>(wParam);::SetTextColor(dc,ProfilesView::Ink());::SetBkColor(dc,ProfilesView::Surface());return reinterpret_cast<LRESULT>(surface.GetSafeHandle());}
            return CDialogEx::WindowProc(message,wParam,lParam);
        }
    private:
        void RefreshTheme()
        {
            surface.DeleteObject();surface.CreateSolidBrush(ProfilesView::Surface());ProfilesView::ApplyWindowTheme(m_hWnd);
            for(auto id:{IDC_PROFILE_PROMPT_VALUE1,IDC_PROFILE_PROMPT_VALUE2})
                ::SetWindowTheme(GetDlgItem(id)->m_hWnd,ProfilesView::Dark()?L"":nullptr,ProfilesView::Dark()?L"":nullptr);
            RedrawWindow(nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
        }
        CBrush surface;ProfilesView::Button ok,cancel;
        std::unique_ptr<ProfilesView::ThemeObserver> observer;
        CString caption,label1,label2;
    };
}
void CProfilesPage::OnProfileMenu()
{
    if(!m_Draft)return;CMenu menu;menu.CreatePopupMenu();bool app=m_Draft->kind==HidHide::Profiles::Kind::Application;
    auto edit=MF_STRING|(m_RepositoryWritable?0:MF_GRAYED);
    menu.AppendMenuW(edit,1,L"Rename...");menu.AppendMenuW(edit,2,L"Duplicate...");menu.AppendMenuW(edit,3,app?L"Activation priority...":L"Make this the Global selection");
    menu.AppendMenuW(edit|(m_Draft->enabled?MF_CHECKED:0),4,L"Enabled");menu.AppendMenuW(MF_SEPARATOR);
    menu.AppendMenuW(MF_STRING|(m_Saved?0:MF_GRAYED),5,L"Export saved profile...");menu.AppendMenuW(MF_STRING|(m_Saved?0:MF_GRAYED),6,L"Open JSON");menu.AppendMenuW(edit,7,L"Delete profile...");
    CRect rect;GetDlgItem(IDC_PROFILE_MENU)->GetWindowRect(rect);auto command=menu.TrackPopupMenu(TPM_RETURNCMD,rect.left,rect.bottom,this);
    if(command==1){ProfileTextPrompt prompt(this,L"Rename profile",L"Profile name",m_Draft->name);if(prompt.DoModal()==IDOK)m_Name.SetWindowTextW(prompt.value1);}
    else if(command==2){if(!ConfirmAbandon(L"duplicating this profile"))return;auto copy=*m_Draft;copy.id=HidHide::Profiles::NewStableId();copy.revision=0;copy.name+=L" (copy)";copy.enabled=false;m_Saved.reset();m_Expected.reset();m_Draft=copy;m_DeleteStaged=false;RefreshDraftControls();}
    else if(command==3){if(app){ProfileTextPrompt prompt(this,L"Activation priority",L"Highest value wins. Equal values use stable profile ID.",std::to_wstring(m_Draft->priority));if(prompt.DoModal()==IDOK)m_Priority.SetWindowTextW(prompt.value1);}else{m_DraftSettings.selectedGlobalId=m_Draft->id;RefreshDraftControls();}}
    else if(command==4){m_Enabled.SetCheck(m_Enabled.GetCheck()==BST_CHECKED?BST_UNCHECKED:BST_CHECKED);OnDraftChanged();}
    else if(command==5)OnExport();else if(command==6)OnOpenJson();else if(command==7)OnDelete();
}
void CProfilesPage::OnAddRemembered()
{
    if(!m_Draft||!m_RepositoryWritable)return;ProfileTextPrompt prompt(this,L"Add remembered device",L"Exact device instance identity (not a friendly-name match)",L"",L"Friendly name");if(prompt.DoModal()!=IDOK)return;
    std::wstring identity(prompt.value1),friendly(prompt.value2);if(identity.empty()||identity.size()>4096||identity.find(L'\\')==std::wstring::npos){MessageBoxW(L"Enter a complete device instance identity.",L"Device identity required",MB_OK|MB_ICONWARNING);return;}
    if(std::any_of(m_Draft->rules.begin(),m_Draft->rules.end(),[&](auto const& rule){return _wcsicmp(rule.identity.c_str(),identity.c_str())==0;})){MessageBoxW(L"This exact identity already has a rule.",L"Remembered device",MB_OK);return;}
    m_Draft->rules.push_back({identity,friendly.empty()?identity:friendly,HidHide::Profiles::Visibility::Visible});RefreshDevices();RefreshDirtyState();
}

bool CProfilesPage::AcceptancePresentation(bool dirty,UINT dpi,int width,int height)
{
    if(!m_Coordinator.AcceptanceScanNow())throw std::runtime_error("presentation scan failed");
    auto found=std::find_if(m_Coordinator.Snapshot().profiles.begin(),m_Coordinator.Snapshot().profiles.end(),[](auto const& item){return item.second.name==L"F1 25";});
    if(found==m_Coordinator.Snapshot().profiles.end())throw std::runtime_error("presentation fixture missing");
    RefreshProfiles(found->first);RefreshStatus();SetDpi(dpi);
    CRect bounds(0,0,MulDiv(width,static_cast<int>(dpi),96),MulDiv(height,static_cast<int>(dpi),96));auto parent=GetParent();
    ::AdjustWindowRectExForDpi(&bounds,static_cast<DWORD>(parent->GetStyle()),FALSE,static_cast<DWORD>(parent->GetExStyle()),::GetDpiForWindow(parent->m_hWnd));
    parent->SetWindowPos(nullptr,0,0,bounds.Width(),bounds.Height(),SWP_NOZORDER|SWP_NOACTIVATE);
    auto before=m_Application.Version(found->first);auto settingsBefore=m_Application.SettingsVersion();
    if(m_Apply.IsWindowEnabled()||Dirty())throw std::runtime_error("clean Apply was enabled");
    if(m_DeviceTable.RowCount()!=4)throw std::runtime_error("device rows missing");
    // The native inline radio command must edit exactly one row and never save.
    ::SendMessageW(m_DeviceTable.Radio(1,true),BM_CLICK,0,0);
    CString result;m_Result.GetWindowTextW(result);
    if(!Dirty()||!m_Apply.IsWindowEnabled()||result.Find(L"1 change ready to Apply")<0)throw std::runtime_error("inline radio dirty count incorrect");
    IAccessible* accessible{};auto radio=m_DeviceTable.Radio(1,true);
    if(FAILED(::AccessibleObjectFromWindow(radio,static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(&accessible))))throw std::runtime_error("inline radio accessibility unavailable");
    VARIANT self{};self.vt=VT_I4;self.lVal=CHILDID_SELF;BSTR name{};VARIANT role{};VARIANT state{};
    auto named=accessible->get_accName(self,&name),roled=accessible->get_accRole(self,&role),stated=accessible->get_accState(self,&state);
    bool semantics=SUCCEEDED(named)&&name&&std::wstring(name).find(L"Heusinkveld Sprint")!=std::wstring::npos&&std::wstring(name).find(L"Unsaved")!=std::wstring::npos
        &&SUCCEEDED(roled)&&role.vt==VT_I4&&role.lVal==ROLE_SYSTEM_RADIOBUTTON&&SUCCEEDED(stated)&&state.vt==VT_I4&&(state.lVal&STATE_SYSTEM_CHECKED);
    ::SysFreeString(name);::VariantClear(&role);::VariantClear(&state);accessible->Release();if(!semantics)throw std::runtime_error("inline radio semantic name/role/state failed");
    ::SetFocus(radio);MSG arrow{};arrow.hwnd=radio;arrow.message=WM_KEYDOWN;arrow.wParam=VK_RIGHT;
    if(!::IsDialogMessageW(parent->m_hWnd,&arrow)||Dirty())throw std::runtime_error("native radio arrow did not restore Visible");
    ::SendMessageW(m_DeviceTable.Radio(1,true),BM_CLICK,0,0);
    if(m_Application.Version(found->first).sha256!=before.sha256||m_Application.SettingsVersion().sha256!=settingsBefore.sha256)throw std::runtime_error("inline draft wrote repository");
    auto originalTheme=ProfilesView::Dark();auto detached=*m_Draft;auto detachedSettings=m_DraftSettings;
    auto writes=m_Coordinator.RepositoryWriteCount();auto focus=::GetFocus();
    for(auto dark:{true,false,originalTheme})
    {
        ProfilesView::OverrideThemeForAcceptance(dark?1:0);
        parent->SendMessageW(ProfilesView::ThemeChangedMessage);
        if(ProfilesView::Dark()!=(dark&&!ProfilesView::HighContrast()))throw std::runtime_error("theme selection failed");
        if(!Dirty()||!m_Apply.IsWindowEnabled()||!(*m_Draft==detached)||!(m_DraftSettings==detachedSettings)
            ||m_Coordinator.RepositoryWriteCount()!=writes||::GetFocus()!=focus)throw std::runtime_error("theme change modified draft or focus");
        CClientDC colors(this);auto brush=OnCtlColor(&colors,&m_Search,CTLCOLOR_EDIT);LOGBRUSH description{};
        ::GetObjectW(brush,sizeof(description),&description);
        if(description.lbColor!=ProfilesView::Surface()||colors.GetBkColor()!=ProfilesView::Surface()||colors.GetTextColor()!=ProfilesView::Ink())throw std::runtime_error("search theme foreground/background mismatch");
        if(ProfilesView::Dark()&&(GetRValue(ProfilesView::Surface())>=80||GetRValue(ProfilesView::Ink())<=180))throw std::runtime_error("dark theme palette unreadable");
        BOOL captionDark{};
        if(FAILED(::DwmGetWindowAttribute(parent->m_hWnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&captionDark,sizeof(captionDark)))
            ||bool(captionDark)!=ProfilesView::Dark())throw std::runtime_error("native caption theme did not follow editor");
        if(ProfilesView::Dark())
        {
            auto luminance=[](COLORREF color){auto linear=[](int channel){double value=channel/255.0;return value<=0.04045?value/12.92:std::pow((value+0.055)/1.055,2.4);};
                return 0.2126*linear(GetRValue(color))+0.7152*linear(GetGValue(color))+0.0722*linear(GetBValue(color));};
            auto contrast=[&](COLORREF first,COLORREF second){auto a=luminance(first),b=luminance(second);return (std::max(a,b)+0.05)/(std::min(a,b)+0.05);};
            for(auto pair:{std::pair{ProfilesView::Ink(),ProfilesView::Surface()},std::pair{ProfilesView::Muted(),ProfilesView::Surface()},
                std::pair{ProfilesView::Muted(),ProfilesView::Changed()},std::pair{ProfilesView::Accent(),ProfilesView::Selected()},
                std::pair{ProfilesView::WarningText(),ProfilesView::Warning()},std::pair{ProfilesView::AccentText(),ProfilesView::Accent()}})
                if(contrast(pair.first,pair.second)<4.5)throw std::runtime_error("dark theme text contrast below 4.5:1");
            if(contrast(ProfilesView::Line(),ProfilesView::Surface())<3.0||contrast(ProfilesView::Line(),ProfilesView::Background())<3.0)
                throw std::runtime_error("dark theme control boundary contrast below 3:1");
        }
    }
    if(m_Application.Version(found->first).sha256!=before.sha256||m_Application.SettingsVersion().sha256!=settingsBefore.sha256)throw std::runtime_error("theme change wrote repository");
    auto keyboardApply=[&]
    {
        BYTE previous[256]{},pressed[256]{};if(!::GetKeyboardState(previous))throw std::runtime_error("thread keyboard state unavailable");
        memcpy(pressed,previous,sizeof(pressed));pressed[VK_CONTROL]=0x80;::SetKeyboardState(pressed);
        MSG save{};save.hwnd=m_hWnd;save.message=WM_KEYDOWN;save.wParam='S';auto handled=PreTranslateMessage(&save);::SetKeyboardState(previous);
        if(!handled||Dirty()||m_Apply.IsWindowEnabled())throw std::runtime_error("Ctrl+S did not use the real Apply command");
    };
    keyboardApply();ChooseVisibility(1,false);keyboardApply();ChooseVisibility(1,true);
    if(height==680)
    {
        // Partial-row focus must never recycle the HWND between mouse-down and click.
        m_DeviceTable.SendMessageW(WM_VSCROLL,MAKEWPARAM(SB_LINEDOWN,0),0);
        auto target=m_DeviceTable.Radio(3,false);if(!target)throw std::runtime_error("partial remembered row unavailable");
        CRect viewport,targetBounds;m_DeviceTable.GetClientRect(viewport);::GetWindowRect(target,&targetBounds);m_DeviceTable.ScreenToClient(targetBounds);
        if(targetBounds.top>=viewport.bottom||targetBounds.bottom<=viewport.bottom)throw std::runtime_error("partial-row test did not exercise a clipped action");
        auto position=m_DeviceTable.ScrollPosition();::SetFocus(target);
        if(position!=m_DeviceTable.ScrollPosition())throw std::runtime_error("focus rebound a partial row");
        ::SendMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(5,1));::SendMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(5,1));
        auto xbox=std::find_if(m_Draft->rules.begin(),m_Draft->rules.end(),[](auto const& rule){return rule.identity==L"HID\\CONCEPT_XBOX";});
        if(xbox==m_Draft->rules.end()||xbox->visibility!=HidHide::Profiles::Visibility::Visible)throw std::runtime_error("partial row command targeted the wrong device");
        ::SendMessageW(m_DeviceTable.Radio(3,true),BM_CLICK,0,0);
        m_DeviceTable.SendMessageW(WM_VSCROLL,SB_BOTTOM,0);
        if(::GetFocus()!=m_DeviceTable.m_hWnd)throw std::runtime_error("recycling silently retained focus on another device");
        m_DeviceTable.SendMessageW(WM_VSCROLL,SB_TOP,0);
    }
    if(!dirty)OnDiscard();
    Layout();
    CRect client;GetClientRect(client);
    std::vector<std::pair<int,CRect>> visibleBounds;
    for(CWnd* child=GetWindow(GW_CHILD);child;child=child->GetNextWindow())
    {
        if(!(child->GetStyle()&WS_VISIBLE))continue;CRect rect;child->GetWindowRect(rect);ScreenToClient(rect);
        // Dropdown bounds include the popup; its collapsed native portion is in the strip.
        if(child->GetDlgCtrlID()==IDC_PROFILE_GLOBAL)rect.bottom=rect.top+MulDiv(30,static_cast<int>(dpi),96);
        if(rect.left<0||rect.top<0||rect.right>client.right||rect.bottom>client.bottom||rect.Width()==0||rect.Height()==0)throw std::runtime_error("visible layout control outside client");
        visibleBounds.emplace_back(child->GetDlgCtrlID(),rect);
    }
    for(size_t i{};i<visibleBounds.size();++i)for(size_t j=i+1;j<visibleBounds.size();++j)
    {CRect overlap;if(overlap.IntersectRect(visibleBounds[i].second,visibleBounds[j].second)&&overlap.Width()>1&&overlap.Height()>1)throw std::runtime_error("visible layout controls overlap: "+std::to_string(visibleBounds[i].first)+"/"+std::to_string(visibleBounds[j].first));}
    CRect table,footer;m_DeviceTable.GetWindowRect(table);m_Apply.GetWindowRect(footer);if(table.bottom>=footer.top)throw std::runtime_error("table overlaps sticky Apply footer");
    m_DeviceTable.SendMessageW(WM_VSCROLL,SB_BOTTOM,0);CRect sticky;m_Apply.GetWindowRect(sticky);if(sticky!=footer)throw std::runtime_error("Apply moved during table scroll");
    m_DeviceTable.SendMessageW(WM_VSCROLL,SB_TOP,0);
    auto originalDraft=m_Draft,originalSaved=m_Saved;auto originalRows=m_DeviceRows;
    m_Draft->rules.clear();m_DeviceRows.clear();
    for(size_t i{};i<4096;++i){auto identity=L"HID\\VIRTUALIZED_"+std::to_wstring(i);m_Draft->rules.push_back({identity,L"Long Unicode device \u2014 \u00c9lite \u65e5\u672c\u8a9e controller",HidHide::Profiles::Visibility::Visible});m_DeviceRows.push_back({identity,m_Draft->rules.back().friendlyName,false,true,RowVisibility::Visible,{identity}});}
    m_Saved=m_Draft;UpdateView();m_DeviceTable.SendMessageW(WM_VSCROLL,SB_BOTTOM,0);
    if(m_DeviceTable.ControlRowCount()>16||m_DeviceTable.ScrollPosition()==0)throw std::runtime_error("device viewport did not bound native controls");
    m_Draft=originalDraft;m_Saved=originalSaved;m_DeviceRows=originalRows;m_DeviceTable.SendMessageW(WM_VSCROLL,SB_TOP,0);UpdateView();
    if(m_Profiles.items.size()!=5||m_Profiles.items[0].section!=L"APPLICATION PROFILES"||m_Profiles.items[3].section!=L"GLOBAL PROFILES")throw std::runtime_error("profile grouping incorrect");
    auto sample=std::wstring(L"Em dash — Élite 日本語");if(sample[8]!=0x2014)throw std::runtime_error("Unicode compilation failure");
    LOGFONTW font{};m_BoldFont.GetLogFont(&font);if(wcscmp(font.lfFaceName,L"Segoe UI")!=0)throw std::runtime_error("wrong native typeface");
    CString resource;if(!resource.LoadStringW(IDS_PROFILE_UNICODE_SAMPLE)||std::wstring(resource)!=sample)throw std::runtime_error("Unicode resource compilation failure");
    Invalidate(TRUE);RedrawWindow(nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);return true;
}
