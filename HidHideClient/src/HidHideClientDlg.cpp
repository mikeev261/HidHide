// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// HidHideClientDlg.cpp
#include "stdafx.h"
#include "HidHideClient.h"
#include "HidHideClientDlg.h"
#include "FilterDriverProxy.h"
#include "Utils.h"
#include "Logging.h"

UINT const WM_HIDHIDE_SHOW_MANAGER{ ::RegisterWindowMessageW(L"HidHide.AppProfiles.ShowManager") };
UINT const WM_TASKBAR_CREATED{ ::RegisterWindowMessageW(L"TaskbarCreated") };

namespace
{
    constexpr UINT_PTR PROFILE_TIMER_ID{ 42 };
    constexpr UINT PROFILE_TIMER_INTERVAL_MS{ 100 };
    constexpr UINT WM_TRAY_ICON{ WM_APP + 1 };
    constexpr UINT WM_HIDE_AFTER_START{ WM_APP + 2 };
    constexpr UINT TRAY_COMMAND_SHOW{ 1 };
    constexpr UINT TRAY_COMMAND_EXIT{ 2 };
}

#pragma warning(push)
#pragma warning(disable: 26454 28213) // Warnings caused by Microsoft MFC macros
BEGIN_MESSAGE_MAP(CHidHideClientDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_APPLICATION, &CHidHideClientDlg::OnTcnSelchangeTabApplication)
    ON_WM_SHOWWINDOW()
    ON_WM_TIMER()
    ON_WM_CLOSE()
    ON_WM_DESTROY()
    ON_MESSAGE(WM_TRAY_ICON, &CHidHideClientDlg::OnTrayIcon)
    ON_MESSAGE(WM_HIDE_AFTER_START, &CHidHideClientDlg::OnHideAfterStart)
    ON_REGISTERED_MESSAGE(WM_HIDHIDE_SHOW_MANAGER, &CHidHideClientDlg::OnShowManager)
    ON_REGISTERED_MESSAGE(WM_TASKBAR_CREATED, &CHidHideClientDlg::OnTaskbarCreated)
END_MESSAGE_MAP()
#pragma warning(pop)

_Use_decl_annotations_
CHidHideClientDlg::CHidHideClientDlg(CWnd* pParent, bool startHidden)
    : CDialogEx(IDD_DIALOG_APPLICATION, pParent)
    , m_FilterDriverProxy{}
    , m_ProfileManager{}
    , m_DropTarget{}
    , m_hIcon{}
    , m_TabApplication{}
    , m_BlacklistDlg(*this, nullptr)
    , m_WhitelistDlg(*this, nullptr)
    , m_AppProfilesDlg(*this, nullptr)
    , m_StartHidden(startHidden)
{
    TRACE_ALWAYS(L"");
    m_hIcon = ::AfxGetApp()->LoadIcon(IDR_DIALOG_APPLICATION);
}

HidHide::FilterDriverProxy& CHidHideClientDlg::FilterDriverProxy() noexcept
{
    return (*m_FilterDriverProxy.get());
}

_Use_decl_annotations_
bool CHidHideClientDlg::ProfileIsActive(HidHide::FullImageName const& profile) const noexcept
{
    return m_ProfileManager && m_ProfileManager->ProfileIsActive(profile);
}

_Use_decl_annotations_
void CHidHideClientDlg::DoDataExchange(CDataExchange* pDX)
{
    TRACE_ALWAYS(L"");
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_TAB_APPLICATION, m_TabApplication);
}

BOOL CHidHideClientDlg::OnInitDialog()
{
    TRACE_ALWAYS(L"");
    CDialogEx::OnInitDialog();

    // Acquire exclusive access to the filter driver
    m_FilterDriverProxy = std::make_unique<HidHide::FilterDriverProxy>(true);

    // Register this window as a drop target
    m_DropTarget.Register(this);

    // Set the dialog title and include the version number, as defined via a define from the build environment
    std::wostringstream title;
    title << HidHide::StringTable(IDS_DIALOG_APPLICATION) << L" v" << _L(BldProductVersion);
    SetWindowTextW(title.str().c_str());

    // Add tabs to tab-control so the height of the client rectangle is defined
    TCITEM tcItem;
    tcItem.mask = TCIF_TEXT;
    auto tabApplicationHeader0{ HidHide::StringTable(IDS_TAB_APPLICATION_HEADER_0) };
    tcItem.pszText = tabApplicationHeader0.data();
    m_TabApplication.InsertItem(0, &tcItem);
    auto tabApplicationHeader1{ HidHide::StringTable(IDS_TAB_APPLICATION_HEADER_1) };
    tcItem.pszText = tabApplicationHeader1.data();
    m_TabApplication.InsertItem(1, &tcItem);
    tcItem.pszText = const_cast<LPWSTR>(L"App Profiles");
    m_TabApplication.InsertItem(2, &tcItem);

    // Determine the proper offset for the tab dialogs
    CRect clientRect;
    CRect windowRect;
    m_TabApplication.GetClientRect(&clientRect);
    m_TabApplication.AdjustRect(FALSE, &clientRect);
    m_TabApplication.GetWindowRect(&windowRect);
    ScreenToClient(windowRect);
    clientRect.OffsetRect(windowRect.left, windowRect.top);

    // Create the dialogs (invisible per default)
    m_BlacklistDlg.Create(IDD_DIALOG_BLACKLIST, m_TabApplication.GetWindow(IDD_DIALOG_BLACKLIST));
    m_BlacklistDlg.MoveWindow(clientRect);
    m_WhitelistDlg.Create(IDD_DIALOG_WHITELIST, m_TabApplication.GetWindow(IDD_DIALOG_WHITELIST));
    m_WhitelistDlg.MoveWindow(clientRect);
    m_AppProfilesDlg.Create(IDD_DIALOG_APP_PROFILES, m_TabApplication.GetWindow(IDD_DIALOG_APP_PROFILES));
    m_AppProfilesDlg.MoveWindow(clientRect);

    m_ProfileManager = std::make_unique<CProfileManager>(*m_FilterDriverProxy);
    m_ProfileManager->Recover();
    m_ProfileManager->Tick();
    AddTrayIcon();
    SetTimer(PROFILE_TIMER_ID, PROFILE_TIMER_INTERVAL_MS, nullptr);
    if (m_StartHidden) PostMessageW(WM_HIDE_AFTER_START);

    return (TRUE);
}

void CHidHideClientDlg::AddTrayIcon()
{
    m_LastTrayProfileCount = static_cast<size_t>(-1);
    m_NotifyIcon = {};
    m_NotifyIcon.cbSize = sizeof(m_NotifyIcon);
    m_NotifyIcon.hWnd = m_hWnd;
    m_NotifyIcon.uID = 1;
    m_NotifyIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_NotifyIcon.uCallbackMessage = WM_TRAY_ICON;
    m_NotifyIcon.hIcon = m_hIcon;
    wcscpy_s(m_NotifyIcon.szTip, L"HidHide App Profiles");
    ::Shell_NotifyIconW(NIM_ADD, &m_NotifyIcon);
}

void CHidHideClientDlg::RemoveTrayIcon() noexcept
{
    if (nullptr != m_NotifyIcon.hWnd) ::Shell_NotifyIconW(NIM_DELETE, &m_NotifyIcon);
    m_NotifyIcon.hWnd = nullptr;
}

void CHidHideClientDlg::HideToTray()
{
    ShowWindow(SW_HIDE);
    if (!m_HideNoticeShown)
    {
        m_HideNoticeShown = true;
        m_NotifyIcon.uFlags = NIF_INFO;
        wcscpy_s(m_NotifyIcon.szInfoTitle, L"HidHide App Profiles");
        wcscpy_s(m_NotifyIcon.szInfo, L"Profile monitoring is still running. Use the tray icon to reopen or exit.");
        m_NotifyIcon.dwInfoFlags = NIIF_INFO;
        ::Shell_NotifyIconW(NIM_MODIFY, &m_NotifyIcon);
    }
}

void CHidHideClientDlg::ShowFromTray()
{
    ShowWindow(SW_RESTORE);
    SetForegroundWindow();
}

void CHidHideClientDlg::UpdateTrayTooltip()
{
    if (!m_ProfileManager) return;
    auto const activeProfileCount{ m_ProfileManager->ActiveProfileCount() };
    if (m_LastTrayProfileCount == activeProfileCount) return;

    std::wostringstream text;
    text << L"HidHide App Profiles";
    if (0 != activeProfileCount) text << L" — " << activeProfileCount << L" active";
    m_NotifyIcon.uFlags = NIF_TIP;
    wcsncpy_s(m_NotifyIcon.szTip, text.str().c_str(), _TRUNCATE);
    ::Shell_NotifyIconW(NIM_MODIFY, &m_NotifyIcon);
    m_LastTrayProfileCount = activeProfileCount;
}

void CHidHideClientDlg::OnPaint()
{
    TRACE_ALWAYS(L"");
    if (IsIconic())
    {
        CPaintDC dc(this); // device context for painting

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // Center icon in client rectangle
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // Draw the icon
        dc.DrawIcon(x, y, m_hIcon);
    }
    else
    {
        CDialogEx::OnPaint();
    }
}

HCURSOR CHidHideClientDlg::OnQueryDragIcon()
{
    TRACE_ALWAYS(L"");
    return static_cast<HCURSOR>(m_hIcon);
}

void CHidHideClientDlg::ResyncTabDialogVisibilityState()
{
    TRACE_ALWAYS(L"");
    switch (m_TabApplication.GetCurSel())
    {
    case 0: // Applications
        m_BlacklistDlg.ShowWindow(SW_HIDE);
        m_AppProfilesDlg.ShowWindow(SW_HIDE);
        m_WhitelistDlg.ShowWindow(SW_SHOW);
        m_DropTarget.SetRedirectionTarget(m_WhitelistDlg);
        break;
    case 1: // Devices
        m_WhitelistDlg.ShowWindow(SW_HIDE);
        m_AppProfilesDlg.ShowWindow(SW_HIDE);
        m_BlacklistDlg.ShowWindow(SW_SHOW);
        m_DropTarget.SetRedirectionTarget(m_BlacklistDlg);
        break;
    case 2: // App Profiles
        m_WhitelistDlg.ShowWindow(SW_HIDE);
        m_BlacklistDlg.ShowWindow(SW_HIDE);
        m_AppProfilesDlg.ShowWindow(SW_SHOW);
        m_DropTarget.SetRedirectionTarget(m_AppProfilesDlg);
        break;
    }
}

_Use_decl_annotations_
void CHidHideClientDlg::OnTcnSelchangeTabApplication(NMHDR* pNMHDR, LRESULT* pResult)
{
    TRACE_ALWAYS(L"");
    UNREFERENCED_PARAMETER(pNMHDR);
    ResyncTabDialogVisibilityState();
    *pResult = 0;
}

_Use_decl_annotations_
void CHidHideClientDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
    TRACE_ALWAYS(L"");
    CDialogEx::OnShowWindow(bShow, nStatus);
    m_TabApplication.SetCurSel(0);
    ResyncTabDialogVisibilityState();
}

_Use_decl_annotations_
void CHidHideClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if ((PROFILE_TIMER_ID == nIDEvent) && m_ProfileManager)
    {
        try
        {
            m_ProfileManager->Tick();
            UpdateTrayTooltip();
        }
        catch (...)
        {
            LOGEXC_AND_CONTINUE;
        }
    }
    CDialogEx::OnTimer(nIDEvent);
}

void CHidHideClientDlg::OnClose()
{
    HideToTray();
}

void CHidHideClientDlg::OnCancel()
{
    if (!m_Exiting)
    {
        HideToTray();
        return;
    }
    CDialogEx::OnCancel();
}

void CHidHideClientDlg::OnOK()
{
    HideToTray();
}

void CHidHideClientDlg::OnDestroy()
{
    KillTimer(PROFILE_TIMER_ID);
    if (m_ProfileManager) m_ProfileManager->Stop();
    RemoveTrayIcon();
    CDialogEx::OnDestroy();
}

_Use_decl_annotations_
LRESULT CHidHideClientDlg::OnTrayIcon(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    auto const message{ static_cast<UINT>(lParam) };
    if ((WM_LBUTTONDBLCLK == message) || (WM_LBUTTONUP == message))
    {
        ShowFromTray();
    }
    else if ((WM_RBUTTONUP == message) || (WM_CONTEXTMENU == message))
    {
        CMenu menu;
        menu.CreatePopupMenu();
        menu.AppendMenuW(MF_STRING, TRAY_COMMAND_SHOW, L"Open HidHide App Profiles");
        menu.AppendMenuW(MF_SEPARATOR);
        menu.AppendMenuW(MF_STRING, TRAY_COMMAND_EXIT, L"Exit and restore device settings");

        CPoint point;
        ::GetCursorPos(&point);
        SetForegroundWindow();
        auto const command{ menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, this) };
        if (TRAY_COMMAND_SHOW == command) ShowFromTray();
        if (TRAY_COMMAND_EXIT == command)
        {
            m_Exiting = true;
            CDialogEx::OnCancel();
        }
    }
    return 0;
}

_Use_decl_annotations_
LRESULT CHidHideClientDlg::OnHideAfterStart(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    ShowWindow(SW_HIDE);
    return 0;
}

_Use_decl_annotations_
LRESULT CHidHideClientDlg::OnShowManager(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    ShowFromTray();
    return 0;
}

_Use_decl_annotations_
LRESULT CHidHideClientDlg::OnTaskbarCreated(WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    AddTrayIcon();
    UpdateTrayTooltip();
    return 0;
}

