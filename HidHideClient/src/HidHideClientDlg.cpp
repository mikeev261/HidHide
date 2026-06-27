// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// HidHideClientDlg.cpp
#include "stdafx.h"
#include "HidHideClient.h"
#include "HidHideClientDlg.h"
#include "FilterDriverProxy.h"
#include "Utils.h"
#include "Logging.h"
#include <TlHelp32.h>
#include <algorithm>

#pragma warning(push)
#pragma warning(disable: 26454 28213) // Warnings caused by Microsoft MFC macros
BEGIN_MESSAGE_MAP(CHidHideClientDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_APPLICATION, &CHidHideClientDlg::OnTcnSelchangeTabApplication)
    ON_WM_SHOWWINDOW()
    ON_WM_TIMER()
END_MESSAGE_MAP()
#pragma warning(pop)

_Use_decl_annotations_
CHidHideClientDlg::CHidHideClientDlg(CWnd* pParent)
    : CDialogEx(IDD_DIALOG_APPLICATION, pParent)
    , m_FilterDriverProxy{}
    , m_DropTarget{}
    , m_hIcon{}
    , m_TabApplication{}
    , m_BlacklistDlg(*this, nullptr)
    , m_WhitelistDlg(*this, nullptr)
    , m_AppProfilesDlg(*this, nullptr)
{
    TRACE_ALWAYS(L"");
    m_hIcon = ::AfxGetApp()->LoadIcon(IDR_DIALOG_APPLICATION);
}

HidHide::FilterDriverProxy& CHidHideClientDlg::FilterDriverProxy() noexcept
{
    return (*m_FilterDriverProxy.get());
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

    // Start background polling for App Profiles wrapper
    SetTimer(1, 1000, NULL);

    return (TRUE);
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

void CHidHideClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1)
    {
        try
        {
            auto profiles = m_FilterDriverProxy->GetAppProfiles();
            if (profiles.empty())
            {
                CDialogEx::OnTimer(nIDEvent);
                return;
            }

            // Build a lowercase lookup map for case-insensitive matching (full path and filename-only)
            std::map<std::wstring, HidHide::DeviceInstancePaths> profilesByFullPath;
            std::map<std::wstring, HidHide::DeviceInstancePaths> profilesByFilename;
            for (auto const& [appPath, devices] : profiles)
            {
                std::wstring lower = appPath.native();
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                profilesByFullPath[lower] = devices;

                // Also index by just the filename for anti-cheat protected processes
                std::wstring filename = appPath.filename().native();
                std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);
                profilesByFilename[filename] = devices;
            }

            // Scan all running processes for a match
            // Use CreateToolhelp32Snapshot which can see protected process names
            HidHide::DeviceInstancePaths matchedDevices;
            bool found = false;

            HANDLE hSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnapshot != INVALID_HANDLE_VALUE)
            {
                PROCESSENTRY32W pe;
                pe.dwSize = sizeof(pe);
                if (::Process32FirstW(hSnapshot, &pe))
                {
                    do
                    {
                        if (found) break;

                        // First try to get the full path
                        HANDLE hProcess = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                        if (hProcess)
                        {
                            WCHAR exePath[MAX_PATH];
                            DWORD size = MAX_PATH;
                            if (::QueryFullProcessImageNameW(hProcess, 0, exePath, &size))
                            {
                                std::wstring exeLower(exePath);
                                std::transform(exeLower.begin(), exeLower.end(), exeLower.begin(), ::towlower);

                                auto it = profilesByFullPath.find(exeLower);
                                if (it != profilesByFullPath.end())
                                {
                                    matchedDevices = it->second;
                                    found = true;
                                }
                            }
                            ::CloseHandle(hProcess);
                        }

                        // If full path didn't match, fall back to filename-only matching
                        // This handles anti-cheat protected processes where OpenProcess fails
                        if (!found)
                        {
                            std::wstring procName(pe.szExeFile);
                            std::transform(procName.begin(), procName.end(), procName.begin(), ::towlower);

                            auto it = profilesByFilename.find(procName);
                            if (it != profilesByFilename.end())
                            {
                                matchedDevices = it->second;
                                found = true;
                            }
                        }
                    } while (::Process32NextW(hSnapshot, &pe));
                }
                ::CloseHandle(hSnapshot);
            }

            // Track state to avoid redundant driver calls
            static bool wasProfileActive = false;
            static HidHide::DeviceInstancePaths lastAppliedDevices;

            if (found)
            {
                if (!wasProfileActive || matchedDevices != lastAppliedDevices)
                {
                    // Save the original blacklist on first activation
                    if (!wasProfileActive)
                    {
                        m_SavedBlacklist = m_FilterDriverProxy->GetBlacklist();
                        m_SavedActiveState = m_FilterDriverProxy->GetActive();
                    }
                    m_FilterDriverProxy->SetBlacklist(matchedDevices);
                    m_FilterDriverProxy->SetActive(true);
                    lastAppliedDevices = matchedDevices;
                    wasProfileActive = true;
                }
            }
            else if (wasProfileActive)
            {
                // Game closed — restore original state
                m_FilterDriverProxy->SetBlacklist(m_SavedBlacklist);
                m_FilterDriverProxy->SetActive(m_SavedActiveState);
                lastAppliedDevices.clear();
                wasProfileActive = false;
            }
        }
        catch (...) {}
    }
    CDialogEx::OnTimer(nIDEvent);
}

