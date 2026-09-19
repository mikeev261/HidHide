// SPDX-License-Identifier: MIT
#include "stdafx.h"
#include "ProfilesView.h"
#include <oleacc.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <mutex>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "windowsapp.lib")
#undef min
#undef max

namespace ProfilesView
{
    namespace { bool contrast{}, dark{}; int acceptanceTheme{-1}; }
    bool HighContrast() { return contrast; }
    bool Dark() { return dark && !contrast; }
    void RefreshTheme()
    {
        HIGHCONTRASTW value{sizeof(value)};
        contrast=::SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0)&&(value.dwFlags&HCF_HIGHCONTRASTON);
        if(acceptanceTheme>=0) { dark=acceptanceTheme!=0; return; }
        try {
            auto foreground=winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Foreground);
            dark=(5*foreground.G+2*foreground.R+foreground.B)>8*128;
        } catch(winrt::hresult_error const&) { dark=false; } // Read failure uses the readable light palette.
    }
    void OverrideThemeForAcceptance(int value) { acceptanceTheme=value;RefreshTheme(); }
    struct ThemeObserver::State
    {
        struct Target { std::mutex mutex; HWND window{}; };
        std::shared_ptr<Target> target=std::make_shared<Target>();
        winrt::Windows::UI::ViewManagement::UISettings settings{nullptr};
        winrt::Windows::UI::ViewManagement::UISettings::ColorValuesChanged_revoker subscription{};
    };
    ThemeObserver::ThemeObserver(HWND window):m_State(std::make_unique<State>())
    {
        m_State->target->window=window;
        try {
            m_State->settings=winrt::Windows::UI::ViewManagement::UISettings();
            m_State->subscription=m_State->settings.ColorValuesChanged(winrt::auto_revoke,[target=m_State->target](auto const&,auto const&) {
                // WinRT delivers on a worker. Never read/write MFC or theme state here.
                std::lock_guard<std::mutex> lock(target->mutex);
                if(target->window)::PostMessageW(target->window,ThemeChangedMessage,0,0);
            });
        } catch(winrt::hresult_error const&) {} // WM_SETTINGCHANGE remains available.
    }
    ThemeObserver::~ThemeObserver()
    {
        {std::lock_guard<std::mutex> lock(m_State->target->mutex);m_State->target->window=nullptr;}
        m_State->subscription.revoke();
    }
    void ApplyWindowTheme(HWND window)
    {
        BOOL enabled=Dark();::DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&enabled,sizeof(enabled));
    }
    COLORREF Background() { return HighContrast()?::GetSysColor(COLOR_3DFACE):Dark()?RGB(20,24,31):RGB(245,247,251); }
    COLORREF Surface() { return HighContrast()?::GetSysColor(COLOR_WINDOW):Dark()?RGB(29,35,44):RGB(255,255,255); }
    COLORREF Ink() { return HighContrast()?::GetSysColor(COLOR_WINDOWTEXT):Dark()?RGB(235,240,248):RGB(27,42,65); }
    COLORREF Muted() { return HighContrast()?::GetSysColor(COLOR_WINDOWTEXT):Dark()?RGB(174,188,208):RGB(94,111,139); }
    COLORREF Line() { return HighContrast()?::GetSysColor(COLOR_WINDOWTEXT):Dark()?RGB(110,127,151):RGB(220,228,239); }
    COLORREF Accent() { return HighContrast()?::GetSysColor(COLOR_HIGHLIGHT):Dark()?RGB(126,184,255):RGB(0,96,204); }
    COLORREF AccentText() { return HighContrast()?::GetSysColor(COLOR_HIGHLIGHTTEXT):Dark()?RGB(15,29,48):RGB(255,255,255); }
    COLORREF Selected() { return HighContrast()?::GetSysColor(COLOR_HIGHLIGHT):Dark()?RGB(38,63,92):RGB(225,239,255); }
    COLORREF Subtle() { return HighContrast()?Surface():Dark()?RGB(35,43,55):RGB(247,249,252); }
    COLORREF Changed() { return HighContrast()?Surface():Dark()?RGB(57,45,29):RGB(255,249,238); }
    COLORREF Warning() { return HighContrast()?Surface():Dark()?RGB(71,52,22):RGB(255,244,216); }
    COLORREF WarningText() { return HighContrast()?Ink():Dark()?RGB(255,201,106):RGB(149,91,0); }
    COLORREF Success() { return HighContrast()?Ink():Dark()?RGB(91,212,164):RGB(23,131,94); }
    void Font(CFont& font, int pixels, UINT dpi, int weight)
    {
        font.DeleteObject(); font.CreateFontW(-MulDiv(pixels, static_cast<int>(dpi), 96), 0,0,0, weight,FALSE,FALSE,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    }
    void AccessibleName(HWND window, std::wstring const& name, DWORD child)
    {
        IAccPropServices* service{};
        constexpr CLSID cls{0xb5f8350b,0x0548,0x48b1,{0xa6,0xee,0x88,0xbd,0x00,0xb4,0xa5,0xe7}};
        constexpr MSAAPROPID prop{0x608d3df8,0x8128,0x4aa7,{0xa4,0x28,0xf5,0x5e,0x49,0x26,0x72,0x91}};
        if (SUCCEEDED(::CoCreateInstance(cls,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&service))))
        { service->SetHwndPropStr(window, static_cast<DWORD>(OBJID_CLIENT), child, prop, name.c_str()); service->Release(); }
    }
    BEGIN_MESSAGE_MAP(Button, CButton)
        ON_WM_PAINT()
    END_MESSAGE_MAP()
    void Button::OnPaint()
    {
        if (HighContrast()) { Default(); return; }
        CPaintDC dc(this); CRect rect; GetClientRect(rect); CString text; GetWindowTextW(text);
        auto checked = GetCheck() == BST_CHECKED;
        auto down = (GetState() & BST_PUSHED) != 0;
        auto background = prominent && IsWindowEnabled() ? Accent() : checked ? Selected() : rail ? Background() : Surface();
        dc.FillSolidRect(rect, background);
        CPen pen(PS_SOLID,1,checked ? (Dark()?Accent():RGB(142,189,250)) : Line()); auto oldPen=dc.SelectObject(&pen);
        CBrush brush(background); auto oldBrush=dc.SelectObject(&brush);
        if (!link) { rect.DeflateRect(0,0,1,1); dc.RoundRect(rect,CPoint(5,5)); }
        dc.SetBkMode(TRANSPARENT); dc.SetTextColor(!IsWindowEnabled() ? Muted() : prominent ? AccentText() : checked || link ? Accent() : Ink());
        auto oldFont=dc.SelectObject(GetFont()); if (down) rect.OffsetRect(1,1);
        rect.DeflateRect(8,2); dc.DrawText(text,rect,DT_SINGLELINE|DT_VCENTER|(rail?DT_LEFT:DT_CENTER)|DT_END_ELLIPSIS);
        if (GetFocus()==this) { rect.InflateRect(3,0); dc.DrawFocusRect(rect); }
        dc.SelectObject(oldFont); dc.SelectObject(oldBrush); dc.SelectObject(oldPen);
    }
    void Rail::MeasureItem(LPMEASUREITEMSTRUCT item) { item->itemHeight=MulDiv(66,static_cast<int>(dpi),96); }
    void Rail::UpdateHeights()
    {
        for (size_t i{};i<items.size();++i) SetItemHeight(static_cast<int>(i),MulDiv(items[i].section.empty()?66:98,static_cast<int>(dpi),96));
        Invalidate(FALSE);
    }
    void Rail::DrawItem(LPDRAWITEMSTRUCT item)
    {
        if (item->itemID>=items.size()) return;
        auto const& row=items[item->itemID]; CDC dc; dc.Attach(item->hDC); auto savedDc=dc.SaveDC(); CRect rect(item->rcItem);
        auto s=[&](int value){return MulDiv(value,static_cast<int>(dpi),96);};
        dc.FillSolidRect(rect,Background()); dc.SetBkMode(TRANSPARENT);
        if (!row.section.empty())
        {
            auto heading=rect; heading.DeflateRect(s(12),0); heading.bottom=heading.top+s(30);
            dc.SelectObject(smallFont); dc.SetTextColor(Muted()); dc.DrawText(row.section.c_str(),heading,DT_SINGLELINE|DT_VCENTER);
            dc.DrawText(std::to_wstring(row.count).c_str(),heading,DT_SINGLELINE|DT_VCENTER|DT_RIGHT);
            rect.top+=s(32);
        }
        bool selected=(item->itemState&ODS_SELECTED)!=0;
        if (selected) { dc.FillSolidRect(rect,Selected()); dc.FillSolidRect(rect.left,rect.top+s(8),s(3),rect.Height()-s(16),Accent()); }
        auto icon=rect; icon.left+=s(12); icon.right=icon.left+s(34); icon.top+=s(15); icon.bottom=icon.top+s(34);
        dc.FillSolidRect(icon,Accent()); dc.SetTextColor(AccentText()); dc.SelectObject(bold);
        std::wstring initials; if (!row.name.empty()) initials+=row.name[0]; auto space=row.name.find(L' '); if(row.name.size()>1&&iswdigit(row.name[1]))initials+=row.name[1];else if(space!=std::wstring::npos && space+1<row.name.size()) initials+=static_cast<wchar_t>(towupper(row.name[space+1]));
        dc.DrawText(initials.c_str(),icon,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        auto label=rect; label.left+=s(58); label.right-=s(8); label.top+=s(11); label.bottom=label.top+s(22);
        dc.SetTextColor(selected&&HighContrast()?::GetSysColor(COLOR_HIGHLIGHTTEXT):Ink()); dc.DrawText(row.name.c_str(),label,DT_SINGLELINE|DT_END_ELLIPSIS);
        label.top+=s(24); label.bottom+=s(24); dc.SelectObject(smallFont); dc.SetTextColor(selected&&HighContrast()?::GetSysColor(COLOR_HIGHLIGHTTEXT):Muted());
        auto state=row.state+(row.dirty?L" \u00b7 Unsaved":L""); dc.DrawText(state.c_str(),label,DT_SINGLELINE|DT_END_ELLIPSIS);
        if (item->itemState&ODS_FOCUS) { rect.DeflateRect(3,3); dc.DrawFocusRect(rect); }
        dc.RestoreDC(savedDc);dc.Detach();
    }
    BEGIN_MESSAGE_MAP(SearchEdit,CEdit)
        ON_WM_PAINT()
        ON_WM_NCPAINT()
    END_MESSAGE_MAP()
    void SearchEdit::OnPaint()
    {
        Default();
        if(!Dark()||GetWindowTextLengthW()!=0)return;
        CClientDC dc(this);CRect rect;GetClientRect(rect);dc.FillSolidRect(rect,Surface());
        auto old=dc.SelectObject(GetFont());dc.SetTextColor(Muted());dc.SetBkMode(TRANSPARENT);
        rect.DeflateRect(4,2);dc.DrawText(L"Find a profile",rect,DT_SINGLELINE|DT_TOP|DT_END_ELLIPSIS);dc.SelectObject(old);
    }
    void SearchEdit::OnNcPaint()
    {
        if(!Dark()){Default();return;}
        CWindowDC dc(this);CRect rect;GetWindowRect(rect);rect.OffsetRect(-rect.TopLeft());
        dc.Draw3dRect(rect,Line(),Line());rect.DeflateRect(1,1);dc.Draw3dRect(rect,Surface(),Surface());
    }
    BEGIN_MESSAGE_MAP(Combo,CComboBox)
        ON_WM_PAINT()
    END_MESSAGE_MAP()
    void Combo::MeasureItem(LPMEASUREITEMSTRUCT item) { item->itemHeight=MulDiv(24,static_cast<int>(::GetDpiForWindow(m_hWnd)),96); }
    void Combo::DrawItem(LPDRAWITEMSTRUCT item)
    {
        if(item->itemID==static_cast<UINT>(-1))return;
        CDC dc;dc.Attach(item->hDC);auto saved=dc.SaveDC();CRect rect(item->rcItem);
        bool selected=(item->itemState&ODS_SELECTED)!=0;
        dc.FillSolidRect(rect,selected?Selected():Surface());dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(selected&&HighContrast()?AccentText():Ink());dc.SelectObject(GetFont());
        CString text;GetLBText(static_cast<int>(item->itemID),text);rect.DeflateRect(6,0);
        dc.DrawText(text,rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        if(item->itemState&ODS_FOCUS)dc.DrawFocusRect(rect);
        dc.RestoreDC(saved);dc.Detach();
    }
    void Combo::OnPaint()
    {
        if(!Dark()){Default();return;}
        CPaintDC dc(this);CRect rect;GetClientRect(rect);dc.FillSolidRect(rect,Surface());
        dc.Draw3dRect(rect,Line(),Line());auto old=dc.SelectObject(GetFont());
        dc.SetBkMode(TRANSPARENT);dc.SetTextColor(IsWindowEnabled()?Ink():Muted());
        CString text;GetWindowTextW(text);auto label=rect;label.DeflateRect(6,1);label.right-=24;
        dc.DrawText(text,label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        CPen pen(PS_SOLID,1,Ink());auto oldPen=dc.SelectObject(&pen);int x=rect.right-14,y=rect.CenterPoint().y;
        dc.MoveTo(x-4,y-2);dc.LineTo(x,y+2);dc.LineTo(x+4,y-2);
        if(GetFocus()==this){label.DeflateRect(1,2);dc.DrawFocusRect(label);}
        dc.SelectObject(oldPen);dc.SelectObject(old);
    }
    BEGIN_MESSAGE_MAP(DeviceTable,CWnd)
        ON_WM_PAINT()
        ON_WM_SIZE()
        ON_WM_VSCROLL()
        ON_WM_MOUSEWHEEL()
        ON_WM_CTLCOLOR()
    END_MESSAGE_MAP()
    bool DeviceTable::Create(CWnd* parent,UINT id)
    {
        return CreateEx(WS_EX_CONTROLPARENT,AfxRegisterWndClass(CS_DBLCLKS,::LoadCursor(nullptr,IDC_ARROW)),L"Device visibility table. Page Up and Page Down scroll; Tab enters row controls.",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_VSCROLL|WS_TABSTOP,CRect(),parent,id)!=FALSE;
    }
    void DeviceTable::SetMetrics(UINT dpi,CFont* normal,CFont* smallFont,CFont* bold)
    {
        m_Dpi=dpi; m_Normal=normal; m_Small=smallFont; m_Bold=bold;
        m_Surface.DeleteObject(); m_Surface.CreateSolidBrush(Surface()); m_Changed.DeleteObject(); m_Changed.CreateSolidBrush(Changed());
        for(auto& row:m_Rows) { row->name.SetFont(bold); row->detail.SetFont(smallFont); row->current.SetFont(normal); row->changed.SetFont(smallFont); row->hidden.SetFont(normal);row->visible.SetFont(normal);row->info.SetFont(smallFont); }
        if(GetSafeHwnd()) Layout();
    }
    HWND DeviceTable::Radio(size_t row,bool hidden) const { if(row<m_FirstRow||row>=m_FirstRow+m_Rows.size())return nullptr; return hidden?m_Rows[row-m_FirstRow]->hidden.GetSafeHwnd():m_Rows[row-m_FirstRow]->visible.GetSafeHwnd(); }
    void DeviceTable::Update(std::vector<Device> const& devices)
    {
        m_Data=devices;Layout();Invalidate(FALSE);
    }
    void DeviceTable::BindRows()
    {
        CRect client;GetClientRect(client);auto height=MulDiv(70,static_cast<int>(m_Dpi),96);
        auto first=static_cast<size_t>(m_Scroll/height);
        auto count=std::min(m_Data.size()-std::min(first,m_Data.size()),static_cast<size_t>((client.Height()+height-1)/height+1));
        // Never silently retarget a focused semantic control when recycling it.
        if((first!=m_FirstRow||count!=m_Rows.size())&&::IsChild(m_hWnd,::GetFocus()))SetFocus();
        m_FirstRow=first;
        // Bound native HWND count to the viewport, even for 4096 remembered rules.
        while(m_Rows.size()>count)m_Rows.pop_back();
        while(m_Rows.size()<count)
        {
            auto index=static_cast<UINT>(m_Rows.size()); auto row=std::make_unique<Row>(); auto base=2000+index*8;
            row->name.Create(L"",WS_CHILD|WS_VISIBLE|SS_ENDELLIPSIS,CRect(),this,base);
            row->detail.Create(L"",WS_CHILD|WS_VISIBLE|SS_ENDELLIPSIS,CRect(),this,base+1);
            row->current.Create(L"",WS_CHILD|WS_VISIBLE|SS_ENDELLIPSIS,CRect(),this,base+2);
            row->changed.Create(L"",WS_CHILD|WS_VISIBLE,CRect(),this,base+3);
            row->hidden.Create(L"Hidden",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|BS_NOTIFY|WS_GROUP|WS_TABSTOP,CRect(),this,base+4);
            row->visible.Create(L"Visible",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|BS_NOTIFY,CRect(),this,base+5);
            row->info.Create(L"Details",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_GROUP|WS_TABSTOP,CRect(),this,base+6); row->info.link=true;
            row->name.SetFont(m_Bold);row->detail.SetFont(m_Small);row->current.SetFont(m_Normal);row->changed.SetFont(m_Small);
            row->hidden.SetFont(m_Normal);row->visible.SetFont(m_Normal);row->info.SetFont(m_Small); m_Rows.push_back(std::move(row));
        }
        for(size_t i{};i<m_Rows.size();++i)
        {
            auto const& value=m_Data[m_FirstRow+i];auto& row=*m_Rows[i];
            row.name.SetWindowTextW(value.name.c_str());row.detail.SetWindowTextW(value.detail.c_str());row.current.SetWindowTextW(value.current.c_str());row.changed.SetWindowTextW(value.changed?L"Changed":L"");
            row.hidden.SetCheck(value.visibility==1?BST_CHECKED:BST_UNCHECKED);row.visible.SetCheck(value.visibility==0?BST_CHECKED:BST_UNCHECKED);
            row.hidden.EnableWindow(value.writable);row.visible.EnableWindow(value.writable);
            auto description=value.name+L". "+value.detail+L". Currently "+value.current+L". After Apply "+(value.visibility==1?L"hidden":value.visibility==0?L"visible":L"mixed")+(value.changed?L". Unsaved.":L".");
            AccessibleName(row.hidden,description+L" Choose Hidden");AccessibleName(row.visible,description+L" Choose Visible");AccessibleName(row.info,value.name+L". Full name and exact device identity details");
            row.hidden.Invalidate(FALSE);row.visible.Invalidate(FALSE);
        }
    }
    void DeviceTable::Layout()
    {
        if(!GetSafeHwnd())return; CRect rect;GetClientRect(rect);auto s=[&](int value){return MulDiv(value,static_cast<int>(m_Dpi),96);};
        auto rowHeight=s(70); auto total=static_cast<int>(m_Data.size())*rowHeight;
        m_Scroll=std::clamp(m_Scroll,0,std::max(0,total-rect.Height()));
        SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,total-1),static_cast<UINT>(rect.Height()),m_Scroll,0};SetScrollInfo(SB_VERT,&scroll,TRUE);
        BindRows();GetClientRect(rect);auto after=rect.Width()-s(248),current=after-s(150);
        for(size_t i{};i<m_Rows.size();++i)
        {
            auto& row=*m_Rows[i];int y=static_cast<int>(m_FirstRow+i)*rowHeight-m_Scroll;
            row.name.MoveWindow(s(18),y+s(12),std::max(s(40),current-s(28)),s(21));
            row.detail.MoveWindow(s(18),y+s(37),std::max(s(40),current-s(28)),s(20));
            row.current.MoveWindow(current,y+s(17),s(138),s(20));
            row.changed.MoveWindow(current,y+s(40),s(76),s(18));
            row.info.MoveWindow(current+s(76),y+s(37),s(61),s(24));
            row.hidden.MoveWindow(after,y+s(19),s(110),s(32));row.visible.MoveWindow(after+s(110),y+s(19),s(110),s(32));
            // Fully clipped row actions must not become invisible keyboard stops.
            auto visibility=(y+s(51)>0&&y+s(19)<rect.Height())?SW_SHOW:SW_HIDE;
            auto focus=::GetFocus();if(visibility==SW_HIDE&&(focus==row.hidden.m_hWnd||focus==row.visible.m_hWnd))SetFocus();
            row.hidden.ShowWindow(visibility);row.visible.ShowWindow(visibility);
            row.info.ShowWindow(y+s(61)>0&&y+s(37)<rect.Height()?SW_SHOW:SW_HIDE);
        }
    }
    void DeviceTable::OnSize(UINT type,int width,int height){CWnd::OnSize(type,width,height);Layout();}
    void DeviceTable::Scroll(int position)
    {
        CRect rect;GetClientRect(rect);auto height=MulDiv(70,static_cast<int>(m_Dpi),96);
        auto next=std::clamp(position,0,std::max(0,static_cast<int>(m_Data.size())*height-rect.Height()));
        if(next!=m_Scroll&&::IsChild(m_hWnd,::GetFocus()))SetFocus();
        m_Scroll=next;Layout();Invalidate(FALSE);
    }
    void DeviceTable::OnVScroll(UINT code,UINT pos,CScrollBar*)
    {
        CRect rect;GetClientRect(rect);auto next=m_Scroll;auto line=MulDiv(35,static_cast<int>(m_Dpi),96);
        if(code==SB_LINEUP)next-=line;else if(code==SB_LINEDOWN)next+=line;else if(code==SB_PAGEUP)next-=rect.Height();else if(code==SB_PAGEDOWN)next+=rect.Height();
        else if(code==SB_THUMBTRACK||code==SB_THUMBPOSITION){SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(SB_VERT,&info);next=info.nTrackPos;}else if(code==SB_TOP)next=0;else if(code==SB_BOTTOM)next=INT_MAX;
        UNREFERENCED_PARAMETER(pos);Scroll(next);
    }
    BOOL DeviceTable::OnMouseWheel(UINT,short delta,CPoint){Scroll(m_Scroll-MulDiv(delta,MulDiv(70,static_cast<int>(m_Dpi),96),120));return TRUE;}
    bool DeviceTable::Keyboard(MSG const& message)
    {
        if(message.message!=WM_KEYDOWN||(message.hwnd!=m_hWnd&&!::IsChild(m_hWnd,message.hwnd)))return false;
        CRect rect;GetClientRect(rect);
        if(message.wParam==VK_NEXT)Scroll(m_Scroll+rect.Height());else if(message.wParam==VK_PRIOR)Scroll(m_Scroll-rect.Height());
        else if(message.wParam==VK_HOME)Scroll(0);else if(message.wParam==VK_END)Scroll(INT_MAX);else return false;
        return true;
    }
    BOOL DeviceTable::OnCommand(WPARAM wParam,LPARAM lParam)
    {
        auto id=LOWORD(wParam);if(id>=2000){size_t index=(id-2000)/8;auto action=(id-2000)%8;
            if(index<m_Rows.size()){
                if(HIWORD(wParam)==BN_CLICKED){if((action==4||action==5)&&choose)choose(m_FirstRow+index,action==4);else if(action==6&&details)details(m_FirstRow+index);return TRUE;}
                // Focus does not scroll/rebind: a partial row's mouse-down and
                // subsequent click must refer to the same exact device.
            }}return CWnd::OnCommand(wParam,lParam);
    }
    void DeviceTable::OnPaint()
    {
        CPaintDC dc(this);CRect rect;GetClientRect(rect);dc.FillSolidRect(rect,Surface());auto height=MulDiv(70,static_cast<int>(m_Dpi),96);
        for(size_t i{};i<m_Data.size();++i){int y=static_cast<int>(i)*height-m_Scroll;if(y+height<0||y>rect.bottom)continue;
            if(m_Data[i].changed){dc.FillSolidRect(0,y,rect.Width(),height,Changed());dc.FillSolidRect(0,y,3,height,WarningText());}
            dc.FillSolidRect(0,y+height-1,rect.Width(),1,Line());}
        if(GetFocus()==this){rect.DeflateRect(2,2);dc.DrawFocusRect(rect);}
    }
    HBRUSH DeviceTable::OnCtlColor(CDC* dc,CWnd* window,UINT)
    {
        auto id=window->GetDlgCtrlID();size_t index=id>=2000?static_cast<size_t>((id-2000)/8):m_Data.size();index+=m_FirstRow;bool changed=index<m_Data.size()&&m_Data[index].changed;
        dc->SetBkMode(TRANSPARENT);dc->SetTextColor(id%8==1?Muted():Ink());return changed?m_Changed:m_Surface;
    }
}
