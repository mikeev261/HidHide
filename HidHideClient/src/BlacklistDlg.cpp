// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// BlacklistDlg.cpp
#include "stdafx.h"
#include "BlacklistDlg.h"
#include "HidHideClientDlg.h"
#include "Utils.h"
#include "Logging.h"
#include "ConfigurationUi.h"
#include "ActiveStateView.h"

// Define user-message for processing device interface arrivals
constexpr auto WM_USER_CM_NOTIFICATION_REFRESH{ WM_USER + 1 };

IMPLEMENT_DYNAMIC(CBlacklistDlg, CDialogEx)

#pragma warning(push)
#pragma warning(disable: 26454 28213) // Warnings caused by Microsoft MFC macros
BEGIN_MESSAGE_MAP(CBlacklistDlg, CDialogEx)
    ON_NOTIFY(TVN_ITEMCHANGED, IDC_TREE_BLACKLIST, &CBlacklistDlg::OnTvnItemChangedTreeBlacklist)
    ON_BN_CLICKED(IDC_CHECK_BLACKLIST_FILTER,      &CBlacklistDlg::OnBnClickedCheckFilter)
    ON_BN_CLICKED(IDC_CHECK_BLACKLIST_GAMING,      &CBlacklistDlg::OnBnClickedCheckGaming)
    ON_BN_CLICKED(IDC_CHECK_BLACKLIST_ENABLE,      &CBlacklistDlg::OnBnClickedCheckEnable)
    ON_MESSAGE(WM_USER_CM_NOTIFICATION_REFRESH,    &CBlacklistDlg::OnUserMessageRefresh)
    ON_WM_SHOWWINDOW()
END_MESSAGE_MAP()
#pragma warning(pop)

_Use_decl_annotations_
CBlacklistDlg::CBlacklistDlg(CHidHideClientDlg& hidHideClientDlg, CWnd* pParent)
    : CDialogEx(IDD_DIALOG_BLACKLIST, pParent)
    , HidHide::IDropTarget()
    , m_HidHideClientDlg{ hidHideClientDlg }
    , m_Blacklist{}
    , m_LockBlank{}
    , m_LockOff{}
    , m_LockOn{}
    , m_ImageList{}
    , m_Filter{}
    , m_Gaming{}
    , m_Enable{}
    , m_Guidance{}
{
    TRACE_ALWAYS(L"");
}

CBlacklistDlg::~CBlacklistDlg() = default;

HidHide::FilterDriverProxy& CBlacklistDlg::FilterDriverProxy() noexcept
{
    return (m_HidHideClientDlg.FilterDriverProxy());
}

_Use_decl_annotations_
void CBlacklistDlg::DoDataExchange(CDataExchange* pDX)
{
    TRACE_ALWAYS(L"");
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_TREE_BLACKLIST,            m_Blacklist);
    DDX_Control(pDX, IDC_CHECK_BLACKLIST_FILTER,    m_Filter);
    DDX_Control(pDX, IDC_CHECK_BLACKLIST_GAMING,    m_Gaming);
    DDX_Control(pDX, IDC_CHECK_BLACKLIST_ENABLE,    m_Enable);
    DDX_Control(pDX, IDC_STATIC_BLACKLIST_GUIDANCE, m_Guidance);
}

BOOL CBlacklistDlg::OnInitDialog()
{
    TRACE_ALWAYS(L"");
    CDialogEx::OnInitDialog();

    // Apply the labels from the string table
    m_Filter.SetWindowTextW  (HidHide::StringTable(IDS_CHECK_BLACKLIST_FILTER).c_str());
    m_Gaming.SetWindowTextW  (HidHide::StringTable(IDS_CHECK_BLACKLIST_GAMING).c_str());
    m_Enable.SetWindowTextW  (HidHide::StringTable(IDS_CHECK_BLACKLIST_ENABLE).c_str());
    m_Guidance.SetWindowTextW(HidHide::StringTable(IDS_STATIC_BLACKLIST_GUIDANCE).c_str());

    // Reflect the current Active state in the check-box
    m_Filter.SetCheck(BST_CHECKED);
    m_Gaming.SetCheck(BST_CHECKED);
    // The posted refresh reads configuration after controls are initialized.
    Refresh();

    // Prepare list icons
    if (nullptr == (m_LockBlank = ::LoadIconW(AfxGetApp()->m_hInstance, MAKEINTRESOURCEW(IDI_ICON_BLACKLIST_LOCK_BLANK)))) THROW_WIN32_LAST_ERROR;
    if (nullptr == (m_LockOff   = ::LoadIconW(AfxGetApp()->m_hInstance, MAKEINTRESOURCEW(IDI_ICON_BLACKLIST_LOCK_OFF))))   THROW_WIN32_LAST_ERROR;
    if (nullptr == (m_LockOn    = ::LoadIconW(AfxGetApp()->m_hInstance, MAKEINTRESOURCEW(IDI_ICON_BLACKLIST_LOCK_ON))))    THROW_WIN32_LAST_ERROR;
    if (FALSE == m_ImageList.Create(16, 16, (ILC_COLOR8 | ILC_MASK), 2, 2)) THROW_WIN32(ERROR_INVALID_PARAMETER);
    if (-1 == m_ImageList.Add(m_LockBlank)) THROW_WIN32(ERROR_INVALID_PARAMETER);
    if (-1 == m_ImageList.Add(m_LockOff))   THROW_WIN32(ERROR_INVALID_PARAMETER);
    if (-1 == m_ImageList.Add(m_LockOn))    THROW_WIN32(ERROR_INVALID_PARAMETER);
    m_Blacklist.SetImageList(&m_ImageList, TVSIL_NORMAL);

    return (TRUE);
}

_Use_decl_annotations_
void CBlacklistDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
    TRACE_ALWAYS(L"");
    CDialogEx::OnShowWindow(bShow, nStatus);
    Refresh();
}

void CBlacklistDlg::Refresh(bool background)
{
    TRACE_ALWAYS(L"");
    if (background) m_PendingRefresh.Request(::GetTickCount64());
    else PostMessageW(WM_USER_CM_NOTIFICATION_REFRESH);
}

void CBlacklistDlg::RetryPendingRefresh()
{
    try
    {
        m_PendingRefresh.RunIfDue(::GetTickCount64(), [this] { RefreshDevices(); });
    }
    catch (std::runtime_error const&)
    {
        m_Refreshing = false;
        LOGEXC_AND_CONTINUE;
    }
}

_Use_decl_annotations_
LRESULT CBlacklistDlg::OnUserMessageRefresh(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    try { RefreshDevices(); }
    catch (std::runtime_error const& error)
    {
        m_Refreshing = false;
        ReportConfigurationError(error);
    }
    return 0;
}

void CBlacklistDlg::RefreshDevices()
{
    TRACE_ALWAYS(L"");
    // Read everything before changing controls, so contention retains the view.
    auto const deviceInstancePathsBlacklisted{ m_HidHideClientDlg.Baseline() };
    auto const active{ FilterDriverProxy().GetActive() };
    auto devices = HidHide::HidDevices(false);
    m_Refreshing = true;
    m_AcknowledgedTree.clear();
    m_Selector.Build(m_Blacklist, devices, deviceInstancePathsBlacklisted,
        { 0 != (m_Gaming.GetCheck() & BST_CHECKED), 0 != (m_Filter.GetCheck() & BST_CHECKED), true },
        DeviceSelectionTree::Presentation::DeviceLocks);
    m_DisplayedBlacklist = deviceInstancePathsBlacklisted;
    m_DisplayedActive = active;
    m_Enable.SetCheck(active ? BST_CHECKED : BST_UNCHECKED);
    AcknowledgeTree();
    m_Refreshing = false;
    m_PendingRefresh.Complete();
}

_Use_decl_annotations_
void CBlacklistDlg::OnTvnItemChangedTreeBlacklist(NMHDR* pNMHDR, LRESULT* pResult)
try
{
    TRACE_ALWAYS(L"");
    *pResult = 0;
    if (m_Refreshing) return;
    auto const& pNMTVItemChange{ *reinterpret_cast<NMTVITEMCHANGE*>(pNMHDR) };

    if ((pNMTVItemChange.uStateOld & TVIS_STATEIMAGEMASK) == (pNMTVItemChange.uStateNew & TVIS_STATEIMAGEMASK)) return;
    struct RefreshGuard { bool& flag; RefreshGuard(bool& value) : flag(value) { flag = true; } ~RefreshGuard() { flag = false; } } guard(m_Refreshing);
    if (!m_Selector.Change(m_Blacklist, pNMTVItemChange.hItem)) return;
    auto const deviceInstancePaths = m_Selector.Selection(m_Blacklist, m_DisplayedBlacklist);

    // Forward the new selection to the filter driver
    m_HidHideClientDlg.EditBaseline(m_DisplayedBlacklist, deviceInstancePaths);
    m_DisplayedBlacklist = deviceInstancePaths;
    AcknowledgeTree();
    *pResult = 0;
}
catch (std::runtime_error const& error)
{
    RestoreTree();
    ReportConfigurationError(error, true);
    Refresh();
}


void CBlacklistDlg::OnBnClickedCheckFilter()
{
    TRACE_ALWAYS(L"");
    Refresh();
}

void CBlacklistDlg::OnBnClickedCheckGaming()
{
    TRACE_ALWAYS(L"");
    Refresh();
}

void CBlacklistDlg::SynchronizeActiveState()
{
    if (!m_Enable.GetSafeHwnd()) return;
    HidHide::SynchronizeActiveState(FilterDriverProxy(), m_DisplayedActive,
        [this](bool active) { m_Enable.SetCheck(active ? BST_CHECKED : BST_UNCHECKED); });
}

void CBlacklistDlg::OnBnClickedCheckEnable()
try
{
    TRACE_ALWAYS(L"");
    bool const active = 0 != (m_Enable.GetCheck() & BST_CHECKED);
    m_HidHideClientDlg.SetEnabled(m_DisplayedActive, active);
    m_DisplayedActive = active;
}
catch (std::runtime_error const& error)
{
    m_Enable.SetCheck(m_DisplayedActive ? BST_CHECKED : BST_UNCHECKED);
    m_Refreshing = false;
    ReportConfigurationError(error, true);
    Refresh();
}

// Retain the rendered control state, including parent icons and composite checks.
// Rollback must remain available even when configuration reads keep failing.
void CBlacklistDlg::AcknowledgeTree()
{
    m_Selector.Acknowledge();
    std::vector<TreeState> acknowledged;
    auto capture = [this, &acknowledged](HTREEITEM item)
    {
        TreeState state{ item, m_Blacklist.GetItemState(item, TVIS_STATEIMAGEMASK), 0, 0 };
        m_Blacklist.GetItemImage(item, state.image, state.selectedImage);
        acknowledged.push_back(state);
    };
    for (auto parent = m_Blacklist.GetRootItem(); parent; parent = m_Blacklist.GetNextSiblingItem(parent))
    {
        capture(parent);
        for (auto child = m_Blacklist.GetChildItem(parent); child; child = m_Blacklist.GetNextSiblingItem(child)) capture(child);
    }
    m_AcknowledgedTree = std::move(acknowledged);
}

void CBlacklistDlg::RestoreTree()
{
    m_Refreshing = true;
    for (auto const& state : m_AcknowledgedTree)
    {
        m_Blacklist.SetItemState(state.item, state.state, TVIS_STATEIMAGEMASK);
        m_Blacklist.SetItemImage(state.item, state.image, state.selectedImage);
    }
    m_Refreshing = false;
}
