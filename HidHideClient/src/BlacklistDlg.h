// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// BlacklistDlg.h
#pragma once
#include "IDropTarget.h"
#include "FilterDriverProxy.h"
#include "DeviceSelectionTree.h"
#include "PendingDeviceRefresh.h"

class CHidHideClientDlg;

class CBlacklistDlg : public CDialogEx, public HidHide::IDropTarget
{
    DECLARE_DYNAMIC(CBlacklistDlg)

public:

    CBlacklistDlg() noexcept = delete;
    CBlacklistDlg(_In_ CBlacklistDlg const& rhs) = delete;
    CBlacklistDlg(_In_ CBlacklistDlg && rhs) noexcept = delete;
    CBlacklistDlg& operator=(_In_ CBlacklistDlg const& rhs) = delete;
    CBlacklistDlg& operator=(_In_ CBlacklistDlg && rhs) = delete;

    CBlacklistDlg(_In_ CHidHideClientDlg& hidHideClientDlg, _In_opt_ CWnd* pParent);
    virtual ~CBlacklistDlg();

    // Refresh only the global switch after background profile reconciliation.
    void SynchronizeActiveState();

    void Refresh(bool background = false);
    void RetryPendingRefresh();

private:

    // Dialog Data
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_DIALOG_BLACKLIST };
#endif

    void DoDataExchange(_In_ CDataExchange* pDX) override;
    BOOL OnInitDialog() override;

    // User Message on CM Notification Callbacks
    LRESULT OnUserMessageRefresh(_In_ WPARAM wParam, _In_ LPARAM lParam);

    // The shared filter driver proxy
    HidHide::FilterDriverProxy& FilterDriverProxy() noexcept;

    DECLARE_MESSAGE_MAP()

    // The parent
    CHidHideClientDlg& m_HidHideClientDlg;

    // The item data for the black-list
    DeviceSelectionTree m_Selector;

    HidHide::DeviceInstancePaths m_DisplayedBlacklist;
    bool m_DisplayedActive{};
    bool m_Refreshing{};
    HidHide::PendingDeviceRefresh m_PendingRefresh;
    void RefreshDevices();
    struct TreeState { HTREEITEM item; UINT state; int image; int selectedImage; };
    std::vector<TreeState> m_AcknowledgedTree;
    void AcknowledgeTree();
    void RestoreTree();

    // Controls
    CTreeCtrl       m_Blacklist;
    HICON           m_LockOn;
    HICON           m_LockOff;
    HICON           m_LockBlank;
    CImageList      m_ImageList;
    CButton         m_Filter;
    CButton         m_Gaming;
    CButton         m_Enable;
    CStatic         m_Guidance;

    // Events
    afx_msg void OnTvnItemChangedTreeBlacklist(_In_ NMHDR* pNMHDR, _Out_ LRESULT* pResult);
    afx_msg void OnBnClickedCheckFilter();
    afx_msg void OnBnClickedCheckGaming();
    afx_msg void OnBnClickedCheckEnable();
    afx_msg void OnShowWindow(_In_ BOOL bShow, _In_ UINT nStatus);
};
