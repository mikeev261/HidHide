// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
#include "stdafx.h"
#include "HidHideClient.h"
#include "HidHideClientDlg.h"
#include "AppProfilesDlg.h"
#include "Utils.h"
#include "Volume.h"
#include "ConfigurationUi.h"

IMPLEMENT_DYNAMIC(CAppProfilesDlg, CDialogEx)

BEGIN_MESSAGE_MAP(CAppProfilesDlg, CDialogEx)
    ON_LBN_SELCHANGE(IDC_LIST_APP_PROFILES_APPS, &CAppProfilesDlg::OnLbnSelchangeListApps)
    ON_NOTIFY(TVN_ITEMCHANGED, IDC_TREE_APP_PROFILES_DEVICES, &CAppProfilesDlg::OnTvnItemChangedTreeDevices)
    ON_BN_CLICKED(IDC_BUTTON_APP_PROFILES_ADD_APP, &CAppProfilesDlg::OnBnClickedButtonAddApp)
    ON_BN_CLICKED(IDC_BUTTON_APP_PROFILES_DEL_APP, &CAppProfilesDlg::OnBnClickedButtonDelApp)
    ON_BN_CLICKED(IDC_CHECK_APP_PROFILES_GAMING, &CAppProfilesDlg::OnBnClickedDeviceFilter)
    ON_BN_CLICKED(IDC_CHECK_APP_PROFILES_DISCONNECTED, &CAppProfilesDlg::OnBnClickedDeviceFilter)
    ON_WM_SHOWWINDOW()
    ON_WM_TIMER()
END_MESSAGE_MAP()

_Use_decl_annotations_
CAppProfilesDlg::CAppProfilesDlg(CHidHideClientDlg& hidHideClientDlg, CWnd* pParent)
    : CDialogEx(IDD_DIALOG_APP_PROFILES, pParent)
    , HidHide::IDropTarget()
    , m_HidHideClientDlg(hidHideClientDlg)
{}

CAppProfilesDlg::~CAppProfilesDlg() = default;

HidHide::FilterDriverProxy& CAppProfilesDlg::FilterDriverProxy() noexcept
{
    return m_HidHideClientDlg.FilterDriverProxy();
}

_Use_decl_annotations_
void CAppProfilesDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST_APP_PROFILES_APPS, m_AppsList);
    DDX_Control(pDX, IDC_TREE_APP_PROFILES_DEVICES, m_DevicesTree);
    DDX_Control(pDX, IDC_CHECK_APP_PROFILES_GAMING, m_GamingOnly);
    DDX_Control(pDX, IDC_CHECK_APP_PROFILES_DISCONNECTED, m_ShowDisconnected);
    DDX_Control(pDX, IDC_STATIC_APP_PROFILES_PATH, m_ProfilePath);
    DDX_Control(pDX, IDC_STATIC_APP_PROFILES_STATUS, m_ProfileStatus);
}

BOOL CAppProfilesDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    m_GamingOnly.SetCheck(BST_CHECKED);
    m_ShowDisconnected.SetCheck(BST_UNCHECKED);
    SetTimer(1, 1000, nullptr);
    return TRUE;
}

_Use_decl_annotations_
void CAppProfilesDlg::OnShowWindow(BOOL bShow, UINT nStatus)
try
{
    CDialogEx::OnShowWindow(bShow, nStatus);
    if (bShow) RefreshApps();
}
catch (std::runtime_error const& error)
{
    m_Refreshing = false;
    m_ProfileStatus.SetWindowTextW(L"Changes were not saved. Repeat the action to retry.");
    KillTimer(1);
    ReportConfigurationError(error);
    SetTimer(1, 1000, nullptr);
    if (dynamic_cast<HidHide::ConfigurationConflict const*>(&error))
    {
        m_RefreshPending = true;
        m_SavePending = false;
    }
}


_Use_decl_annotations_
std::filesystem::path CAppProfilesDlg::DisplayPath(HidHide::FullImageName const& fullImageName) const
{
    try
    {
        auto const path{ HidHide::FullImageNameToFileName(fullImageName) };
        return path.empty() ? fullImageName : path;
    }
    catch (...)
    {
        return fullImageName;
    }
}

_Use_decl_annotations_
void CAppProfilesDlg::RefreshApps(HidHide::FullImageName const* selectProfile)
{
    if (m_SavePending) UpdateProfileFromTree();
    // Keep retrying an incomplete view refresh even when the profile map is unchanged.
    // In particular, a changed list selection must not leave the previous tree displayed.
    m_RefreshPending = true;
    auto selection{ selectProfile ? std::optional<HidHide::FullImageName>(*selectProfile) : SelectedProfile() };

    auto const profiles{ FilterDriverProxy().GetAppProfiles() };
    m_AppPaths.clear();
    for (auto const& [application, devices] : profiles)
    {
        UNREFERENCED_PARAMETER(devices);
        m_AppPaths.emplace_back(application);
    }

    std::sort(m_AppPaths.begin(), m_AppPaths.end(), [this](auto const& left, auto const& right)
    {
        return _wcsicmp(DisplayPath(left).filename().c_str(), DisplayPath(right).filename().c_str()) < 0;
    });

    m_AppsList.ResetContent();
    int selectedIndex = LB_ERR;
    for (size_t index = 0; index < m_AppPaths.size(); index++)
    {
        auto const display{ DisplayPath(m_AppPaths[index]) };
        auto const duplicateFileName{ std::count_if(m_AppPaths.begin(), m_AppPaths.end(), [this, &display](auto const& candidate)
        {
            return 0 == _wcsicmp(DisplayPath(candidate).filename().c_str(), display.filename().c_str());
        }) > 1 };
        auto const label{ duplicateFileName
            ? display.filename().native() + L" — " + display.parent_path().native()
            : display.filename().native() };
        m_AppsList.AddString(label.c_str());
        if (selection && (*selection == m_AppPaths[index])) selectedIndex = static_cast<int>(index);
    }

    if ((LB_ERR == selectedIndex) && (!m_AppPaths.empty())) selectedIndex = 0;
    if (LB_ERR != selectedIndex) m_AppsList.SetCurSel(selectedIndex);

    RefreshDevices(profiles);
    m_RefreshPending = false;
}

std::optional<HidHide::FullImageName> CAppProfilesDlg::SelectedProfile() const
{
    int const selected{ m_AppsList.GetCurSel() };
    if ((LB_ERR == selected) || (selected < 0) || (static_cast<size_t>(selected) >= m_AppPaths.size())) return std::nullopt;
    return m_AppPaths[static_cast<size_t>(selected)];
}

void CAppProfilesDlg::RefreshDevices(HidHide::AppProfiles const& profiles)
{
    auto deviceItemData = HidHide::HidDevices(false);
    m_Refreshing = true;
    m_DevicesTree.DeleteAllItems();
    m_DisplayedProfiles = profiles;
    m_SavePending = false;

    auto const selectedProfile{ SelectedProfile() };
    m_DisplayedProfile = selectedProfile;
    if (!selectedProfile)
    {
        m_ProfilePath.SetWindowTextW(L"Select an application");
        m_ProfileStatus.SetWindowTextW(L"");
        m_Refreshing = false;
        return;
    }

    auto const displayPath{ DisplayPath(*selectedProfile) };
    m_ProfilePath.SetWindowTextW(displayPath.native().c_str());

    auto const profileIterator{ profiles.find(*selectedProfile) };
    HidHide::DeviceInstancePaths const selectedPaths{ profileIterator == profiles.end() ? HidHide::DeviceInstancePaths{} : profileIterator->second };

    m_Selector.Build(m_DevicesTree, deviceItemData, selectedPaths,
        { 0 != (m_GamingOnly.GetCheck() & BST_CHECKED), 0 == (m_ShowDisconnected.GetCheck() & BST_CHECKED), false },
        DeviceSelectionTree::Presentation::ProfileCheckboxes);

    m_Refreshing = false;
    UpdateStatus();
}

void CAppProfilesDlg::UpdateProfileFromTree()
{
    auto const selectedProfile{ SelectedProfile() };
    if (!selectedProfile) return;

    if (selectedProfile != m_DisplayedProfile) throw HidHide::ConfigurationConflict("View needs refresh");
    auto profiles{ m_DisplayedProfiles };
    auto profileEntry = profiles.find(*selectedProfile);
    if (profileEntry == profiles.end()) throw HidHide::ConfigurationConflict("Profile deleted");
    auto& profilePaths{ profileEntry->second };
    profilePaths = m_Selector.Selection(m_DevicesTree, std::move(profilePaths));

    FilterDriverProxy().SetAppProfiles(m_DisplayedProfiles, profiles);
    m_DisplayedProfiles = std::move(profiles);
    m_Selector.Acknowledge();
    m_SavePending = false;
    UpdateStatus();
}

void CAppProfilesDlg::UpdateStatus()
{
    auto const selectedProfile{ SelectedProfile() };
    if (!selectedProfile) return;

    auto const& profiles{ m_DisplayedProfiles };
    auto const found{ profiles.find(*selectedProfile) };
    size_t selectedInterfaces{};
    if (found != profiles.end())
    {
        selectedInterfaces = m_Selector.SelectedInterfaces(found->second);
    }

    bool const running{ m_HidHideClientDlg.ProfileIsActive(*selectedProfile) };
    std::wostringstream status;
    status << (running ? L"Running" : m_HidHideClientDlg.ProfileIsUnresolved(*selectedProfile) ? L"Path unavailable" : L"Not running") << L" \u2022 " << selectedInterfaces << L" interface" << (1 == selectedInterfaces ? L"" : L"s");
    if (!FilterDriverProxy().GetActive()) status << L" \u2022 hiding disabled";
    m_ProfileStatus.SetWindowTextW(status.str().c_str());
}

void CAppProfilesDlg::OnLbnSelchangeListApps()
try
{
    RefreshApps();
}
catch (std::runtime_error const& error)
{
    m_Refreshing = false;
    m_ProfileStatus.SetWindowTextW(L"Changes were not saved. Repeat the action to retry.");
    KillTimer(1);
    ReportConfigurationError(error);
    SetTimer(1, 1000, nullptr);
    if (dynamic_cast<HidHide::ConfigurationConflict const*>(&error))
    {
        m_RefreshPending = true;
        m_SavePending = false;
    }
}


_Use_decl_annotations_
void CAppProfilesDlg::OnTvnItemChangedTreeDevices(NMHDR* pNMHDR, LRESULT* pResult)
try
{
    *pResult = 0;
    if (m_Refreshing) return;

    auto const& itemChange{ *reinterpret_cast<NMTVITEMCHANGE*>(pNMHDR) };
    if ((itemChange.uStateOld & TVIS_STATEIMAGEMASK) == (itemChange.uStateNew & TVIS_STATEIMAGEMASK)) return;

    m_Refreshing = true;
    bool const changed = m_Selector.Change(m_DevicesTree, itemChange.hItem);
    m_Refreshing = false;
    if (!changed) return;

    m_SavePending = true;
    UpdateProfileFromTree();
}
catch (std::runtime_error const& error)
{
    m_Refreshing = false;
    m_ProfileStatus.SetWindowTextW(L"Changes were not saved. Repeat the action to retry.");
    KillTimer(1);
    ReportConfigurationError(error);
    SetTimer(1, 1000, nullptr);
    if (dynamic_cast<HidHide::ConfigurationConflict const*>(&error))
    {
        m_RefreshPending = true;
        m_SavePending = false;
    }
}


void CAppProfilesDlg::OnBnClickedButtonAddApp()
try
{
    CFileDialog fileDialog(TRUE, L"exe", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"Executables (*.exe)|*.exe|All Files (*.*)|*.*||", this);
    if (IDOK != fileDialog.DoModal()) return;

    auto const profile{ HidHide::FileNameToFullImageName(fileDialog.GetPathName().GetString()) };
    if (profile.empty())
    {
        AfxMessageBox(L"The selected application is not on a supported local volume.", MB_ICONERROR);
        return;
    }

    FilterDriverProxy().AppProfileAdd(profile);
    RefreshApps(&profile);
}
catch (std::runtime_error const& error)
{
    m_Refreshing = false;
    m_ProfileStatus.SetWindowTextW(L"Changes were not saved. Repeat the action to retry.");
    KillTimer(1);
    ReportConfigurationError(error);
    SetTimer(1, 1000, nullptr);
    if (dynamic_cast<HidHide::ConfigurationConflict const*>(&error))
    {
        m_RefreshPending = true;
        m_SavePending = false;
    }
}


void CAppProfilesDlg::OnBnClickedButtonDelApp()
try
{
    auto const profile{ SelectedProfile() };
    if (!profile) return;

    auto profiles = m_DisplayedProfiles;
    profiles.erase(*profile);
    FilterDriverProxy().SetAppProfiles(m_DisplayedProfiles, profiles);
    RefreshApps();
}
catch (std::runtime_error const& error)
{
    m_Refreshing = false;
    m_ProfileStatus.SetWindowTextW(L"Changes were not saved. Repeat the action to retry.");
    KillTimer(1);
    ReportConfigurationError(error);
    SetTimer(1, 1000, nullptr);
    if (dynamic_cast<HidHide::ConfigurationConflict const*>(&error))
    {
        m_RefreshPending = true;
        m_SavePending = false;
    }
}


void CAppProfilesDlg::OnBnClickedDeviceFilter()
try
{
    RefreshApps();
}
catch (std::runtime_error const& error)
{
    m_Refreshing = false;
    m_ProfileStatus.SetWindowTextW(L"Changes were not saved. Repeat the action to retry.");
    KillTimer(1);
    ReportConfigurationError(error);
    SetTimer(1, 1000, nullptr);
    if (dynamic_cast<HidHide::ConfigurationConflict const*>(&error))
    {
        m_RefreshPending = true;
        m_SavePending = false;
    }
}


void CAppProfilesDlg::OnTimer(UINT_PTR nIDEvent)
{
    if ((1 == nIDEvent) && IsWindowVisible())
    {
        try
        {
            if (m_SavePending) UpdateProfileFromTree();
            if (m_RefreshPending || (!m_SavePending && FilterDriverProxy().GetAppProfiles() != m_DisplayedProfiles))
            {
                RefreshApps();
            }
            UpdateStatus();
        }
        catch (HidHide::ConfigurationConflict const& error)
        {
            KillTimer(1);
            ReportConfigurationError(error);
            m_SavePending = false;
            m_RefreshPending = true;
            SetTimer(1, 1000, nullptr);
        }
        catch (std::runtime_error const& error)
        {
            // Timers retry reads without closing the dialog or opening modal boxes.
            LOGEXC_AND_CONTINUE;
            CString status(L"Configuration unavailable or changes not saved; retrying: ");
            status += CString(error.what());
            m_ProfileStatus.SetWindowTextW(status);
        }
    }
    CDialogEx::OnTimer(nIDEvent);
}

_Use_decl_annotations_
DROPEFFECT CAppProfilesDlg::OnDragEnter(CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point)
{
    UNREFERENCED_PARAMETER(pWnd);
    UNREFERENCED_PARAMETER(point);

    m_DropTargetFullImageNames.clear();
    for (auto const& fileName : HidHide::DragTargetFileNames(pDataObject))
    {
        if (!HidHide::FileIsAnApplication(fileName))
        {
            m_DropTargetFullImageNames.clear();
            break;
        }

        auto const fullImageName{ HidHide::FileNameToFullImageName(fileName) };
        if (fullImageName.empty())
        {
            m_DropTargetFullImageNames.clear();
            break;
        }
        m_DropTargetFullImageNames.emplace(fullImageName);
    }

    return HidHide::DragTargetCopyOperation(dwKeyState) && !m_DropTargetFullImageNames.empty() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

_Use_decl_annotations_
DROPEFFECT CAppProfilesDlg::OnDragOver(CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point)
{
    UNREFERENCED_PARAMETER(pWnd);
    UNREFERENCED_PARAMETER(pDataObject);
    UNREFERENCED_PARAMETER(point);
    return HidHide::DragTargetCopyOperation(dwKeyState) && !m_DropTargetFullImageNames.empty() ? DROPEFFECT_COPY : DROPEFFECT_NONE;
}

_Use_decl_annotations_
DROPEFFECT CAppProfilesDlg::OnDropEx(CWnd* pWnd, COleDataObject* pDataObject, DROPEFFECT dropDefault, DROPEFFECT dropList, CPoint point)
try
{
    UNREFERENCED_PARAMETER(pWnd);
    UNREFERENCED_PARAMETER(pDataObject);
    UNREFERENCED_PARAMETER(dropDefault);
    UNREFERENCED_PARAMETER(dropList);
    UNREFERENCED_PARAMETER(point);

    if (m_DropTargetFullImageNames.empty()) return DROPEFFECT_NONE;
    for (auto const& profile : m_DropTargetFullImageNames) FilterDriverProxy().AppProfileAdd(profile);
    auto const selected{ *m_DropTargetFullImageNames.begin() };
    RefreshApps(&selected);
    return DROPEFFECT_COPY;
}
catch (std::runtime_error const& error)
{
    m_Refreshing = false;
    m_ProfileStatus.SetWindowTextW(L"Changes were not saved. Repeat the action to retry.");
    KillTimer(1);
    ReportConfigurationError(error);
    SetTimer(1, 1000, nullptr);
    if (dynamic_cast<HidHide::ConfigurationConflict const*>(&error))
    {
        m_RefreshPending = true;
        m_SavePending = false;
    }
    return DROPEFFECT_NONE;
}
