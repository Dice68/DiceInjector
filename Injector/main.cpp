


#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <unknwn.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;


#define IDI_APP        101
#define ID_EDIT_DLL    201
#define ID_BTN_BROWSE  202
#define ID_BTN_REFRESH 203
#define ID_BTN_INJECT  204
#define ID_BTN_MIN     205
#define ID_BTN_CLOSE   206
#define ID_SEARCH      207

#define WMU_INJECT_DONE (WM_APP + 1)
#define WMU_SELECTION   (WM_APP + 2)
#define BMU_PULSE       (WM_APP + 3)
#define LSM_REFRESH     (WM_APP + 4)


namespace Theme {
    const Color BgTop(0xFF0B0E17);
    const Color BgBottom(0xFF06080D);
    const Color Panel(0xFF0E1220);
    const Color PanelBorder(0xFF1F2637);
    const Color TextPrimary(0xFFE9EEF8);
    const Color TextSecondary(0xFF9AA7BD);
    const Color TextDim(0xFF5D6B84);
    const Color Accent(0xFF3B82F6);
    const Color AccentLight(0xFF7CB3FF);
    const Color Cyan(0xFF22D3EE);
    const Color Green(0xFF34D399);
    const Color Red(0xFFF87171);
    const Color Amber(0xFFFBBF24);
    float scale = 1.0f;
    inline int S(float v) { return int(v * scale + 0.5f); }
}

const wchar_t* kVersion = L"v2.0";


HWND g_hWnd, g_hList, g_hDll, g_hSearch;
HWND g_hInject, g_hBrowse, g_hRefresh, g_hMin, g_hClose;
bool g_busy = false;
bool g_searchFocus = false, g_dllFocus = false;
DWORD g_selPid = 0;

struct StatusInfo { Color color; std::wstring text; };
StatusInfo g_status = { Theme::Green, L"READY" };

struct ProcInfo { DWORD pid; std::wstring name; };
std::vector<ProcInfo> g_procs;
std::vector<int> g_visible;
std::wstring g_filter;

struct Layout { RECT searchField{}, dllField{}; };
Layout g_lay;

HBRUSH g_panelBrush = nullptr;
WNDPROC g_origEditProc = nullptr;


void PaintParentBgInto(HWND hwnd, HDC dst, int w, int h) {
    HWND parent = GetParent(hwnd);
    HDC pdc = parent ? GetDC(parent) : NULL;
    if (!pdc) {
        FillRect(dst, NULL, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return;
    }
    POINT pt = { 0, 0 };
    ClientToScreen(hwnd, &pt);
    ScreenToClient(parent, &pt);
    BitBlt(dst, 0, 0, w, h, pdc, pt.x, pt.y, SRCCOPY);
    ReleaseDC(parent, pdc);
}

void MakeRoundRect(GraphicsPath& p, float x, float y, float w, float h, float r) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    p.Reset();
    p.AddArc(x, y, 2 * r, 2 * r, 180, 90);
    p.AddArc(x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
    p.AddArc(x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0, 90);
    p.AddArc(x, y + h - 2 * r, 2 * r, 2 * r, 90, 90);
    p.CloseFigure();
}

void FillRound(Graphics& g, float x, float y, float w, float h, float r, const Brush& br) {
    GraphicsPath p;
    MakeRoundRect(p, x, y, w, h, r);
    g.FillPath(&br, &p);
}

std::wstring Lower(const std::wstring& s) {
    std::wstring r = s;
    for (wchar_t& c : r) c = (wchar_t)std::towlower(c);
    return r;
}


struct FontKey { float px; bool bold; bool operator<(const FontKey& o) const {
    return px < o.px || (px == o.px && !bold && o.bold); } };
std::map<FontKey, std::unique_ptr<Font>> g_fonts;

Font& F(float px, bool bold) {
    FontKey k{ px, bold };
    auto& f = g_fonts[k];
    if (!f) {
        static FontFamily fam(L"Segoe UI");
        f = std::make_unique<Font>(&fam, px,
                                   bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    }
    return *f;
}

void DrawT(Graphics& g, const std::wstring& s, const RectF& rc, float px, bool bold,
           const Color& color, StringAlignment halign = StringAlignmentNear) {
    if (s.empty() || rc.Width <= 0) return;
    StringFormat fmt;
    fmt.SetAlignment(halign);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush br(color);
    g.DrawString(s.c_str(), (INT)s.size(), &F(px, bold), rc, &fmt, &br);
}

float TW(Graphics& g, const std::wstring& s, float px, bool bold) {
    if (s.empty()) return 0;
    StringFormat fmt;
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF out;
    g.MeasureString(s.c_str(), (INT)s.size(), &F(px, bold), PointF(0, 0), &fmt, &out);
    return out.Width;
}

void DrawTClip(Graphics& g, const std::wstring& s, const RectF& rc, float px, bool bold, const Color& color) {
    if (s.empty() || rc.Width <= 0) return;
    float avail = rc.Width;
    if (TW(g, s, px, bold) <= avail) { DrawT(g, s, rc, px, bold, color); return; }
    std::wstring out = s;
    for (size_t i = 0; i < s.size(); i++) {
        std::wstring c = s.substr(0, s.size() - i) + L"...";
        if (TW(g, c, px, bold) <= avail) { out = c; break; }
    }
    DrawT(g, out, rc, px, bold, color);
}


struct MonoCol { ARGB bg, fg; };
const MonoCol kMono[6] = {
    { 0x333B82F6, 0xFFA8C6FF },
    { 0x338B5CF6, 0xFFC9B8FF },
    { 0x3322D3EE, 0xFF9FE8F5 },
    { 0x3310B981, 0xFF9FE8C8 },
    { 0x33F59E0B, 0xFFFDE6A8 },
    { 0x33F43F5E, 0xFFFEBFCB },
};

MonoCol MonoFor(const std::wstring& name) {
    unsigned long long h = 1469598103934665603ull;
    for (wchar_t c : name) { h ^= (unsigned char)std::towlower(c); h *= 1099511628211ull; }
    return kMono[h % 6];
}


void DrawLogo(Graphics& g, float x, float y, float s) {
    GraphicsPath p;
    MakeRoundRect(p, x, y, s, s, s * 0.28f);
    LinearGradientBrush br(RectF(x, y, s, s), Color(0xFF4F9BFF), Color(0xFF1D5FD6), 45.0f);
    g.FillPath(&br, &p);
    Pen border(Color(0x66FFFFFF), s * 0.045f);
    g.DrawPath(&border, &p);
    SolidBrush dot(Color(0xFFF4F8FF));
    float off = s * 0.26f, r = s * 0.075f, rc = s * 0.09f, cx = x + s / 2, cy = y + s / 2;
    g.FillEllipse(&dot, x + off - r, y + off - r, 2 * r, 2 * r);
    g.FillEllipse(&dot, x + s - off - r, y + off - r, 2 * r, 2 * r);
    g.FillEllipse(&dot, x + off - r, y + s - off - r, 2 * r, 2 * r);
    g.FillEllipse(&dot, x + s - off - r, y + s - off - r, 2 * r, 2 * r);
    g.FillEllipse(&dot, cx - rc, cy - rc, 2 * rc, 2 * rc);
}

void DrawRefreshIcon(Graphics& g, float cx, float cy, float s, ARGB color) {
    Pen pen(Color(color), s * 0.16f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    RectF arc(cx - s * 0.34f, cy - s * 0.34f, s * 0.68f, s * 0.68f);
    g.DrawArc(&pen, arc, -60.0f, 285.0f);
    float a = (-60.0f + 285.0f) * 3.14159265f / 180.0f;
    float ex = cx + s * 0.34f * std::cos(a), ey = cy + s * 0.34f * std::sin(a);
    float l = s * 0.17f;
    float tx = std::sin(a), ty = -std::cos(a);
    SolidBrush br{ Color(color) };
    GraphicsPath tr;
    PointF pts[3] = {
        PointF(ex + tx * l, ey + ty * l),
        PointF(ex - ty * l * 0.55f - tx * l * 0.35f, ey + tx * l * 0.55f - ty * l * 0.35f),
        PointF(ex + ty * l * 0.55f - tx * l * 0.35f, ey - tx * l * 0.55f - ty * l * 0.35f),
    };
    tr.AddLines(pts, 3);
    tr.CloseFigure();
    g.FillPath(&br, &tr);
}

void DrawMagnifier(Graphics& g, float cx, float cy, float s, ARGB color) {
    Pen pen(Color(color), s * 0.16f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawEllipse(&pen, cx - s * 0.26f, cy - s * 0.26f, s * 0.52f, s * 0.52f);
    g.DrawLine(&pen, cx + s * 0.18f, cy + s * 0.18f, cx + s * 0.42f, cy + s * 0.42f);
}


struct ListUI {
    int top = 0, hover = -1, sel = -1;
    bool drag = false, overScroll = false, focused = false;
    int dragY = 0;
};

DWORD GetSelectedPid() {
    ListUI* lu = (ListUI*)GetWindowLongPtrW(g_hList, GWLP_USERDATA);
    if (!lu || lu->sel < 0 || lu->sel >= (int)g_visible.size()) return 0;
    return g_procs[g_visible[lu->sel]].pid;
}

void ApplyFilter() {
    std::wstring fl = Lower(g_filter);
    g_visible.clear();
    for (size_t i = 0; i < g_procs.size(); i++) {
        if (fl.empty() || Lower(g_procs[i].name).find(fl) != std::wstring::npos)
            g_visible.push_back((int)i);
    }
}

std::vector<ProcInfo> GetProcessList() {
    std::vector<ProcInfo> procs;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do { procs.push_back({ pe.th32ProcessID, pe.szExeFile }); }
            while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    std::stable_sort(procs.begin(), procs.end(),
        [](const ProcInfo& a, const ProcInfo& b) { return Lower(a.name) < Lower(b.name); });
    return procs;
}

void RefreshList() {
    g_procs = GetProcessList();
    ApplyFilter();
    ListUI* lu = (ListUI*)GetWindowLongPtrW(g_hList, GWLP_USERDATA);
    if (lu) lu->top = 0;
    g_selPid = 0;
    InvalidateRect(g_hList, NULL, FALSE);
    InvalidateRect(g_hWnd, NULL, FALSE);
}

void DrawName(Graphics& g, const std::wstring& name, const RectF& rc, const std::wstring& filter) {
    auto drawPlain = [&]() {
        DrawTClip(g, name, rc, Theme::S(12), false, Theme::TextPrimary);
    };
    if (filter.empty()) { drawPlain(); return; }
    std::wstring lower = Lower(name), fl = Lower(filter);
    size_t pos = lower.find(fl);
    if (pos == std::wstring::npos) { drawPlain(); return; }
    std::wstring pre = name.substr(0, pos);
    std::wstring mid = name.substr(pos, fl.size());
    std::wstring suf = name.substr(pos + fl.size());
    float pw = TW(g, pre, Theme::S(12), false);
    float mw = TW(g, mid, Theme::S(12), true);
    float avail = rc.Width;
    if (pw + mw <= avail) {
        float x = rc.X;
        DrawT(g, pre, RectF(x, rc.Y, pw, rc.Height), Theme::S(12), false, Theme::TextSecondary);
        x += pw;
        DrawT(g, mid, RectF(x, rc.Y, mw, rc.Height), Theme::S(12), true, Theme::AccentLight);
        x += mw;
        DrawTClip(g, suf, RectF(x, rc.Y, avail - (x - rc.X), rc.Height),
                  Theme::S(12), false, Theme::TextPrimary);
        return;
    }
    drawPlain();
}

LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ListUI* lu = (ListUI*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    int rh = Theme::S(34);
    auto rowsVisible = [&]() { RECT rc; GetClientRect(hwnd, &rc); return std::max(0, (int)(rc.bottom - 2) / rh); };
    auto updateScrollFromY = [&](int y) {
        RECT rc; GetClientRect(hwnd, &rc);
        int h = rc.bottom, n = (int)g_visible.size();
        int rowsVis = std::max(1, (h - 2) / rh);
        int maxTop = std::max(0, n - rowsVis);
        if (maxTop <= 0) { lu->top = 0; return; }
        int trackH = h - 2;
        int thumbH = std::max(Theme::S(28), trackH * rowsVis / std::max(1, n));
        int range = std::max(1, trackH - thumbH);
        float t = (float)(y - 1 - thumbH / 2) / range;
        t = std::max(0.0f, std::min(1.0f, t));
        lu->top = (int)(t * maxTop + 0.5f);
    };
    auto rowAt = [&](int y) {
        RECT rc; GetClientRect(hwnd, &rc);
        int n = (int)g_visible.size();
        if (y < 1 || y >= rc.bottom - 1) return -1;
        int vi = lu->top + (y - 1) / rh;
        return (vi >= 0 && vi < n) ? vi : -1;
    };

    switch (msg) {
    case WM_NCDESTROY:
        delete lu;
        break;

    case LSM_REFRESH:
        ApplyFilter();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
    case WM_MOUSEWHEEL:
        if (msg == WM_MOUSEWHEEL) {
            int n = (int)g_visible.size(), rowsVis = rowsVisible();
            int maxTop = std::max(0, n - rowsVis);
            lu->top -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 3;
            lu->top = std::max(0, std::min(lu->top, maxTop));
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_SETFOCUS:
        lu->focused = true;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_KILLFOCUS:
        lu->focused = false;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_KEYDOWN: {
        int n = (int)g_visible.size(), rowsVis = rowsVisible();
        int maxTop = std::max(0, n - rowsVis);
        switch (wp) {
        case VK_UP:   if (lu->sel > 0) { lu->sel--; if (lu->sel < lu->top) lu->top = lu->sel; } break;
        case VK_DOWN: if (lu->sel < n - 1) { lu->sel++; if (lu->sel >= lu->top + rowsVis) lu->top = lu->sel - rowsVis + 1; } break;
        case VK_HOME: lu->sel = 0; lu->top = 0; break;
        case VK_END:  lu->sel = n - 1; lu->top = std::max(0, n - rowsVis); break;
        case VK_PRIOR: lu->top = std::max(0, lu->top - rowsVis); break;
        case VK_NEXT:  lu->top = std::min(maxTop, lu->top + rowsVis); break;
        case VK_RETURN:
            if (lu->sel >= 0) SendMessageW(GetParent(hwnd), WM_COMMAND, ID_BTN_INJECT, 0);
            return 0;
        default: return 0;
        }
        g_selPid = GetSelectedPid();
        SendMessageW(GetParent(hwnd), WMU_SELECTION, g_selPid, 0);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (lu->drag) { updateScrollFromY(pt.y); InvalidateRect(hwnd, NULL, FALSE); return 0; }
        RECT rc; GetClientRect(hwnd, &rc);
        int n = (int)g_visible.size(), rowsVis = rowsVisible();
        bool needScroll = n > rowsVis;
        int scrollW = needScroll ? Theme::S(10) : 0;
        bool overScroll = needScroll && pt.x > rc.right - scrollW;
        if (overScroll != lu->overScroll) { lu->overScroll = overScroll; InvalidateRect(hwnd, NULL, FALSE); }
        int vi = rowAt(pt.y);
        if (vi != lu->hover) { lu->hover = vi; InvalidateRect(hwnd, NULL, FALSE); }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        lu->hover = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc; GetClientRect(hwnd, &rc);
        int n = (int)g_visible.size(), rowsVis = rowsVisible();
        bool needScroll = n > rowsVis;
        int scrollW = needScroll ? Theme::S(10) : 0;
        if (needScroll && pt.x > rc.right - scrollW) {
            lu->drag = true;
            lu->dragY = pt.y;
            SetCapture(hwnd);
            updateScrollFromY(pt.y);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        SetFocus(hwnd);
        int vi = rowAt(pt.y);
        if (vi >= 0) {
            lu->sel = vi;
            g_selPid = GetSelectedPid();
            SendMessageW(GetParent(hwnd), WMU_SELECTION, g_selPid, 0);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int vi = rowAt(pt.y);
        if (vi >= 0) {
            lu->sel = vi;
            g_selPid = GetSelectedPid();
            SendMessageW(GetParent(hwnd), WMU_SELECTION, g_selPid, 0);
            InvalidateRect(hwnd, NULL, FALSE);
            SendMessageW(GetParent(hwnd), WM_COMMAND, ID_BTN_INJECT, 0);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (lu->drag) {
            lu->drag = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (w <= 0 || h <= 0) { EndPaint(hwnd, &ps); return 0; }
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        {
            Graphics g(mem);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            PaintParentBgInto(hwnd, mem, w, h);

            GraphicsPath clip;
            MakeRoundRect(clip, 1, 1, w - 2, h - 2, Theme::S(12));
            Region rgn(&clip);
            g.SetClip(&rgn);
            SolidBrush panel(Theme::Panel);
            g.FillPath(&panel, &clip);

            int n = (int)g_visible.size();
            int rowsVis = std::max(1, (h - 2) / rh);
            int maxTop = std::max(0, n - rowsVis);
            lu->top = std::max(0, std::min(lu->top, maxTop));
            bool needScroll = n > rowsVis;
            int scrollW = needScroll ? Theme::S(10) : 0;
            int contentR = w - 1 - scrollW;

            for (int r = 0; r < rowsVis; r++) {
                int vi = lu->top + r;
                if (vi >= n) break;
                int gi = g_visible[vi];
                float y = 1 + r * rh;
                float rowH = (float)rh;
                bool sel = (vi == lu->sel);
                bool hov = (vi == lu->hover);

                if (sel) {
                    LinearGradientBrush selBg(RectF(1, y, contentR - 1, rowH),
                                              Color(0x2E3B82F6), Color(0x0A3B82F6), LinearGradientModeHorizontal);
                    g.FillRectangle(&selBg, 1.0f, y, (float)contentR - 1.0f, rowH);
                    LinearGradientBrush bar(RectF(1, y, Theme::S(3), rowH),
                                            Color(0xFF3B82F6), Color(0xFF22D3EE), LinearGradientModeVertical);
                    FillRound(g, Theme::S(5), y + Theme::S(7), Theme::S(3), rowH - Theme::S(14), Theme::S(1.5f), bar);
                } else if (hov) {
                    SolidBrush hb(Color(0x0CFFFFFF));
                    g.FillRectangle(&hb, 1.0f, y, (float)contentR - 1.0f, rowH);
                }

                const std::wstring& name = g_procs[gi].name;
                int m = Theme::S(22);
                float mx = Theme::S(14);
                float my = y + (rowH - m) / 2;
                MonoCol mc = MonoFor(name);
                SolidBrush mb(Color(mc.bg));
                FillRound(g, mx, my, m, m, Theme::S(6), mb);
                std::wstring ch;
                ch += name.empty() ? L'?' : (wchar_t)std::towupper(name[0]);
                DrawT(g, ch, RectF(mx, my, m, m), Theme::S(11), true, mc.fg);

                std::wstring pid = std::to_wstring(g_procs[gi].pid);
                float pidW = TW(g, pid, Theme::S(11), false);
                float pidX = contentR - Theme::S(16) - pidW;
                DrawT(g, pid, RectF(pidX, y, pidW, rowH), Theme::S(11), false, Theme::TextDim);

                float nx = mx + m + Theme::S(12);
                float avail = pidX - Theme::S(10) - nx;
                DrawName(g, name, RectF(nx, y, std::max(0.0f, avail), rowH), g_filter);
            }

            if (needScroll) {
                int trackH = h - 2;
                int thumbH = std::max(Theme::S(28), trackH * rowsVis / std::max(1, n));
                int range = std::max(1, trackH - thumbH);
                float ty = 1 + (float)lu->top / maxTop * range;
                float sx = w - scrollW + (scrollW - Theme::S(5)) / 2.0f;
                bool hot = lu->overScroll || lu->drag;
                SolidBrush trackBr(Color(0x08FFFFFF));
                FillRound(g, sx, 1, Theme::S(5), trackH, Theme::S(2.5f), trackBr);
                SolidBrush thumbBr(Color(hot ? 0x50FFFFFF : 0x2AFFFFFF));
                FillRound(g, sx, ty, Theme::S(5), thumbH, Theme::S(2.5f), thumbBr);
            }

            g.ResetClip();
            if (lu->focused) {
                Pen fp(Color(0x663B82F6), 1);
                g.DrawPath(&fp, &clip);
            }
        }
        BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}


enum class BtnKind { Primary, Ghost, Refresh, Min, Close };
struct BtnUI {
    BtnKind kind;
    UINT id;
    bool hover = false, pressed = false, pulse = false;
};

LRESULT CALLBACK BtnProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    BtnUI* ui = (BtnUI*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCDESTROY:
        delete ui;
        break;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;

    case WM_MOUSEMOVE:
        if (!ui->hover) {
            ui->hover = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_MOUSELEAVE:
        ui->hover = false;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        ui->pressed = true;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            ui->pressed = false;
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (PtInRect(&rc, pt))
                SendMessageW(GetParent(hwnd), WM_COMMAND, ui->id, 0);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_ENABLE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case BMU_PULSE:
        ui->pulse = (wp != 0);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (w <= 0 || h <= 0) { EndPaint(hwnd, &ps); return 0; }
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        {
            Graphics g(mem);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            bool hover = ui->hover && !ui->pressed;
            bool disabled = !IsWindowEnabled(hwnd);
            int off = ui->pressed ? 1 : 0;
            std::wstring label;
            int len = GetWindowTextLengthW(hwnd);
            if (len > 0) {
                label.resize(len);
                GetWindowTextW(hwnd, &label[0], len + 1);
            }

            PaintParentBgInto(hwnd, mem, w, h);

            switch (ui->kind) {
            case BtnKind::Primary: {
                float r = Theme::S(15);
                float glow = 0;
                if (disabled) glow = 0;
                else if (ui->pulse)
                    glow = 0.55f + 0.45f * std::sin(GetTickCount() / 120.0f);
                else if (hover) glow = 1;
                if (glow > 0.01f) {
                    SolidBrush halo(Color((BYTE)(26 * glow), 0x3B, 0x82, 0xF6));
                    FillRound(g, -Theme::S(3), -Theme::S(3), w + Theme::S(6), h + Theme::S(6), r + Theme::S(3), halo);
                }                Color c1, c2;
                if (disabled) { c1 = Color(0xFF2A3140); c2 = Color(0xFF232A38); }
                else if (hover) { c1 = Color(0xFF4F97FF); c2 = Color(0xFF2A6FE8); }
                else { c1 = Color(0xFF3E8BFF); c2 = Color(0xFF1E5FE0); }
                LinearGradientBrush body(RectF(0, 0, w, h), c1, c2, LinearGradientModeVertical);
                FillRound(g, off, off, w, h, r, body);
                SolidBrush shine(Color(0x14FFFFFF));
                FillRound(g, off + 1, off + 1, w - 2, h / 2, r, shine);
                DrawT(g, label, RectF(0, off, w, h), Theme::S(13.5f), true,
                      disabled ? 0xFF6B7689 : 0xFFFFFFFF, StringAlignmentCenter);
                break;
            }
            case BtnKind::Ghost: {
                float r = Theme::S(10);
                if (hover) {
                    SolidBrush bg(Color(disabled ? 0x00000000 : 0x0CFFFFFF));
                    FillRound(g, 0, 0, w, h, r, bg);
                }
                Pen border(Color(disabled ? 0x0FFFFFFF :
                             (hover ? 0x3DFFFFFF : 0x24FFFFFF)), 1);
                GraphicsPath bp;
                MakeRoundRect(bp, 0.5f, 0.5f, w - 1, h - 1, r);
                g.DrawPath(&border, &bp);
                DrawT(g, label, RectF(0, off, w, h), Theme::S(11.5f), true,
                      disabled ? 0xFF5D6B84 : 0xFFE9EEF8, StringAlignmentCenter);
                break;
            }
            case BtnKind::Refresh: {
                float r = Theme::S(9);
                if (hover) {
                    SolidBrush bg(Color(0x0CFFFFFF));
                    FillRound(g, 0, 0, w, h, r, bg);
                }
                DrawRefreshIcon(g, w / 2.0f, h / 2.0f + 1, Theme::S(16),
                                hover ? 0xFFD6E0F0 : 0xFF93A4C4);
                break;
            }
            case BtnKind::Min:
            case BtnKind::Close: {
                if (hover || ui->pressed) {
                    SolidBrush bg(Color(ui->kind == BtnKind::Close ? 0xFFE81123 : 0x1AFFFFFF));
                    g.FillRectangle(&bg, 0, 0, w, h);
                }
                float cx = w / 2.0f, cy = h / 2.0f;
                Pen pen(Color(0xFFE9EEF8), Theme::S(1.5f));
                pen.SetStartCap(LineCapRound);
                pen.SetEndCap(LineCapRound);
                if (ui->kind == BtnKind::Min) {
                    g.DrawLine(&pen, cx - Theme::S(5), cy, cx + Theme::S(5), cy);
                } else {
                    float d = Theme::S(5.5f);
                    g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
                    g.DrawLine(&pen, cx - d, cy + d, cx + d, cy - d);
                }
                break;
            }
            }
        }
        BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}


LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            if (hwnd == g_hSearch) g_searchFocus = (msg == WM_SETFOCUS);
            if (hwnd == g_hDll) g_dllFocus = (msg == WM_SETFOCUS);
            InvalidateRect(parent, NULL, FALSE);
        }
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN && hwnd == g_hDll) {
        SendMessageW(GetParent(hwnd), WM_COMMAND, ID_BTN_INJECT, 0);
        return 0;
    }
    if (msg == WM_PAINT) {
        if (GetWindowTextLengthW(hwnd) == 0) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, g_panelBrush);
            const wchar_t* ph = (hwnd == g_hSearch) ? L"SEARCH PROCESSES..." : L"SELECT A DLL TO INJECT...";
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(93, 107, 132));
            RECT tr = rc;
            tr.left += 2;
            DrawTextW(dc, ph, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wp, lp);
}


void LayoutControls() {
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    int W = rc.right;
    int m = Theme::S(20);

    SetWindowPos(g_hClose, NULL, W - Theme::S(46), 0, Theme::S(46), Theme::S(46), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hMin, NULL, W - 2 * Theme::S(46), 0, Theme::S(46), Theme::S(46), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hRefresh, NULL, W - m - Theme::S(36), Theme::S(56), Theme::S(36), Theme::S(36), SWP_NOZORDER | SWP_NOACTIVATE);

    g_lay.searchField = { m, Theme::S(90), W - m, Theme::S(90) + Theme::S(36) };
    SetWindowPos(g_hSearch, NULL, m + Theme::S(38), Theme::S(96), W - 2 * m - Theme::S(52), Theme::S(24),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    SetWindowPos(g_hList, NULL, m, Theme::S(134), W - 2 * m, Theme::S(218), SWP_NOZORDER | SWP_NOACTIVATE);

    g_lay.dllField = { m, Theme::S(400), W - m, Theme::S(400) + Theme::S(44) };
    int browseX = g_lay.dllField.right - Theme::S(120);
    SetWindowPos(g_hBrowse, NULL, browseX, g_lay.dllField.top + Theme::S(7), Theme::S(104), Theme::S(30),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(g_hDll, NULL, g_lay.dllField.left + Theme::S(16), g_lay.dllField.top + Theme::S(10),
                 browseX - (g_lay.dllField.left + Theme::S(16)) - Theme::S(12), Theme::S(24),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    SetWindowPos(g_hInject, NULL, (W - Theme::S(320)) / 2, Theme::S(470), Theme::S(320), Theme::S(56),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    InvalidateRect(g_hWnd, NULL, TRUE);
}


bool InjectDLL(DWORD pid, const std::wstring& path, std::wstring& err) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        err = L"CAN'T OPEN PROCESS (ERROR " + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    size_t size = (path.size() + 1) * sizeof(wchar_t);
    LPVOID mem = VirtualAllocEx(hProcess, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) {
        err = L"CAN'T ALLOCATE MEMORY (ERROR " + std::to_wstring(GetLastError()) + L")";
        CloseHandle(hProcess);
        return false;
    }
    if (!WriteProcessMemory(hProcess, mem, path.c_str(), size, NULL)) {
        err = L"CAN'T WRITE MEMORY (ERROR " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(hProcess, mem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    LPVOID loadLib = (LPVOID)GetProcAddress(kernel32, "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)loadLib, mem, 0, NULL);
    if (!hThread) {
        err = L"CAN'T CREATE REMOTE THREAD (ERROR " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(hProcess, mem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    WaitForSingleObject(hThread, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, mem, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    if (!exitCode) {
        err = L"DLL LOAD FAILED INSIDE TARGET";
        return false;
    }
    return true;
}

struct InjectJob { DWORD pid; std::wstring dll; };
bool g_injectOk = false;
std::wstring g_injectErr;

DWORD WINAPI InjectThread(LPVOID param) {
    InjectJob* job = (InjectJob*)param;
    g_injectOk = InjectDLL(job->pid, job->dll, g_injectErr);
    delete job;
    PostMessageW(g_hWnd, WMU_INJECT_DONE, 0, 0);
    return 0;
}

void StartInject() {
    if (g_busy) return;
    DWORD pid = GetSelectedPid();
    if (!pid) {
        g_status = { Theme::Amber, L"SELECT A PROCESS FROM THE LIST" };
        InvalidateRect(g_hWnd, NULL, TRUE);
        return;
    }
    wchar_t dllPath[MAX_PATH] = { 0 };
    GetWindowTextW(g_hDll, dllPath, MAX_PATH);
    if (!dllPath[0]) {
        g_status = { Theme::Amber, L"SELECT A DLL FILE" };
        InvalidateRect(g_hWnd, NULL, TRUE);
        return;
    }
    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES) {
        g_status = { Theme::Red, L"DLL FILE NOT FOUND" };
        InvalidateRect(g_hWnd, NULL, TRUE);
        return;
    }
    g_busy = true;
    EnableWindow(g_hInject, FALSE);
    SetWindowTextW(g_hInject, L"INJECTING...");
    SendMessageW(g_hInject, BMU_PULSE, TRUE, 0);
    g_status = { Theme::Accent, L"INJECTING INTO PID " + std::to_wstring(pid) + L" ..." };
    InjectJob* job = new InjectJob{ pid, dllPath };
    CreateThread(NULL, 0, InjectThread, job, 0, NULL);
    SetTimer(g_hWnd, 1, 60, NULL);
    InvalidateRect(g_hWnd, NULL, TRUE);
}

void OnInjectDone() {
    g_busy = false;
    KillTimer(g_hWnd, 1);
    EnableWindow(g_hInject, TRUE);
    SetWindowTextW(g_hInject, L"INJECT");
    SendMessageW(g_hInject, BMU_PULSE, FALSE, 0);
    if (g_injectOk)
        g_status = { Theme::Green, L"INJECTED SUCCESSFULLY - PID " + std::to_wstring(g_selPid) };
    else
        g_status = { Theme::Red, g_injectErr };
    InvalidateRect(g_hWnd, NULL, TRUE);
}


LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hwnd;
        Theme::scale = (float)GetDpiForWindow(hwnd) / 96.0f;
        RECT rc;
        GetClientRect(hwnd, &rc);
        int W = Theme::S(660), H = Theme::S(600);
        SetWindowPos(hwnd, NULL, 0, 0, W, H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        HRGN rgn = CreateRoundRectRgn(0, 0, W + 1, H + 1, Theme::S(16), Theme::S(16));
        SetWindowRgn(hwnd, rgn, TRUE);

        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        auto makeBtn = [&](HWND& out, BtnKind kind, UINT id, const wchar_t* text,
                           int x, int y, int w, int h) {
            out = CreateWindowExW(0, L"DiceButton", text,
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                x, y, w, h, hwnd, NULL, NULL, NULL);
            BtnUI* ui = new BtnUI{ kind, id };
            SetWindowLongPtrW(out, GWLP_USERDATA, (LONG_PTR)ui);
        };

        makeBtn(g_hClose, BtnKind::Close, ID_BTN_CLOSE, L"", W - Theme::S(46), 0, Theme::S(46), Theme::S(46));
        makeBtn(g_hMin, BtnKind::Min, ID_BTN_MIN, L"", W - 2 * Theme::S(46), 0, Theme::S(46), Theme::S(46));
        makeBtn(g_hRefresh, BtnKind::Refresh, ID_BTN_REFRESH, L"", 0, 0, Theme::S(36), Theme::S(36));
        makeBtn(g_hBrowse, BtnKind::Ghost, ID_BTN_BROWSE, L"BROWSE", 0, 0, Theme::S(104), Theme::S(30));
        makeBtn(g_hInject, BtnKind::Primary, ID_BTN_INJECT, L"INJECT", 0, 0, Theme::S(320), Theme::S(56));

        g_hSearch = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)ID_SEARCH, NULL, NULL);
        g_hDll = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_DLL, NULL, NULL);
        g_origEditProc = (WNDPROC)SetWindowLongPtrW(g_hSearch, GWLP_WNDPROC, (LONG_PTR)EditProc);
        SetWindowLongPtrW(g_hDll, GWLP_WNDPROC, (LONG_PTR)EditProc);

        HFONT font = CreateFontW(-Theme::S(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SendMessageW(g_hSearch, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(g_hDll, WM_SETFONT, (WPARAM)font, TRUE);

        g_hList = CreateWindowExW(0, L"DiceList", L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        SetWindowLongPtrW(g_hList, GWLP_USERDATA, (LONG_PTR)new ListUI());

        LayoutControls();
        RefreshList();
        g_status = { Theme::Green, L"READY" };
        break;
    }

    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
        if (r == HTCLIENT) {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);
            if (pt.y < Theme::S(46)) return HTCAPTION;
        }
        return r;
    }

    case WM_GETMINMAXINFO: {
        LPMINMAXINFO mmi = (LPMINMAXINFO)lp;
        mmi->ptMinTrackSize = { Theme::S(560), Theme::S(520) };
        return 0;
    }

    case WM_TIMER:
        if (wp == 1) {
            InvalidateRect(g_hInject, NULL, FALSE);
            RECT fr = { 0, Theme::S(552), 10000, Theme::S(600) };
            InvalidateRect(hwnd, &fr, FALSE);
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_SEARCH && HIWORD(wp) == EN_UPDATE) {
            wchar_t buf[256];
            GetWindowTextW(g_hSearch, buf, 256);
            g_filter = buf;
            ApplyFilter();
            InvalidateRect(g_hList, NULL, FALSE);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        switch (id) {
        case ID_BTN_REFRESH:
            RefreshList();
            g_status = { Theme::Green, L"PROCESS LIST UPDATED" };
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case ID_BTN_BROWSE: {
            OPENFILENAMEW ofn = {};
            wchar_t file[MAX_PATH] = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(g_hDll, file);
                g_status = { Theme::Green, L"DLL SELECTED" };
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case ID_BTN_INJECT:
            StartInject();
            break;

        case ID_BTN_MIN:
            ShowWindow(hwnd, SW_MINIMIZE);
            break;

        case ID_BTN_CLOSE:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        }
        return 0;
    }

    case WMU_SELECTION:
        g_selPid = (DWORD)wp;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WMU_INJECT_DONE:
        OnInjectDone();
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB(233, 238, 248));
        SetBkColor(dc, RGB(14, 18, 32));
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)g_panelBrush;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (w > 0 && h > 0) {
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
            HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
            {
                Graphics g(mem);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);


                LinearGradientBrush bg(RectF(0, 0, w, h), Theme::BgTop, Theme::BgBottom,
                                       LinearGradientModeVertical);
                g.FillRectangle(&bg, 0, 0, w, h);


                GraphicsPath glowPath;
                glowPath.AddEllipse(RectF(w * 0.52f, -Theme::S(160), w * 0.95f, w * 0.95f));
                PathGradientBrush glow(&glowPath);
                Color glowC(28, 59, 130, 246);
                Color glowE(0, 11, 14, 23);
                INT cn = 1;
                glow.SetCenterColor(glowC);
                glow.SetSurroundColors(&glowE, &cn);
                g.FillPath(&glow, &glowPath);


                LinearGradientBrush hl(RectF(0, Theme::S(46), Theme::S(280), 1),
                                       Color(90, 59, 130, 246), Color(0, 59, 130, 246),
                                       LinearGradientModeHorizontal);
                g.FillRectangle(&hl, 0, Theme::S(46), Theme::S(280), 1);


                DrawLogo(g, Theme::S(16), Theme::S(11), Theme::S(26));
                float tx = Theme::S(52);
                DrawT(g, L"DICE INJECTOR", RectF(tx, Theme::S(6), Theme::S(220), Theme::S(32)),
                      Theme::S(13.5f), true, Theme::TextPrimary);
                float tw = TW(g, L"DICE INJECTOR", Theme::S(13.5f), true);
                DrawT(g, kVersion, RectF(tx + tw + Theme::S(8), Theme::S(6), Theme::S(80), Theme::S(32)),
                      Theme::S(9.5f), false, Theme::TextDim);


                DrawT(g, L"TARGET PROCESS", RectF(Theme::S(20), Theme::S(52), Theme::S(300), Theme::S(20)),
                      Theme::S(10.5f), true, Theme::TextSecondary);
                std::wstring cnt = std::to_wstring(g_visible.size()) + L" / " + std::to_wstring(g_procs.size()) + L" PROCESSES";
                DrawT(g, cnt, RectF(Theme::S(20), Theme::S(52), w - Theme::S(20) - Theme::S(96), Theme::S(20)),
                      Theme::S(10.5f), false, Theme::TextDim, StringAlignmentFar);


                {
                    const RECT& f = g_lay.searchField;
                    float fx = f.left, fy = f.top, fw = f.right - f.left, fh = f.bottom - f.top;
                    FillRound(g, fx, fy, fw, fh, Theme::S(9), SolidBrush(Theme::Panel));
                    Pen bp(g_searchFocus ? Color(0xFF3B82F6) : Color(Theme::PanelBorder), 1);
                    GraphicsPath p;
                    MakeRoundRect(p, fx + 0.5f, fy + 0.5f, fw - 1, fh - 1, Theme::S(9));
                    g.DrawPath(&bp, &p);
                    DrawMagnifier(g, fx + Theme::S(16), fy + fh / 2, Theme::S(14),
                                  g_searchFocus ? 0xFF7CB3FF : 0xFF5D6B84);
                }


                DrawT(g, L"DLL LIBRARY", RectF(Theme::S(20), Theme::S(374), Theme::S(300), Theme::S(20)),
                      Theme::S(10.5f), true, Theme::TextSecondary);


                {
                    const RECT& f = g_lay.dllField;
                    float fx = f.left, fy = f.top, fw = f.right - f.left, fh = f.bottom - f.top;
                    FillRound(g, fx, fy, fw, fh, Theme::S(9), SolidBrush(Theme::Panel));
                    Pen bp(g_dllFocus ? Color(0xFF3B82F6) : Color(Theme::PanelBorder), 1);
                    GraphicsPath p;
                    MakeRoundRect(p, fx + 0.5f, fy + 0.5f, fw - 1, fh - 1, Theme::S(9));
                    g.DrawPath(&bp, &p);
                }


                {
                    int fy = h - Theme::S(48);
                    SolidBrush hair(Color(0x0DFFFFFF));
                    g.FillRectangle(&hair, 0, fy, w, 1);

                    float cx = Theme::S(20), cy = fy + Theme::S(25);
                    float dotR = (float)Theme::S(4);
                    if (g_busy) {
                        float p = 0.5f + 0.5f * std::sin(GetTickCount() / 90.0f);
                        dotR = (float)Theme::S(4) + (float)Theme::S(1) * p;
                        Color c1(0xFF3B82F6), c2(0xFF22D3EE);
                        LinearGradientBrush db(RectF(cx - dotR, cy - dotR, 2 * dotR, 2 * dotR), c1, c2, 45.0f);
                        g.FillEllipse(&db, cx - dotR, cy - dotR, 2 * dotR, 2 * dotR);
                    } else {
                        SolidBrush db(g_status.color);
                        g.FillEllipse(&db, cx - dotR, cy - dotR, 2 * dotR, 2 * dotR);
                    }
                    DrawT(g, g_status.text, RectF(Theme::S(36), fy, w - Theme::S(260), Theme::S(48)),
                          Theme::S(11), false, Theme::TextSecondary);

                    std::wstring right;
                    if (g_selPid) {
                        for (const auto& p : g_procs)
                            if (p.pid == g_selPid) { right = std::to_wstring(p.pid) + L"  " + p.name; break; }
                    } else {
                        right = std::wstring(L"DICE INJECTOR ") + kVersion;
                    }
                    DrawT(g, right, RectF(Theme::S(20), fy, w - Theme::S(40), Theme::S(48)),
                          Theme::S(10.5f), false, Theme::TextDim, StringAlignmentFar);
                }
            }
            BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        RECT lr;
        GetWindowRect(g_hList, &lr);
        ScreenToClient(hwnd, (LPPOINT)&lr);
        if (pt.x >= lr.left && pt.x <= lr.right && pt.y >= lr.top && pt.y <= lr.bottom)
            SendMessageW(g_hList, WM_MOUSEWHEEL, wp, 0);
        return 0;
    }

    case WM_DPICHANGED: {
        Theme::scale = HIWORD(wp) / 96.0f;
        RECT* sr = (RECT*)lp;
        SetWindowPos(hwnd, NULL, sr->left, sr->top, sr->right - sr->left, sr->bottom - sr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        HRGN rgn = CreateRoundRectRgn(0, 0, Theme::S(660) + 1, Theme::S(600) + 1, Theme::S(16), Theme::S(16));
        SetWindowRgn(hwnd, rgn, TRUE);
        LayoutControls();
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}


int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    GdiplusStartupInput gsi;
    ULONG_PTR gdiToken;
    GdiplusStartup(&gdiToken, &gsi, NULL);

    g_panelBrush = CreateSolidBrush(RGB(14, 18, 32));

    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.lpszClassName = L"DiceMain";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = BtnProc;
    wc.hIcon = NULL;
    wc.hIconSm = NULL;
    wc.lpszClassName = L"DiceButton";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = ListProc;
    wc.lpszClassName = L"DiceList";
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowW(L"DiceMain", L"DICE INJECTOR",
        WS_POPUP | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 600, NULL, NULL, hInst, NULL);
    if (!g_hWnd) return 1;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
