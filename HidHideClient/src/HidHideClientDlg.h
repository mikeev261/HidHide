// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// HidHideClientDlg.h
#pragma once
#include "IDropTarget.h"
#include "BlacklistDlg.h"
#include "WhitelistDlg.h"
#include "AppProfilesDlg.h"
#include "ProfileManager.h"

extern UINT const WM_HIDHIDE_SHOW_MANAGER;

class CHidHideClientDlg : public CDialogEx, public HidHide::IDropTarget
{
public:

    CHidHideClientDlg() noexcept = delete;
    CHidHideClientDlg(_In_ CHidHideClientDlg const& rhs) = delete;
    CHidHideClientDlg(_In_ CHidHideClientDlg&& rhs) noexcept = delete;
    CHidHideClientDlg& operator=(_In_ CHidHideClientDlg const& rhs) = delete;
    CHidHideClientDlg& operator=(_In_ CHidHideClientDlg&& rhs) = delete;

    explicit CHidHideClientDlg(_In_opt_ CWnd* pParent, _In_ bool startHidden = false);
    virtual ~CHidHideClientDlg() = default;

    // Allow child dialogs access to the shared filter driver proxy
    HidHide::FilterDriverProxy& FilterDriverProxy() noexcept;
    bool ProfileIsActive(_In_ HidHide::FullImageName const& profile) const noexcept;

private:

    // Handler for drop target events
    class CDropTarget : public COleDropTarget
    {
    public:

        CDropTarget(_In_ CDropTarget const& rhs) = delete;
        CDropTarget(_In_ CDropTarget&& rhs) noexcept = delete;
        CDropTarget& operator=(_In_ CDropTarget const& rhs) = delete;
        CDropTarget& operator=(_In_ CDropTarget&& rhs) = delete;

        CDropTarget() noexcept : m_IDropTarget{} {};
        virtual ~CDropTarget() {};

        // Called when the cursor first enters the window
        DROPEFFECT OnDragEnter(_In_ CWnd* pWnd, _In_ COleDataObject* pDataObject, _In_ DWORD dwKeyState, _In_ CPoint point) override
        {
            return ((nullptr == m_IDropTarget) ? DROPEFFECT_NONE : m_IDropTarget->OnDragEnter(pWnd, pDataObject, dwKeyState, point));
        }

        // Called repeatedly when the cursor is dragged over the window
        DROPEFFECT OnDragOver(_In_ CWnd* pWnd, _In_ COleDataObject* pDataObject, _In_ DWORD dwKeyState, _In_ CPoint point) override
        {
            return ((nullptr == m_IDropTarget) ? DROPEFFECT_NONE : m_IDropTarget->OnDragOver(pWnd, pDataObject, dwKeyState, point));
        }

        // Called when data is dropped into the window, initial handler
        DROPEFFECT OnDropEx(_In_ CWnd* pWnd, _In_ COleDataObject* pDataObject, _In_ DROPEFFECT dropDefault, _In_ DROPEFFECT dropList, _In_ CPoint point) override
        {
            return ((nullptr == m_IDropTarget) ? DROPEFFECT_NONE : m_IDropTarget->OnDropEx(pWnd, pDataObject, dropDefault, dropList, point));
        }

        // Define redirection
        void SetRedirectionTarget(IDropTarget& iDropTarget)
        {
            m_IDropTarget = &iDropTarget;
        }

    private:
        IDropTarget* m_IDropTarget;
    };

    // Dialog Data
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_DIALOG_APPLICATION };
#endif

    // Update visibility of tab dialogs based on the currently selected tab
    void ResyncTabDialogVisibilityState();

    void DoDataExchange(_In_ CDataExchange* pDX) override;
    BOOL OnInitDialog() override;
    void OnCancel() override;
    void OnOK() override;

    DECLARE_MESSAGE_MAP()

    // Acquire exclusive access to the filter driver
    std::unique_ptr<HidHide::FilterDriverProxy> m_FilterDriverProxy;
    std::unique_ptr<CProfileManager> m_ProfileManager;

    // Drop file support
    CDropTarget m_DropTarget;

    // Controls
    HICON           m_hIcon;
    CTabCtrl        m_TabApplication;
    CBlacklistDlg   m_BlacklistDlg;
    CWhitelistDlg   m_WhitelistDlg;
    CAppProfilesDlg m_AppProfilesDlg;

    NOTIFYICONDATAW m_NotifyIcon{};
    bool m_StartHidden{};
    bool m_Exiting{};
    bool m_HideNoticeShown{};
    size_t m_LastTrayProfileCount{ static_cast<size_t>(-1) };

    void AddTrayIcon();
    void RemoveTrayIcon() noexcept;
    void HideToTray();
    void ShowFromTray();
    void UpdateTrayTooltip();

    // Events
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnTcnSelchangeTabApplication(_In_ NMHDR* pNMHDR, _Out_ LRESULT* pResult);
    afx_msg void OnShowWindow(_In_ BOOL bShow, _In_ UINT nStatus);
    afx_msg void OnTimer(_In_ UINT_PTR nIDEvent);
    afx_msg void OnClose();
    afx_msg void OnDestroy();
    afx_msg LRESULT OnTrayIcon(_In_ WPARAM wParam, _In_ LPARAM lParam);
    afx_msg LRESULT OnHideAfterStart(_In_ WPARAM wParam, _In_ LPARAM lParam);
    afx_msg LRESULT OnShowManager(_In_ WPARAM wParam, _In_ LPARAM lParam);
    afx_msg LRESULT OnTaskbarCreated(_In_ WPARAM wParam, _In_ LPARAM lParam);
};
