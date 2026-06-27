// AppProfilesDlg.cpp
#include "stdafx.h"
#include "HidHideClient.h"
#include "HidHideClientDlg.h"
#include "AppProfilesDlg.h"

IMPLEMENT_DYNAMIC(CAppProfilesDlg, CDialogEx)

_Use_decl_annotations_
CAppProfilesDlg::CAppProfilesDlg(CHidHideClientDlg& hidHideClientDlg, CWnd* pParent)
    : CDialogEx(IDD_DIALOG_APP_PROFILES, pParent)
    , m_HidHideClientDlg(hidHideClientDlg)
{}

CAppProfilesDlg::~CAppProfilesDlg() {}

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
}

BEGIN_MESSAGE_MAP(CAppProfilesDlg, CDialogEx)
    ON_LBN_SELCHANGE(IDC_LIST_APP_PROFILES_APPS, &CAppProfilesDlg::OnLbnSelchangeListApps)
    ON_NOTIFY(TVN_ITEMCHANGED, IDC_TREE_APP_PROFILES_DEVICES, &CAppProfilesDlg::OnTvnItemChangedTreeDevices)
    ON_BN_CLICKED(IDC_BUTTON_APP_PROFILES_ADD_APP, &CAppProfilesDlg::OnBnClickedButtonAddApp)
    ON_BN_CLICKED(IDC_BUTTON_APP_PROFILES_DEL_APP, &CAppProfilesDlg::OnBnClickedButtonDelApp)
    ON_WM_SHOWWINDOW()
END_MESSAGE_MAP()

BOOL CAppProfilesDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    return TRUE;
}

_Use_decl_annotations_
void CAppProfilesDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
    CDialogEx::OnShowWindow(bShow, nStatus);
    if (bShow)
    {
        RefreshApps();
    }
}

void CAppProfilesDlg::RefreshApps()
{
    m_AppsList.ResetContent();
    for (auto const& pair : FilterDriverProxy().GetAppProfiles())
    {
        m_AppsList.AddString(pair.first.native().c_str());
    }
    m_DevicesTree.DeleteAllItems();
}

void CAppProfilesDlg::RefreshDevices()
{
    m_DevicesTree.DeleteAllItems();
    int sel = m_AppsList.GetCurSel();
    if (sel == LB_ERR) return;

    CString appPathStr;
    m_AppsList.GetText(sel, appPathStr);
    std::filesystem::path appPath(appPathStr.GetString());

    auto profiles = FilterDriverProxy().GetAppProfiles();
    auto const& activeProfileDevices = profiles[appPath];

    m_DeviceItemData = HidHide::HidDevices(false);

    for (auto const& container : m_DeviceItemData)
    {
        // Use the container friendly name as the parent (same as the Devices tab)
        HTREEITEM hParent = m_DevicesTree.InsertItem(container.first.c_str());
        for (auto const& device : container.second)
        {
            // Use the HID usage string for child nodes (e.g., "Game Pad", "Joystick")
            HTREEITEM hChild = m_DevicesTree.InsertItem(device.usage.c_str(), hParent);
            m_DevicesTree.SetItemData(hChild, reinterpret_cast<DWORD_PTR>(&device));

            if (activeProfileDevices.count(device.deviceInstancePath))
            {
                m_DevicesTree.SetCheck(hChild, TRUE);
            }
        }
        m_DevicesTree.Expand(hParent, TVE_EXPAND);
    }
}

void CAppProfilesDlg::OnLbnSelchangeListApps()
{
    RefreshDevices();
}

_Use_decl_annotations_
void CAppProfilesDlg::OnTvnItemChangedTreeDevices(NMHDR* pNMHDR, LRESULT* pResult)
{
    *pResult = 0;
    NMTVITEMCHANGE* pItemChange = reinterpret_cast<NMTVITEMCHANGE*>(pNMHDR);
    if ((pItemChange->uStateNew & TVIS_STATEIMAGEMASK) == 0) return;

    bool checked = ((pItemChange->uStateNew >> 12) - 1) != 0;
    auto* device = reinterpret_cast<HidHide::HidDeviceInformation*>(m_DevicesTree.GetItemData(pItemChange->hItem));
    
    if (!device) return;

    int sel = m_AppsList.GetCurSel();
    if (sel == LB_ERR) return;

    CString appPathStr;
    m_AppsList.GetText(sel, appPathStr);
    std::filesystem::path appPath(appPathStr.GetString());

    try
    {
        if (checked)
            FilterDriverProxy().AppProfileAddEntry(appPath, device->deviceInstancePath);
        else
            FilterDriverProxy().AppProfileDelEntry(appPath, device->deviceInstancePath);
    }
    catch (...) {}
}

void CAppProfilesDlg::OnBnClickedButtonAddApp()
{
    CFileDialog fileDlg(TRUE, L"exe", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"Executables (*.exe)|*.exe|All Files (*.*)|*.*||", this);
    if (fileDlg.DoModal() == IDOK)
    {
        CString path = fileDlg.GetPathName();
        // Just add a dummy profile or select it
        try { FilterDriverProxy().AppProfileAddEntry(path.GetString(), L""); } catch (...) {}
        RefreshApps();
    }
}

void CAppProfilesDlg::OnBnClickedButtonDelApp()
{
    int sel = m_AppsList.GetCurSel();
    if (sel == LB_ERR) return;

    CString appPathStr;
    m_AppsList.GetText(sel, appPathStr);
    std::filesystem::path appPath(appPathStr.GetString());

    auto profiles = FilterDriverProxy().GetAppProfiles();
    for (auto const& dev : profiles[appPath])
    {
        try { FilterDriverProxy().AppProfileDelEntry(appPath, dev); } catch (...) {}
    }
    RefreshApps();
}
