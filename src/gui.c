/**
 * @             Project: BUAA GPA Studio
 * @                File: gui.c
 * @              Author: WangNan
 * @       Creation Date: 2026.10.14
 * @         Description: IUP desktop controls, callbacks and live results.
 * @ Version Information: Created by WangNan, 2026.10.14
 *                        Revised by WangNan, 2026.10.19
 *                        Revised by WangNan, 2026.11.04
 *                        Revised by WangNan, 2026.11.11
 *                        Revised by WangNan, 2026.11.13
 *                        Revised by WangNan, 2026.11.14
 *                        Revised by WangNan, 2026.11.15
 *                        Revised by WangNan, 2026.11.21
 *                        Revised by WangNan, 2026.11.22
 *                        Revised by WangNan, 2026.11.23
 *                        Revised by WangNan, 2026.11.24
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <wchar.h>
#include <iup.h>
#include <iupdraw.h>
#include <iupkey.h>
#include "gpastudio.h"

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#endif

/* ─────────────────── 跨平台工具 ─────────────────── */

/** 跨平台字符串复制 (避免 strdup 的 POSIX/MSVC 差异) */
static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *d = (char *)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

/* ─────────────────── GUI 全局状态 ─────────────────── */

typedef struct {
    Ihandle *dlg;
    Ihandle *tab_bar;          /* Tab 栏容器 (Hbox) */
    Ihandle *tab_stack;        /* 页面堆栈 */
    Ihandle *result_labels[5]; /* 0=算术 1=加权 2=GPA 3=学分 4=课程数 */
    Ihandle *status_label;
    Ihandle *focus_sink;
    Ihandle *undo_btn;
    Ihandle *redo_btn;
    Ihandle *autosave_timer;
    Ihandle *results_timer;
    Ihandle *status_timer;
    Ihandle *autosave_switch;
    Ihandle *precision_list;
    unsigned long long status_at;
    bool     pending_autosave_status;
    bool     rebuilding;       /* 重建期间抑制回调 */
    int      restore_set;
    int      restore_course;
    int      restore_field;
    int      restore_start;
    int      restore_end;
} GuiState;

static GuiState *gui = NULL;
static int min_course_name_width = 290;
enum {
    TAB_BAR_MIN_WIDTH     = 560,
    TAB_NORMAL_WIDTH      = 136,
    TAB_MENU_WIDTH        = 350,
    TAB_MENU_BUTTON_WIDTH = 40,
    DRAWN_GRID_AFTER      = 0, /* 所有非空成绩组使用相同的绘制表格 */
    DRAWN_ROW_PITCH       = 38,
    DRAWN_ROW_HEIGHT      = 28,
    DRAWN_MARGIN          = 14,
    DRAWN_HEADER_EXTRA    = 2 * DRAWN_MARGIN + 30 + 60 + 80 + 208 + 28 + 5 * 8
};

static int measure_text_width(const wchar_t *value, int points, bool bold, int fallback) {
#ifdef _WIN32
    HDC dc = GetDC(NULL);
    if (!dc) return fallback;
    int pixels = -MulDiv(points, GetDeviceCaps(dc, LOGPIXELSY), 72);
    HFONT font = CreateFontW(pixels, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    SIZE size = {0, 0};
    if (font) {
        HGDIOBJ previous = SelectObject(dc, font);
        GetTextExtentPoint32W(dc, value, (int)wcslen(value), &size);
        SelectObject(dc, previous);
        DeleteObject(font);
    }
    ReleaseDC(NULL, dc);
    return size.cx > 0 ? size.cx : fallback;
#else
    (void)value;
    (void)points;
    (void)bold;
    return fallback;
#endif
}

static int measure_course_name_width(void) {
    return measure_text_width(L"新时代中国特色社会主义理论与实践", 10, false, min_course_name_width);
}

static int measure_utf8_text_width(const char *value, int points, bool bold, int fallback) {
#ifdef _WIN32
    wchar_t wide[MAX_NAME_LEN];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, wide, MAX_NAME_LEN)) return fallback;
    return measure_text_width(wide, points, bold, fallback);
#else
    (void)value;
    (void)points;
    (void)bold;
    return fallback;
#endif
}

/* ─────────────────── 前向声明 ─────────────────── */

static void rebuild_tab_bar(void);
static void rebuild_page(int si);
static void rebuild_current_page(void);
static void rebuild_stack_pages(void);
static void append_course_rows(int si);
static void remove_course_row(int si, int ci);
static void set_course_row_index(Ihandle *row, int ci);
static void ensure_page_ready(int si);
static void refresh_tab_selection(void);
static void refresh_results(void);
static void schedule_results_refresh(void);
static void update_toolbar_state(void);
static void mark_dirty(void);
static int course_scroll_resize_cb(Ihandle *scroll, int width, int height);
static void sync_course_header_gutter(Ihandle *page);
static void rebuild_drawn_grid(int si);
static void update_drawn_grid(int si);
static void activate_drawn_editor(Ihandle *canvas, int ci, int field);
static void drawn_grid_fill_layer(Ihandle *layer);
static void position_drawn_editor(Ihandle *canvas);
static void hide_drawn_editor(Ihandle *canvas);
static void sync_drawn_page_visibility(void);

static unsigned long long tick_ms(void) {
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    return (unsigned long long)time(NULL) * 1000;
#endif
}

static void caret_to_end(Ihandle *text) {
#ifdef _WIN32
    HWND window = (HWND)IupGetAttribute(text, "WID");
    if (window) {
        LRESULT length = SendMessageW(window, WM_GETTEXTLENGTH, 0, 0);
        SendMessageW(window, EM_SETSEL, (WPARAM)length, (LPARAM)length);
    }
#else
    IupSetAttribute(text, "SELECTION", "NONE");
#endif
}

static int text_killfocus_cb(Ihandle *text) {
    caret_to_end(text);
    return IUP_DEFAULT;
}

static void focus_dialog_background(Ihandle *dialog) {
    if (!dialog) return;
#ifdef _WIN32
    IupPostMessage(dialog, "FOCUS_BACKGROUND", 0, 0.0, (void *)GetFocus());
#else
    IupPostMessage(dialog, "FOCUS_BACKGROUND", 0, 0.0, NULL);
#endif
}

static int background_click_cb(Ihandle *control, int button, int pressed, int x, int y, char *status) {
    (void)x;
    (void)y;
    (void)status;
    if (button == IUP_BUTTON1 && pressed) focus_dialog_background(IupGetDialog(control));
    return IUP_DEFAULT;
}

static Ihandle *separator(void) {
    Ihandle *line = IupLabel(NULL);
    IupSetAttribute(line, "SEPARATOR", "HORIZONTAL");
    IupSetAttribute(line, "EXPAND", "HORIZONTAL");
    IupSetCallback(line, "BUTTON_CB", (Icallback)background_click_cb);
    return line;
}

static Ihandle *copyable_text(const char *value, const char *font, int width) {
    Ihandle *text = IupText(NULL);
    IupSetAttribute(text, "VALUE", value);
    IupSetAttribute(text, "READONLY", "YES");
    IupSetAttribute(text, "BORDER", "NO");
    IupSetAttribute(text, "BGCOLOR", "245 247 250");
    IupSetAttribute(text, "ALIGNMENT", "ACENTER");
    if (font) IupSetAttribute(text, "FONT", font);
    if (width > 0) IupSetStrf(text, "RASTERSIZE", "%dx", width);
    IupSetCallback(text, "KILLFOCUS_CB", (Icallback)text_killfocus_cb);
    return text;
}

static Ihandle *action_button(const char *title, bool primary) {
    Ihandle *button = IupFlatButton(title);
    IupSetAttribute(button, "PADDING", "5x5");
    /* 随附 IUP 在 BORDERWIDTH=0 时保留单线边框，不画内侧浅边。 */
    IupSetAttribute(button, "BORDER", "YES");
    IupSetAttribute(button, "BORDERWIDTH", "0");
    IupSetAttribute(button, "CANFOCUS", "NO");
    IupSetAttribute(button, "BGCOLOR", primary ? "52 152 219" : "255 255 255");
    IupSetAttribute(button, "FGCOLOR", primary ? "255 255 255" : "51 65 85");
    IupSetAttribute(button, "HLCOLOR", primary ? "41 128 185" : "234 242 251");
    IupSetAttribute(button, "PSCOLOR", primary ? "35 110 163" : "218 232 247");
    return button;
}

static Ihandle *horizontal_fill(void) {
    Ihandle *fill = IupLabel(NULL);
    IupSetAttribute(fill, "EXPAND", "HORIZONTAL");
    IupSetCallback(fill, "BUTTON_CB", (Icallback)background_click_cb);
    return fill;
}

static Ihandle *fixed_spacer(int width) {
    Ihandle *spacer = IupLabel(NULL);
    IupSetStrf(spacer, "RASTERSIZE", "%dx0", width);
    return spacer;
}

static Ihandle *toolbar_spacer(void) {
    Ihandle *spacer = IupLabel(NULL);
    IupSetAttribute(spacer, "RASTERSIZE", "10x0");
    return spacer;
}

static void style_danger_button(Ihandle *button) {
    /* 删除课程与删除组均保留纯色块，不显示边框。 */
    IupSetAttribute(button, "BORDER", "NO");
    IupSetAttribute(button, "BGCOLOR", "185 48 48");
    IupSetAttribute(button, "FGCOLOR", "255 255 255");
    IupSetAttribute(button, "HLCOLOR", "163 38 38");
    IupSetAttribute(button, "PSCOLOR", "130 30 30");
}

static void sync_current_drawn_grid(void) {
    if (!gui || !gui->dlg || !g_app || !gui->tab_stack ||
        IupGetInt(gui->dlg, "SYNCING_DRAWN_GRID") ||
        g_app->current_set < 0 || g_app->current_set >= g_app->set_count) return;
    Ihandle *page = IupGetChild(gui->tab_stack, g_app->current_set);
    if (!page || !IupGetInt(page, "DRAWN_GRID")) return;
    IupSetInt(gui->dlg, "SYNCING_DRAWN_GRID", 1);
    Ihandle *layer = IupGetChild(page, 2);
    drawn_grid_fill_layer(layer);
    sync_course_header_gutter(page);
    drawn_grid_fill_layer(layer);
    position_drawn_editor(IupGetChild(layer, 0));
    IupSetInt(gui->dlg, "SYNCING_DRAWN_GRID", 0);
}

/* 行按钮的回调返回之后再销毁旧行，避免销毁正在处理事件的控件。 */
static int main_postmessage_cb(Ihandle *dlg, char *message, int set_idx, double number, void *data) {
    if (!message || !gui || !g_app) return IUP_DEFAULT;
    if (!strcmp(message, "APPEND_COURSE_ROWS")) {
        append_course_rows(set_idx);
        refresh_results();
        update_toolbar_state();
    } else if (!strcmp(message, "REMOVE_COURSE_ROW")) {
        remove_course_row(set_idx, (int)number);
        refresh_results();
        update_toolbar_state();
    } else if (!strcmp(message, "SYNC_DRAWN_GRID_SIZE")) {
        IupSetInt(dlg, "GRID_RESIZE_QUEUED", 0);
        sync_current_drawn_grid();
    } else if (!strcmp(message, "FOCUS_BACKGROUND")) {
#ifdef _WIN32
        HWND window = (HWND)IupGetAttribute(dlg, "WID");
        if (!window || (data && GetFocus() != (HWND)data)) return IUP_DEFAULT;
#endif
        if (gui->focus_sink) IupSetFocus(gui->focus_sink);
    } else if (!strcmp(message, "RESTORE_COURSE_FOCUS")) {
        int si = gui->restore_set;
        int ci = gui->restore_course;
        int field = gui->restore_field;
        gui->restore_field = 0;
        if (field < 1 || field > 3 || si != g_app->current_set ||
            si < 0 || si >= g_app->set_count ||
            ci < 0 || ci >= g_app->sets[si].count) return IUP_DEFAULT;
        Ihandle *page = IupGetChild(gui->tab_stack, si);
        Ihandle *entry = NULL;
        if (page && IupGetInt(page, "DRAWN_GRID")) {
            Ihandle *layer = IupGetChild(page, 2);
            Ihandle *canvas = IupGetChild(layer, 0);
            int height = 0;
            IupGetIntInt(canvas, "DRAWSIZE", NULL, &height);
            int y = 5 + ci * DRAWN_ROW_PITCH;
            float pos = IupGetFloat(canvas, "POSY");
            if (y < pos || y + DRAWN_ROW_HEIGHT > pos + height) {
                IupSetFloat(canvas, "POSY", y > 5 ? y - 5 : 0);
                IupUpdate(canvas);
            }
            activate_drawn_editor(canvas, ci, field);
            entry = IupGetChild(layer, 1);
        } else {
            Ihandle *scroll = page ? IupGetChild(page, 1) : NULL;
            Ihandle *vbox = scroll ? IupGetChild(scroll, 0) : NULL;
            Ihandle *row = vbox ? IupGetChild(vbox, ci) : NULL;
            entry = row ? IupGetChild(row, field) : NULL;
        }
        if (entry) {
            IupSetFocus(entry);
#ifdef _WIN32
            HWND window = (HWND)IupGetAttribute(entry, "WID");
            if (window) {
                int length = (int)SendMessageW(window, WM_GETTEXTLENGTH, 0, 0);
                int start = gui->restore_start < length ? gui->restore_start : length;
                int end = gui->restore_end < length ? gui->restore_end : length;
                SendMessageW(window, EM_SETSEL, (WPARAM)start, (LPARAM)end);
            }
#endif
        }
    }
    return IUP_DEFAULT;
}

static int main_resize_cb(Ihandle *dlg, int width, int height) {
    (void)width;
    (void)height;
    if (!IupGetInt(dlg, "GRID_RESIZE_QUEUED")) {
        IupSetInt(dlg, "GRID_RESIZE_QUEUED", 1);
        IupPostMessage(dlg, "SYNC_DRAWN_GRID_SIZE", 0, 0.0, NULL);
    }
    return IUP_DEFAULT;
}

#ifdef _WIN32
static LRESULT CALLBACK background_focus_subclass(HWND window, UINT message, WPARAM wparam,
                                                   LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
    (void)data;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, background_focus_subclass, id);
    LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_LBUTTONDOWN && gui && gui->dlg)
        focus_dialog_background(gui->dlg);
    if (id == 1 && (message == WM_EXITSIZEMOVE ||
                    (message == WM_SIZE && wparam != SIZE_MINIMIZED)))
        sync_current_drawn_grid();
    return result;
}

static void set_window_icon(Ihandle *dlg) {
    HWND window = (HWND)IupGetAttribute(dlg, "WID");
    HINSTANCE module = GetModuleHandleW(NULL);
    if (!window || !module) return;
    HICON small = (HICON)LoadImageW(module, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_SHARED);
    HICON large = (HICON)LoadImageW(module, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_SHARED);
    if (small) SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)small);
    if (large) SendMessageW(window, WM_SETICON, ICON_BIG, (LPARAM)large);
}
#endif

/* ─────────────────── 模态框 ─────────────────── */

typedef struct {
    Ihandle *dlg;
    int answer;
} ChoiceDlgData;

static int choice_button_cb(Ihandle *button) {
    ChoiceDlgData *data = (ChoiceDlgData *)IupGetAttribute(button, "DATA");
    data->answer = IupGetInt(button, "ANSWER");
    IupHide(data->dlg);
    return IUP_DEFAULT;
}

static int gui_choice(const char *title, const char *message, const char *first, const char *second, const char *third) {
    ChoiceDlgData data = {NULL, third ? 3 : 2};
    Ihandle *row = IupHbox(horizontal_fill(), NULL);
    const char *titles[] = {first, second, third};
    for (int i = 0; i < 3 && titles[i]; ++i) {
        Ihandle *button = IupButton(titles[i], NULL);
        IupSetAttribute(button, "RASTERSIZE", "86x32");
        IupSetAttribute(button, "DATA", (char *)&data);
        IupSetInt(button, "ANSWER", i + 1);
        IupSetCallback(button, "ACTION", (Icallback)choice_button_cb);
        IupAppend(row, button);
    }
    IupAppend(row, horizontal_fill());
    IupSetAttribute(row, "GAP", "10");
    IupSetAttribute(row, "MARGIN", "0x0");
    IupSetAttribute(row, "EXPAND", "HORIZONTAL");
    Ihandle *label = IupLabel(message);
    IupSetAttribute(label, "EXPAND", "HORIZONTAL");
    IupSetAttribute(label, "ALIGNMENT", "ACENTER");
    Ihandle *body = IupVbox(label, row, NULL);
    IupSetAttribute(body, "MARGIN", "15x15");
    IupSetAttribute(body, "GAP", "15");
    data.dlg = IupDialog(body);
    IupSetAttribute(data.dlg, "TITLE", title);
    IupSetAttribute(data.dlg, "DIALOGFRAME", "YES");
    IupSetAttributeHandle(data.dlg, "PARENTDIALOG", gui->dlg);
    IupMap(data.dlg);
#ifdef _WIN32
    set_window_icon(data.dlg);
#endif
    IupPopup(data.dlg, IUP_CENTERPARENT, IUP_CENTERPARENT);
    IupDestroy(data.dlg);
    return data.answer;
}

bool gui_confirm(const char *title, const char *msg) {
    return gui_choice(title, msg, "确定", "取消", NULL) == 1;
}

/* 模态输入框数据 */
typedef struct {
    Ihandle *dlg;
    Ihandle *text;
    char     result[MAX_NAME_LEN];
    bool     confirmed;
} InputDlgData;

static int input_ok_cb(Ihandle *btn) {
    InputDlgData *d = (InputDlgData *)IupGetAttribute(btn, "DATA");
    const char *v = IupGetAttribute(d->text, "VALUE");
    if (v && (strlen(v) >= MAX_NAME_LEN || strpbrk(v, "=\r\n"))) {
        gui_choice("名称无效", "名称不能包含等号或换行，长度不能超过 255 字节。",
                   "确定", NULL, NULL);
        return IUP_DEFAULT;
    }
    strncpy(d->result, v ? v : "", MAX_NAME_LEN - 1);
    d->result[MAX_NAME_LEN - 1] = '\0';
    d->confirmed = true;
    IupHide(d->dlg);
    return IUP_DEFAULT;
}

static int input_cancel_cb(Ihandle *btn) {
    InputDlgData *d = (InputDlgData *)IupGetAttribute(btn, "DATA");
    d->confirmed = false;
    IupHide(d->dlg);
    return IUP_DEFAULT;
}

static int input_k_any(Ihandle *self, int c) {
    if (c == K_ESC) {
        InputDlgData *d = (InputDlgData *)IupGetAttribute(self, "DATA");
        d->confirmed = false;
        IupHide(d->dlg);
        return IUP_IGNORE;
    }
    if (c == K_CR) {
        InputDlgData *d = (InputDlgData *)IupGetAttribute(self, "DATA");
        input_ok_cb(d->text);
        return IUP_IGNORE;
    }
    return IUP_DEFAULT;
}

static int input_show_cb(Ihandle *dlg, int state) {
    if (state == IUP_SHOW) {
        IupPostMessage(dlg, "INPUT_CARET_END", 0, 0.0, NULL);
    }
    return IUP_DEFAULT;
}

static int input_postmessage_cb(Ihandle *dlg, char *message, int unused, double number, void *data) {
    (void)unused;
    (void)number;
    (void)data;
    if (message && !strcmp(message, "INPUT_CARET_END")) {
        InputDlgData *input = (InputDlgData *)IupGetAttribute(dlg, "DATA");
        IupSetFocus(input->text);
        caret_to_end(input->text);
    }
    return IUP_DEFAULT;
}

static int input_focus_cb(Ihandle *text) {
    caret_to_end(text);
    return IUP_DEFAULT;
}

char *gui_input(const char *title, const char *prompt, const char *initial) {
    InputDlgData d;
    memset(&d, 0, sizeof(d));

    d.text = IupText(NULL);
    IupSetAttribute(d.text, "VALUE", initial ? initial : "");
    IupSetAttribute(d.text, "EXPAND", "HORIZONTAL");
    IupSetCallback(d.text, "GETFOCUS_CB", (Icallback)input_focus_cb);

    Ihandle *ok = IupButton("确定", NULL);
    Ihandle *cancel = IupButton("取消", NULL);
    IupSetCallback(ok, "ACTION", (Icallback)input_ok_cb);
    IupSetCallback(cancel, "ACTION", (Icallback)input_cancel_cb);
    IupSetAttribute(ok, "DATA", (char *)&d);
    IupSetAttribute(cancel, "DATA", (char *)&d);
    IupSetAttribute(ok, "MARGIN", "0x0");
    IupSetAttribute(cancel, "MARGIN", "0x0");
    IupSetAttribute(d.text, "DATA", (char *)&d);

    Ihandle *vbox = IupVbox(
        IupLabel(prompt),
        d.text,
        IupHbox(horizontal_fill(), ok, cancel, horizontal_fill(), NULL),
        NULL
    );
    IupSetAttribute(vbox, "MARGIN", "15x15");
    IupSetAttribute(vbox, "GAP", "10");
    IupSetAttribute(IupGetChild(vbox, 2), "GAP", "30");
    IupSetAttribute(IupGetChild(vbox, 2), "MARGIN", "x5");

    d.dlg = IupDialog(vbox);
    IupSetAttribute(d.dlg, "TITLE", title);
    IupSetAttribute(d.dlg, "MODAL", "YES");
    IupSetAttribute(d.dlg, "RASTERSIZE", "340x160");
    IupSetAttributeHandle(d.dlg, "PARENTDIALOG", gui->dlg);
    IupSetAttribute(d.dlg, "DATA", (char *)&d);
    IupSetCallback(d.dlg, "K_ANY", (Icallback)input_k_any);
    IupSetCallback(d.dlg, "SHOW_CB", (Icallback)input_show_cb);
    IupSetCallback(d.dlg, "POSTMESSAGE_CB", (Icallback)input_postmessage_cb);

    IupMap(d.dlg);
#ifdef _WIN32
    set_window_icon(d.dlg);
#endif
    IupPopup(d.dlg, IUP_CENTERPARENT, IUP_CENTERPARENT);
    IupDestroy(d.dlg);

    if (d.confirmed) {
        return xstrdup(d.result);
    }
    return NULL;
}

/* ─────────────────── 状态栏 ─────────────────── */

void gui_status(const char *fmt, ...) {
    if (!gui || !gui->status_label) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    gui->pending_autosave_status = false;
    if (gui->status_timer) IupSetAttribute(gui->status_timer, "RUN", "NO");
    gui->status_at = tick_ms();
    IupStoreAttribute(gui->status_label, "VALUE", buf);
}

static int status_timer_cb(Ihandle *timer) {
    IupSetAttribute(timer, "RUN", "NO");
    if (gui && gui->pending_autosave_status) {
        gui->pending_autosave_status = false;
        IupSetAttribute(gui->status_label, "VALUE", "已自动保存");
    }
    return IUP_DEFAULT;
}

static void autosave_success_status(void) {
    unsigned long long elapsed = tick_ms() - gui->status_at;
    if (elapsed >= 3000) {
        gui_status("已自动保存");
    } else {
        gui->pending_autosave_status = true;
        IupSetInt(gui->status_timer, "TIME", (int)(3000 - elapsed));
        IupSetAttribute(gui->status_timer, "RUN", "YES");
    }
}

/* ─────────────────── 结果刷新 ─────────────────── */

static void refresh_results(void) {
    if (!gui || !g_app) return;
    if (g_app->current_set < 0 || g_app->current_set >= g_app->set_count) return;

    CalcResult r = calc_set(&g_app->sets[g_app->current_set]);
    char buf[128];
    int p = g_app->precision;

    if (r.percent_count > 0) {
        snprintf(buf, sizeof(buf), "%.*f", p, r.avg_grade);
        IupStoreAttribute(gui->result_labels[0], "VALUE", buf);
        snprintf(buf, sizeof(buf), "%.*f", p, r.weighted_avg);
        IupStoreAttribute(gui->result_labels[1], "VALUE", buf);
    } else {
        IupSetAttribute(gui->result_labels[0], "VALUE", "—");
        IupSetAttribute(gui->result_labels[1], "VALUE", "—");
    }
    if (r.gpa_count > 0) {
        snprintf(buf, sizeof(buf), "%.*f", p, r.gpa);
        IupStoreAttribute(gui->result_labels[2], "VALUE", buf);
    } else {
        IupSetAttribute(gui->result_labels[2], "VALUE", "—");
    }
    if (r.has_data) {
        snprintf(buf, sizeof(buf), "%.2f", r.total_credit);
        IupStoreAttribute(gui->result_labels[3], "VALUE", buf);
        snprintf(buf, sizeof(buf), "%d", r.valid_count);
        IupStoreAttribute(gui->result_labels[4], "VALUE", buf);
    } else {
        IupSetAttribute(gui->result_labels[3], "VALUE", "—");
        IupSetAttribute(gui->result_labels[4], "VALUE", "—");
    }
}

static int results_timer_cb(Ihandle *timer) {
    IupSetAttribute(timer, "RUN", "NO");
    refresh_results();
    return IUP_DEFAULT;
}

static void schedule_results_refresh(void) {
    if (!gui || !gui->results_timer) return;
    IupSetAttribute(gui->results_timer, "RUN", "NO");
    IupSetAttribute(gui->results_timer, "RUN", "YES");
}

/* ─────────────────── 自动保存 ─────────────────── */

static int autosave_timer_cb(Ihandle *timer) {
    if (g_app && g_app->dirty && g_app->config_path[0]) {
        if (config_save(g_app->config_path)) {
            autosave_success_status();
        } else {
            gui_status("自动保存失败");
        }
    }
    IupSetAttribute(timer, "RUN", "NO");
    return IUP_DEFAULT;
}

static void mark_dirty(void) {
    if (!g_app) return;
    g_app->dirty = true;
    if (gui && g_app->current_set >= 0 && g_app->current_set < g_app->set_count) {
        Ihandle *page = IupGetChild(gui->tab_stack, g_app->current_set);
        if (page && IupGetInt(page, "DRAWN_GRID")) {
            Ihandle *layer = IupGetChild(page, 2);
            Ihandle *canvas = layer ? IupGetChild(layer, 0) : NULL;
            if (canvas) IupUpdate(canvas);
        }
    }
    if (g_app->autosave && gui && gui->autosave_timer && g_app->config_path[0]) {
        IupSetAttribute(gui->autosave_timer, "RUN", "NO");
        IupSetAttribute(gui->autosave_timer, "RUN", "YES");
    }
    update_toolbar_state();
}

static int autosave_draw_cb(Ihandle *canvas, float x, float y) {
    (void)x;
    (void)y;
    IupDrawBegin(canvas);
    int width = 0, height = 0;
    IupDrawGetSize(canvas, &width, &height);
    IupDrawParentBackground(canvas);
    if (width < 12 || height < 12) {
        IupDrawEnd(canvas);
        return IUP_DEFAULT;
    }

    int left = 1, right = width - 2;
    int top = 1, bottom = height - 2;
    int diameter = bottom - top;
    if (diameter > right - left - 2) diameter = right - left - 2;
    int knob = diameter - 4;
    IupSetAttribute(canvas, "DRAWSTYLE", "FILL");
    IupSetAttribute(canvas, "DRAWCOLOR", g_app->autosave ? "52 152 219" : "170 181 193");
    IupDrawArc(canvas, left, top, left + diameter, bottom, 0, 360);
    IupDrawArc(canvas, right - diameter, top, right, bottom, 0, 360);
    IupDrawRectangle(canvas, left + diameter / 2, top,
                     right - diameter / 2, bottom);
    IupSetAttribute(canvas, "DRAWCOLOR", "255 255 255");
    int knob_left = g_app->autosave ? right - knob - 2 : left + 2;
    IupDrawArc(canvas, knob_left, top + 2, knob_left + knob, bottom - 2, 0, 360);
    IupDrawEnd(canvas);
    return IUP_DEFAULT;
}

static int result_divider_draw_cb(Ihandle *canvas, float x, float y) {
    (void)x;
    (void)y;
    IupDrawBegin(canvas);
    IupDrawParentBackground(canvas);
    int width = 0, height = 0;
    IupDrawGetSize(canvas, &width, &height);
    IupSetAttribute(canvas, "DRAWCOLOR", "190 200 211");
    if (width > 0 && height > 2)
        IupDrawLine(canvas, width / 2, 1, width / 2, height - 2);
    IupDrawEnd(canvas);
    return IUP_DEFAULT;
}

static int autosave_switch_cb(Ihandle *canvas, int button, int pressed, int x, int y, char *status) {
    (void)x;
    (void)y;
    (void)status;
    if (button != IUP_BUTTON1 || !pressed) return IUP_DEFAULT;
    g_app->autosave = !g_app->autosave;
    if (!g_app->autosave) IupSetAttribute(gui->autosave_timer, "RUN", "NO");
    IupUpdate(canvas);
    focus_dialog_background(gui->dlg);
    mark_dirty();
    gui_status(g_app->autosave ? "自动保存已开启" : "自动保存已关闭；请手动保存更改");
    return IUP_DEFAULT;
}

static int precision_changed_cb(Ihandle *list, char *text, int index, int state) {
    (void)list;
    (void)text;
    if (gui->rebuilding || !state || index < 1 || index > 16) return IUP_DEFAULT;
    if (g_app->precision != index - 1) {
        g_app->precision = index - 1;
        refresh_results();
        mark_dirty();
        gui_status("结果显示 %d 位小数", g_app->precision);
    }
    focus_dialog_background(gui->dlg);
    return IUP_DEFAULT;
}

static int precision_dropdown_cb(Ihandle *list, int state) {
    (void)list;
    if (!state && gui && gui->dlg) focus_dialog_background(gui->dlg);
    return IUP_DEFAULT;
}

static void refresh_settings_controls(void) {
    if (!gui) return;
    gui->rebuilding = true;
    if (gui->precision_list) IupSetInt(gui->precision_list, "VALUE", g_app->precision + 1);
    if (gui->autosave_switch) IupUpdate(gui->autosave_switch);
    gui->rebuilding = false;
}

/* ─────────────────── 行控件回调 ─────────────────── */

/** 每次连续编辑仅在真正发生输入时保存一次快照。 */
static int row_getfocus_cb(Ihandle *text) {
    if (!gui->rebuilding) IupSetInt(text, "EDIT_STARTED", 0);
    return IUP_DEFAULT;
}

static bool begin_edit(Ihandle *text) {
    if (!IupGetInt(text, "EDIT_STARTED")) {
        if (!state_push_undo()) {
            gui_status("内存不足，修改未保存");
            return false;
        }
        IupSetInt(text, "EDIT_STARTED", 1);
        update_toolbar_state();
    }
    return true;
}

static bool valid_numeric_text(const char *value, int max_fraction_digits) {
    int fraction_digits = -1;
    for (const char *p = value; *p; ++p) {
        if (*p == '.' && fraction_digits < 0) fraction_digits = 0;
        else if (isdigit((unsigned char)*p)) {
            if (fraction_digits >= 0 && ++fraction_digits > max_fraction_digits)
                return false;
        } else return false;
    }
    return true;
}

static void restore_text(Ihandle *text, const char *value) {
    gui->rebuilding = true;
    IupSetAttribute(text, "VALUE", value);
    gui->rebuilding = false;
    caret_to_end(text);
}

static bool score_is_valid(const Course *course) {
    if (!course->score_str[0]) return true;
    if (course->type == SCORE_FIVELEVEL) return is_valid_fivelevel(course->score_str);
    if (course->type == SCORE_PASSFAIL) return is_valid_passfail(course->score_str);
    return course->type == SCORE_PERCENT && score_to_number(course) >= 0.0;
}

static void score_validation_color(Ihandle *text, const Course *course) {
    IupSetAttribute(text, "BGCOLOR", score_is_valid(course) ? "255 255 255" : "255 229 229");
}

static int course_index_from_control(Ihandle *control) {
    for (Ihandle *item = control; item; item = IupGetParent(item)) {
        const char *index = IupGetAttribute(item, "ROW_COURSE_IDX");
        if (index) return atoi(index);
    }
    return -1;
}

static int score_killfocus_cb(Ihandle *text) {
    if (gui->rebuilding) return IUP_DEFAULT;
    int si = IupGetInt(text, "SET_IDX");
    int ci = course_index_from_control(text);
    if (si >= 0 && si < g_app->set_count && ci >= 0 && ci < g_app->sets[si].count) {
        Course *course = &g_app->sets[si].courses[ci];
        if (!score_is_valid(course)) {
            gui_status(course->type == SCORE_PERCENT
                ? "百分制无效：请填 0-100 的数字"
                : course->type == SCORE_FIVELEVEL
                    ? "五级制无效：请填 优秀/良好/中等/及格/不及格"
                    : "两级制无效：请填 通过/不通过");
        }
    }
    caret_to_end(text);
    return IUP_DEFAULT;
}

/** 课程名输入变化 */
static int name_changed_cb(Ihandle *text) {
    if (gui->rebuilding) return IUP_DEFAULT;
    int si = IupGetInt(text, "SET_IDX");
    int ci = course_index_from_control(text);
    if (si < 0 || ci < 0) return IUP_DEFAULT;

    const char *v = IupGetAttribute(text, "VALUE");
    Course *c = &g_app->sets[si].courses[ci];
    if (v && (strlen(v) >= MAX_COURSE_NAME || strpbrk(v, "|\r\n"))) {
        restore_text(text, c->name);
        gui_status("课程名不能包含 | 或换行，最多 127 字节");
        return IUP_DEFAULT;
    }
    if (!begin_edit(text)) {
        restore_text(text, c->name);
        return IUP_DEFAULT;
    }
    strncpy(c->name, v ? v : "", MAX_COURSE_NAME - 1);
    c->name[MAX_COURSE_NAME - 1] = '\0';
    mark_dirty();
    return IUP_DEFAULT;
}

/** 学分输入变化 (数值验证) */
static int credit_changed_cb(Ihandle *text) {
    if (gui->rebuilding) return IUP_DEFAULT;
    int si = IupGetInt(text, "SET_IDX");
    int ci = course_index_from_control(text);
    if (si < 0 || ci < 0) return IUP_DEFAULT;
    const char *v = IupGetAttribute(text, "VALUE");

    Course *c = &g_app->sets[si].courses[ci];
    /* 验证：允许空、数字和小数点；小数最多两位。 */
    if (v && v[0]) {
        if (!valid_numeric_text(v, 2)) {
            /* 恢复上一个合法值 */
            char buf[MAX_CREDIT_TEXT];
            snprintf(buf, sizeof(buf), "%.2f", c->credit);
            restore_text(text, buf);
            gui_status("学分只能是非负数，最多两位小数");
            return IUP_DEFAULT;
        }
        double val = atof(v);
        if (!isfinite(val)) {
            char buf[MAX_CREDIT_TEXT];
            snprintf(buf, sizeof(buf), "%.2f", c->credit);
            restore_text(text, buf);
            gui_status("数值过大");
            return IUP_DEFAULT;
        }
        if (!begin_edit(text)) {
            char buf[MAX_CREDIT_TEXT];
            snprintf(buf, sizeof(buf), "%.2f", c->credit);
            restore_text(text, buf);
            return IUP_DEFAULT;
        }
        c->credit = val;
    } else {
        if (!begin_edit(text)) {
            char buf[MAX_CREDIT_TEXT];
            snprintf(buf, sizeof(buf), "%.2f", c->credit);
            restore_text(text, buf);
            return IUP_DEFAULT;
        }
        c->credit = 0.0;
    }
    mark_dirty();
    schedule_results_refresh();
    return IUP_DEFAULT;
}

static int credit_killfocus_cb(Ihandle *text) {
    if (gui->rebuilding) return IUP_DEFAULT;
    int si = IupGetInt(text, "SET_IDX");
    int ci = course_index_from_control(text);
    if (si < 0 || si >= g_app->set_count || ci < 0 ||
        ci >= g_app->sets[si].count) return IUP_DEFAULT;
    char buf[MAX_CREDIT_TEXT];
    snprintf(buf, sizeof(buf), "%.2f", g_app->sets[si].courses[ci].credit);
    restore_text(text, buf);
    IupSetInt(text, "EDIT_STARTED", 0);
    return IUP_DEFAULT;
}

/** 成绩输入变化 */
static int score_changed_cb(Ihandle *text) {
    if (gui->rebuilding) return IUP_DEFAULT;
    int si = IupGetInt(text, "SET_IDX");
    int ci = course_index_from_control(text);
    if (si < 0 || ci < 0) return IUP_DEFAULT;
    const char *v = IupGetAttribute(text, "VALUE");
    Course *c = &g_app->sets[si].courses[ci];

    if (v && (strlen(v) >= MAX_SCORE_LEN || strpbrk(v, "|\r\n"))) {
        restore_text(text, c->score_str);
        gui_status("成绩输入太长或含有分隔符");
        return IUP_DEFAULT;
    }

    if (v && v[0]) {
        if (c->type == SCORE_PERCENT) {
            /* 百分制：验证数字范围 */
            if (!valid_numeric_text(v, MAX_SCORE_LEN)) {
                restore_text(text, c->score_str);
                gui_status("百分制成绩只能是 0-100 的数字");
                return IUP_DEFAULT;
            }
            double val = atof(v);
            if (!isfinite(val)) {
                restore_text(text, c->score_str);
                gui_status("数值过大");
                return IUP_DEFAULT;
            }
            if (val > 100.0) {
                restore_text(text, c->score_str);
                gui_status("成绩不能超过 100");
                return IUP_DEFAULT;
            }
        }
        if (!begin_edit(text)) {
            restore_text(text, c->score_str);
            return IUP_DEFAULT;
        }
        strncpy(c->score_str, v, MAX_SCORE_LEN - 1);
        c->score_str[MAX_SCORE_LEN - 1] = '\0';
    } else {
        if (!begin_edit(text)) {
            restore_text(text, c->score_str);
            return IUP_DEFAULT;
        }
        c->score_str[0] = '\0';
    }
    mark_dirty();
    score_validation_color(text, c);
    schedule_results_refresh();
    return IUP_DEFAULT;
}

static bool change_course_type(int si, int ci, ScoreType new_type, Ihandle *score_text) {
    if (si < 0 || si >= g_app->set_count || ci < 0 ||
        ci >= g_app->sets[si].count || new_type < SCORE_PERCENT ||
        new_type > SCORE_PASSFAIL) return false;
    Course *c = &g_app->sets[si].courses[ci];
    if (c->type == new_type) return true;

    if (!state_push_undo()) {
        gui_status("内存不足，评分制未修改");
        return false;
    }
    c->type = new_type;

    /* 切换类型时清空成绩 (因为不同评分制的成绩格式不兼容) */
    c->score_str[0] = '\0';

    /* 找到对应的成绩输入框，清空并更新悬停提示。 */
    if (score_text) {
        IupSetAttribute(score_text, "VALUE", "");
        const char *cue = "";
        switch (new_type) {
            case SCORE_PERCENT:   cue = "0 - 100"; break;
            case SCORE_FIVELEVEL: cue = "优秀/良好/中等/及格/不及格"; break;
            case SCORE_PASSFAIL:  cue = "通过/不通过"; break;
        }
        IupSetAttribute(score_text, "TIP", cue);
        IupSetAttribute(score_text, "BGCOLOR", "255 255 255");
    }

    const char *type_name = "百分制";
    if (new_type == SCORE_FIVELEVEL) type_name = "五级制";
    else if (new_type == SCORE_PASSFAIL) type_name = "两级制";
    gui_status("已切换为%s，成绩已清空", type_name);

    mark_dirty();
    refresh_results();
    update_toolbar_state();
    return true;
}

/** 类型单选切换 */
static int type_toggle_cb(Ihandle *toggle, int state) {
    if (gui->rebuilding || state != 1) return IUP_DEFAULT;
    int si = IupGetInt(toggle, "SET_IDX");
    int ci = course_index_from_control(toggle);
    if (si < 0 || si >= g_app->set_count || ci < 0 || ci >= g_app->sets[si].count) return IUP_DEFAULT;
    ScoreType old_type = g_app->sets[si].courses[ci].type;
    ScoreType new_type = (ScoreType)IupGetInt(toggle, "SCORE_TYPE");
    Ihandle *score_text = (Ihandle *)IupGetAttribute(toggle, "SCORE_TEXT");
    if (!change_course_type(si, ci, new_type, score_text)) {
        Ihandle *old_toggle = IupGetChild(IupGetParent(toggle), (int)old_type);
        if (old_toggle) IupSetInt(old_toggle, "VALUE", 1);
    }
    return IUP_DEFAULT;
}

/** 删除行按钮 */
static void delete_course(int si, int ci) {
    if (si < 0 || ci < 0) return;

    if (!gui_confirm("删除课程", "确定要删除这门课程吗？")) return;

    if (!state_remove_course(si, ci)) return;
    IupPostMessage(gui->dlg, "REMOVE_COURSE_ROW", si, (double)ci, NULL);
    mark_dirty();
    gui_status("已删除课程");
}

static int del_row_cb(Ihandle *btn) {
    delete_course(IupGetInt(btn, "SET_IDX"), course_index_from_control(btn));
    return IUP_DEFAULT;
}

/** 添加课程按钮 */
static void add_course(int si) {
    if (si < 0) return;
    if (state_add_course(si) < 0) {
        gui_status("已达课程上限或内存不足");
        return;
    }
    IupPostMessage(gui->dlg, "APPEND_COURSE_ROWS", si, 0.0, NULL);
    mark_dirty();
}

static int add_row_cb(Ihandle *btn) {
    add_course(IupGetInt(btn, "SET_IDX"));
    return IUP_DEFAULT;
}

/* ─────────────────── 行创建 ─────────────────── */

static Ihandle *create_row(int si, int ci) {
    const Course *c = &g_app->sets[si].courses[ci];
    char buf[MAX_CREDIT_TEXT];

    /* 编号 */
    snprintf(buf, sizeof(buf), "%d", ci + 1);
    Ihandle *num_lbl = IupLabel("");
    IupStoreAttribute(num_lbl, "TITLE", buf);
    IupSetAttribute(num_lbl, "RASTERSIZE", "30x");
    IupSetAttribute(num_lbl, "ALIGNMENT", "ACENTER");

    /* 课程名 */
    Ihandle *name_text = IupText(NULL);
    IupSetAttribute(name_text, "VALUE", c->name);
    IupSetAttribute(name_text, "EXPAND", "HORIZONTAL");
    IupSetStrf(name_text, "RASTERSIZE", "%dx", min_course_name_width);
    IupSetAttribute(name_text, "PADDING", "6x5");
    IupSetAttribute(name_text, "BGCOLOR", "255 255 255");
    IupSetAttribute(name_text, "TIP", "课程名可留空");
    IupSetInt(name_text, "SET_IDX", si);
    IupSetInt(name_text, "COURSE_FIELD", 1);
    IupSetCallback(name_text, "VALUECHANGED_CB", (Icallback)name_changed_cb);
    IupSetCallback(name_text, "GETFOCUS_CB", (Icallback)row_getfocus_cb);
    IupSetCallback(name_text, "KILLFOCUS_CB", (Icallback)text_killfocus_cb);

    /* 学分 */
    Ihandle *credit_text = IupText(NULL);
    snprintf(buf, sizeof(buf), "%.2f", c->credit);
    IupStoreAttribute(credit_text, "VALUE", buf);
    IupSetAttribute(credit_text, "RASTERSIZE", "60x");
    IupSetAttribute(credit_text, "PADDING", "4x5");
    IupSetAttribute(credit_text, "BGCOLOR", "255 255 255");
    IupSetAttribute(credit_text, "TIP", "非负数，最多两位小数；0 表示不计入统计");
    IupSetInt(credit_text, "SET_IDX", si);
    IupSetInt(credit_text, "COURSE_FIELD", 2);
    IupSetCallback(credit_text, "VALUECHANGED_CB", (Icallback)credit_changed_cb);
    IupSetCallback(credit_text, "GETFOCUS_CB", (Icallback)row_getfocus_cb);
    IupSetCallback(credit_text, "KILLFOCUS_CB", (Icallback)credit_killfocus_cb);

    /* 成绩 */
    Ihandle *score_text = IupText(NULL);
    IupSetAttribute(score_text, "VALUE", c->score_str);
    IupSetAttribute(score_text, "RASTERSIZE", "80x");
    IupSetAttribute(score_text, "PADDING", "4x5");
    const char *cue = "0 - 100";
    if (c->type == SCORE_FIVELEVEL) cue = "优秀/良好/中等/及格/不及格";
    else if (c->type == SCORE_PASSFAIL) cue = "通过/不通过";
    IupSetAttribute(score_text, "TIP", cue);
    score_validation_color(score_text, c);
    IupSetInt(score_text, "SET_IDX", si);
    IupSetInt(score_text, "COURSE_FIELD", 3);
    IupSetCallback(score_text, "VALUECHANGED_CB", (Icallback)score_changed_cb);
    IupSetCallback(score_text, "GETFOCUS_CB", (Icallback)row_getfocus_cb);
    IupSetCallback(score_text, "KILLFOCUS_CB", (Icallback)score_killfocus_cb);

    /* 类型单选 */
    Ihandle *t_percent = IupToggle("百分制", NULL);
    Ihandle *t_five    = IupToggle("五级制", NULL);
    Ihandle *t_pass    = IupToggle("两级制", NULL);
    Ihandle *type_box = IupHbox(t_percent, t_five, t_pass, NULL);
    IupSetAttribute(type_box, "MARGIN", "0x0");
    Ihandle *radio = IupRadio(type_box);

    Ihandle *toggles[3] = {t_percent, t_five, t_pass};
    ScoreType types[3] = {SCORE_PERCENT, SCORE_FIVELEVEL, SCORE_PASSFAIL};
    for (int i = 0; i < 3; i++) {
        IupSetInt(toggles[i], "SET_IDX", si);
        IupSetInt(toggles[i], "SCORE_TYPE", (int)types[i]);
        IupSetAttribute(toggles[i], "SCORE_TEXT", (char *)score_text);
        IupSetCallback(toggles[i], "ACTION", (Icallback)type_toggle_cb);
        if (c->type == types[i]) {
            IupSetInt(toggles[i], "VALUE", 1);
        }
    }

    /* 删除按钮 */
    Ihandle *del_btn = action_button("×", false);
    IupSetAttribute(del_btn, "RASTERSIZE", "28x28");
    IupSetAttribute(del_btn, "PADDING", "0x0");
    IupSetAttribute(del_btn, "ALIGNMENT", "ACENTER");
    IupSetAttribute(del_btn, "FONT", "Microsoft YaHei UI Bold 14");
    IupSetAttribute(del_btn, "TIP", "删除该课程");
    IupSetAttribute(del_btn, "TIPFONT", "Microsoft YaHei UI 10");
    style_danger_button(del_btn);
    IupSetInt(del_btn, "SET_IDX", si);
    IupSetCallback(del_btn, "FLAT_ACTION", (Icallback)del_row_cb);

    Ihandle *row = IupHbox(
        num_lbl, name_text, credit_text, score_text, radio, del_btn, NULL
    );
    IupSetAttribute(row, "GAP", "8");
    IupSetAttribute(row, "ALIGNMENT", "ACENTER");
    IupSetAttribute(row, "MARGIN", "0x0");
    IupSetInt(row, "ROW_COURSE_IDX", ci);
    return row;
}

/* ─────────────────── 页重建 ─────────────────── */

static Ihandle *create_course_header(void) {
    Ihandle *header = IupHbox(
        IupLabel("序号"), IupLabel("课程名 (可空)"), IupLabel("学分"),
        IupLabel("成绩"), IupLabel("评分制"), IupLabel(" "), NULL
    );
    IupSetAttribute(header, "GAP", "8");
    IupSetAttribute(header, "ALIGNMENT", "ACENTER");
    IupSetAttribute(header, "MARGIN", "0x0");
    IupSetAttribute(IupGetChild(header, 0), "RASTERSIZE", "30x");
    IupSetAttribute(IupGetChild(header, 1), "EXPAND", "HORIZONTAL");
    IupSetStrf(IupGetChild(header, 1), "RASTERSIZE", "%dx", min_course_name_width);
    IupSetAttribute(IupGetChild(header, 2), "RASTERSIZE", "60x");
    IupSetAttribute(IupGetChild(header, 3), "RASTERSIZE", "80x");
    IupSetAttribute(IupGetChild(header, 4), "RASTERSIZE", "208x");
    IupSetAttribute(IupGetChild(header, 5), "RASTERSIZE", "28x");
    for (int col = 0; col < 6; ++col) {
        IupSetAttribute(IupGetChild(header, col), "FGCOLOR", "100 116 139");
        IupSetAttribute(IupGetChild(header, col), "ALIGNMENT", "ACENTER");
        IupSetCallback(IupGetChild(header, col), "BUTTON_CB", (Icallback)background_click_cb);
    }
    IupSetAttribute(header, "EXPAND", "HORIZONTAL");
    return header;
}

/* 课程表只绘制视口里的课程；仅正在编辑的单元格使用原生输入框。 */

typedef struct {
    int name, credit, score, type, remove, name_width;
} DrawnColumns;

static DrawnColumns drawn_columns(int width) {
    DrawnColumns col;
    /* 原生评分控件实际为 3×64 + 2×8 = 208 像素，剩余宽度给课程名。 */
    col.name_width = width - 2 * DRAWN_MARGIN - 30 - 60 - 80 - 208 - 28 - 5 * 8;
    if (col.name_width < min_course_name_width) col.name_width = min_course_name_width;
    col.name = DRAWN_MARGIN + 30 + 8;
    col.credit = col.name + col.name_width + 8;
    col.score = col.credit + 60 + 8;
    col.type = col.score + 80 + 8;
    col.remove = col.type + 208 + 8;
    return col;
}

static int drawn_grid_total_height(int count) {
    return 5 + count * DRAWN_ROW_PITCH + 32 + 15;
}

static void drawn_grid_metrics(Ihandle *canvas) {
    if (IupGetInt(canvas, "UPDATING_METRICS")) return;
    int si = IupGetInt(canvas, "SET_IDX");
    if (si < 0 || si >= g_app->set_count) return;
    int width = 0, height = 0;
    IupGetIntInt(canvas, "DRAWSIZE", &width, &height);
    if (height < 1) return;
    IupSetInt(canvas, "UPDATING_METRICS", 1);
    IupSetInt(canvas, "YMAX", drawn_grid_total_height(g_app->sets[si].count));
    IupSetInt(canvas, "DY", height);
    IupSetInt(canvas, "LINEY", DRAWN_ROW_PITCH);
    IupSetInt(canvas, "UPDATING_METRICS", 0);
}

static void drawn_grid_fill_layer(Ihandle *layer) {
    if (IupGetInt(layer, "FILLING_LAYER")) return;
    Ihandle *canvas = IupGetChild(layer, 0);
    int width = 0, height = 0, canvas_width = 0, canvas_height = 0;
    IupGetIntInt(layer, "RASTERSIZE", &width, &height);
    Ihandle *page = IupGetParent(layer);
    if (page && IupGetInt(page, "DRAWN_GRID")) {
        int page_width = 0, page_height = 0, header_height = 0;
        IupGetIntInt(page, "RASTERSIZE", &page_width, &page_height);
        IupGetIntInt(IupGetChild(page, 0), "RASTERSIZE", NULL, &header_height);
        if (page_width > 0 && page_height > header_height) {
            width = page_width;
            height = page_height - header_height;
        }
    }
#ifdef _WIN32
    HWND window = (HWND)IupGetAttribute(canvas, "WID");
    RECT rect;
    if (!window || !GetWindowRect(window, &rect)) return;
    canvas_width = rect.right - rect.left;
    canvas_height = rect.bottom - rect.top;
#else
    IupGetIntInt(canvas, "RASTERSIZE", &canvas_width, &canvas_height);
#endif
    if (width < 1 || height < 1 ||
        (width == canvas_width && height == canvas_height)) return;
    IupSetInt(layer, "FILLING_LAYER", 1);
#ifdef _WIN32
    SetWindowPos(window, NULL, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
#else
    IupSetStrf(canvas, "RASTERSIZE", "%dx%d", width, height);
#endif
    drawn_grid_metrics(canvas);
    IupSetInt(layer, "FILLING_LAYER", 0);
}

static void drawn_text(Ihandle *canvas, const char *value, int x, int y, int width,
                       int height, bool centered, const char *color,
                       int font_height) {
    int text_width = 0, text_height = font_height;
    if (centered) IupDrawGetTextSize(canvas, value, -1, &text_width, &text_height);
    if (centered) x += (width - text_width) / 2;
    y += (height - text_height) / 2;
    IupSetAttribute(canvas, "DRAWCOLOR", color);
    IupDrawText(canvas, value, -1, x, y, centered ? text_width : width, text_height);
}

static void drawn_field(Ihandle *canvas, const char *value, int x, int y, int width, bool invalid, int font_height) {
    y += 2;
    const int height = 23;
    IupSetAttribute(canvas, "DRAWCOLOR", invalid ? "255 229 229" : "255 255 255");
    IupSetAttribute(canvas, "DRAWSTYLE", "FILL");
    IupDrawRectangle(canvas, x, y, x + width - 1, y + height - 1);
    IupSetAttribute(canvas, "DRAWCOLOR", "155 165 174");
    IupSetAttribute(canvas, "DRAWSTYLE", "STROKE");
    IupDrawRectangle(canvas, x, y, x + width - 1, y + height - 1);
    IupDrawSetClipRect(canvas, x + 5, y + 2, x + width - 5, y + height - 3);
    drawn_text(canvas, value, x + 6, y, width - 12, height, false, "34 44 56", font_height);
    IupDrawResetClip(canvas);
}

static int drawn_grid_action_cb(Ihandle *canvas, float posx, float posy) {
    (void)posx;
    (void)posy;
    int si = IupGetInt(canvas, "SET_IDX");
    if (!g_app || si < 0 || si >= g_app->set_count) return IUP_DEFAULT;
    int width = 0, height = 0;
    IupDrawBegin(canvas);
    IupDrawGetSize(canvas, &width, &height);
    IupSetAttribute(canvas, "DRAWCOLOR", "245 247 250");
    IupSetAttribute(canvas, "DRAWSTYLE", "FILL");
    IupDrawRectangle(canvas, 0, 0, width - 1, height - 1);
    IupSetAttribute(canvas, "DRAWFONT", "Microsoft YaHei UI 10");
    int font_width = 0, font_height = 0;
    IupDrawGetTextSize(canvas, "Hg", -1, &font_width, &font_height);
    IupSetAttribute(canvas, "DRAWFONT", "Microsoft YaHei UI Bold 14");
    int glyph_width = 0, glyph_height = 0;
    IupDrawGetTextSize(canvas, "×", -1, &glyph_width, &glyph_height);
    IupSetAttribute(canvas, "DRAWFONT", "Microsoft YaHei UI 10");
    DrawnColumns col = drawn_columns(width);
    int top = (int)IupGetFloat(canvas, "POSY");
    const ScoreSet *set = &g_app->sets[si];
    int first = top / DRAWN_ROW_PITCH;
    if (first < 0) first = 0;
    int last = (top + height) / DRAWN_ROW_PITCH + 1;
    if (last > set->count) last = set->count;
    for (int ci = first; ci < last; ++ci) {
        const Course *course = &set->courses[ci];
        int y = 5 + ci * DRAWN_ROW_PITCH - top;
        char number[24], credit[32];
        snprintf(number, sizeof(number), "%d", ci + 1);
        snprintf(credit, sizeof(credit), "%.2f", course->credit);
        drawn_text(canvas, number, DRAWN_MARGIN, y, 30, DRAWN_ROW_HEIGHT, true, "34 44 56", font_height);
        drawn_field(canvas, course->name, col.name, y, col.name_width, false, font_height);
        drawn_field(canvas, credit, col.credit, y, 60, false, font_height);
        drawn_field(canvas, course->score_str, col.score, y, 80, !score_is_valid(course), font_height);
        static const char *types[] = {"百分制", "五级制", "两级制"};
        for (int type = 0; type < 3; ++type) {
            int x = col.type + type * 72;
            int cy = y + (DRAWN_ROW_HEIGHT - 14) / 2;
            IupSetAttribute(canvas, "DRAWCOLOR", "115 125 135");
            IupSetAttribute(canvas, "DRAWSTYLE", "STROKE");
            IupDrawArc(canvas, x, cy, x + 13, cy + 13, 0, 360);
            if (course->type == (ScoreType)type) {
                IupSetAttribute(canvas, "DRAWCOLOR", "48 130 191");
                IupSetAttribute(canvas, "DRAWSTYLE", "FILL");
                IupDrawArc(canvas, x + 4, cy + 4, x + 9, cy + 9, 0, 360);
            }
            drawn_text(canvas, types[type], x + 18, y, 46, DRAWN_ROW_HEIGHT, false, "34 44 56", font_height);
        }
        bool delete_hover = IupGetInt(canvas, "HOVER_KIND") == 2 && IupGetInt(canvas, "HOVER_ROW") == ci;
        IupSetAttribute(canvas, "DRAWCOLOR", delete_hover ? "163 38 38" : "185 48 48");
        IupSetAttribute(canvas, "DRAWSTYLE", "FILL");
        IupDrawRectangle(canvas, col.remove, y, col.remove + 27, y + DRAWN_ROW_HEIGHT - 1);
        IupSetAttribute(canvas, "DRAWFONT", "Microsoft YaHei UI Bold 14");
        IupSetAttribute(canvas, "DRAWCOLOR", "255 255 255");
        IupDrawText(canvas, "×", -1, col.remove + (28 - glyph_width) / 2,
                    y + (DRAWN_ROW_HEIGHT - glyph_height) / 2,
                    glyph_width, glyph_height);
        IupSetAttribute(canvas, "DRAWFONT", "Microsoft YaHei UI 10");
    }
    int add_y = 5 + set->count * DRAWN_ROW_PITCH - top;
    if (add_y < height && add_y + 32 > 0) {
        int add_x = (width - 200) / 2;
        IupSetAttribute(canvas, "DRAWCOLOR", IupGetInt(canvas, "HOVER_KIND") == 1 ? "41 128 185" : "52 152 219");
        IupSetAttribute(canvas, "DRAWSTYLE", "FILL");
        IupDrawRectangle(canvas, add_x, add_y, add_x + 199, add_y + 31);
        IupSetAttribute(canvas, "DRAWCOLOR", "255 255 255");
        drawn_text(canvas, "＋ 添加课程", add_x, add_y, 200, 32, true, "255 255 255", font_height);
    }
    IupDrawEnd(canvas);
    return IUP_DEFAULT;
}

static int drawn_grid_scroll_cb(Ihandle *canvas, int op, float posx, float posy) {
    (void)op;
    (void)posx;
    (void)posy;
    hide_drawn_editor(canvas);
    IupUpdate(canvas);
    return IUP_DEFAULT;
}

static int drawn_grid_resize_cb(Ihandle *canvas, int width, int height) {
    (void)width;
    (void)height;
    if (!g_app || IupGetInt(canvas, "UPDATING_METRICS")) return IUP_DEFAULT;
    Ihandle *layer = IupGetParent(canvas);
    if (layer && IupGetInt(layer, "FILLING_LAYER")) return IUP_DEFAULT;
    drawn_grid_metrics(canvas);
    position_drawn_editor(canvas);
    Ihandle *page = layer ? IupGetParent(layer) : NULL;
    if (page && IupGetInt(page, "DRAWN_GRID") && gui && gui->dlg &&
        !IupGetInt(gui->dlg, "GRID_RESIZE_QUEUED")) {
        IupSetInt(gui->dlg, "GRID_RESIZE_QUEUED", 1);
        IupPostMessage(gui->dlg, "SYNC_DRAWN_GRID_SIZE", 0, 0.0, NULL);
    }
    return IUP_DEFAULT;
}

static int drawn_grid_button_cb(Ihandle *canvas, int button, int pressed, int x, int y, char *status) {
    (void)status;
    if (button != IUP_BUTTON1 || !pressed || !g_app) return IUP_DEFAULT;
    int si = IupGetInt(canvas, "SET_IDX");
    if (si < 0 || si >= g_app->set_count) return IUP_DEFAULT;
    int width = 0, height = 0;
    IupGetIntInt(canvas, "DRAWSIZE", &width, &height);
    int content_y = y + (int)IupGetFloat(canvas, "POSY") - 5;
    if (content_y < 0) {
        hide_drawn_editor(canvas);
        IupUpdate(canvas);
        focus_dialog_background(gui->dlg);
        return IUP_DEFAULT;
    }
    int ci = content_y / DRAWN_ROW_PITCH;
    if (ci == g_app->sets[si].count) {
        hide_drawn_editor(canvas);
        IupUpdate(canvas);
        int add_x = (width - 200) / 2;
        if (x >= add_x && x < add_x + 200 && content_y % DRAWN_ROW_PITCH < 32) add_course(si);
        else focus_dialog_background(gui->dlg);
        return IUP_DEFAULT;
    }
    if (ci < 0 || ci >= g_app->sets[si].count ||
        content_y % DRAWN_ROW_PITCH >= DRAWN_ROW_HEIGHT) {
        hide_drawn_editor(canvas);
        IupUpdate(canvas);
        focus_dialog_background(gui->dlg);
        return IUP_DEFAULT;
    }
    DrawnColumns col = drawn_columns(width);
    if (x >= col.remove && x < col.remove + 28) {
        hide_drawn_editor(canvas);
        delete_course(si, ci);
    } else if (x >= col.type && x < col.type + 208) {
        hide_drawn_editor(canvas);
        int type = (x - col.type) / 72;
        if (type > 2) type = 2;
        change_course_type(si, ci, (ScoreType)type, NULL);
        IupUpdate(canvas);
    } else if (x >= col.name && x < col.name + col.name_width) {
        activate_drawn_editor(canvas, ci, 1);
    } else if (x >= col.credit && x < col.credit + 60) {
        activate_drawn_editor(canvas, ci, 2);
    } else if (x >= col.score && x < col.score + 80) {
        activate_drawn_editor(canvas, ci, 3);
    } else {
        hide_drawn_editor(canvas);
        IupUpdate(canvas);
        focus_dialog_background(gui->dlg);
    }
    return IUP_DEFAULT;
}

static int drawn_grid_motion_cb(Ihandle *canvas, int x, int y, char *status) {
    (void)status;
    int si = IupGetInt(canvas, "SET_IDX");
    if (!g_app || si < 0 || si >= g_app->set_count) return IUP_DEFAULT;
    int width = 0, height = 0;
    IupGetIntInt(canvas, "DRAWSIZE", &width, &height);
    int content_y = y + (int)IupGetFloat(canvas, "POSY") - 5;
    int kind = 0, row = -1;
    const char *tip = "";
    if (content_y >= 0) {
        row = content_y / DRAWN_ROW_PITCH;
        DrawnColumns col = drawn_columns(width);
        if (row < g_app->sets[si].count &&
            content_y % DRAWN_ROW_PITCH < DRAWN_ROW_HEIGHT) {
            const Course *course = &g_app->sets[si].courses[row];
            if (x >= col.remove && x < col.remove + 28) {
                kind = 2;
                tip = "删除该课程";
            } else if (x >= col.name && x < col.name + col.name_width)
                tip = "课程名可留空";
            else if (x >= col.credit && x < col.credit + 60)
                tip = "非负数，最多两位小数；0 表示不计入统计";
            else if (x >= col.score && x < col.score + 80)
                tip = course->type == SCORE_FIVELEVEL
                    ? "优秀/良好/中等/及格/不及格"
                    : course->type == SCORE_PASSFAIL ? "通过/不通过" : "0 - 100";
        } else if (row == g_app->sets[si].count &&
                   content_y % DRAWN_ROW_PITCH < 32 &&
                   x >= (width - 200) / 2 && x < (width + 200) / 2) {
            kind = 1;
        }
    }
    IupSetAttribute(canvas, "TIP", tip);
    if (kind == 0) row = -1;
    if (kind != IupGetInt(canvas, "HOVER_KIND") || row != IupGetInt(canvas, "HOVER_ROW")) {
        IupSetInt(canvas, "HOVER_KIND", kind);
        IupSetInt(canvas, "HOVER_ROW", row);
        IupUpdate(canvas);
    }
    return IUP_DEFAULT;
}

static Ihandle *create_drawn_grid_layer(void) {
    Ihandle *canvas = IupCanvas(NULL);
    IupSetAttribute(canvas, "SCROLLBAR", "VERTICAL");
    IupSetAttribute(canvas, "BORDER", "NO");
    IupSetAttribute(canvas, "EXPAND", "YES");
    IupSetStrf(canvas, "RASTERSIZE", "%dx120", min_course_name_width + DRAWN_HEADER_EXTRA);
    IupSetAttribute(canvas, "CX", "0");
    IupSetAttribute(canvas, "CY", "0");
    IupSetAttribute(canvas, "BGCOLOR", "245 247 250");
    IupSetCallback(canvas, "ACTION", (Icallback)drawn_grid_action_cb);
    IupSetCallback(canvas, "SCROLL_CB", (Icallback)drawn_grid_scroll_cb);
    IupSetCallback(canvas, "RESIZE_CB", (Icallback)drawn_grid_resize_cb);
    IupSetCallback(canvas, "BUTTON_CB", (Icallback)drawn_grid_button_cb);
    IupSetCallback(canvas, "MOTION_CB", (Icallback)drawn_grid_motion_cb);
    IupSetAttribute(canvas, "TIPFONT", "Microsoft YaHei UI 10");
    Ihandle *layer = IupCbox(canvas, NULL);
    IupSetAttribute(layer, "EXPAND", "YES");
    IupSetStrf(layer, "RASTERSIZE", "%dx120", min_course_name_width + DRAWN_HEADER_EXTRA);
    IupSetAttribute(layer, "VISIBLE", "NO");
    IupSetAttribute(layer, "FLOATING", "YES");
    return layer;
}

static Ihandle *create_course_page(void) {
    Ihandle *header_gutter = IupLabel(NULL);
    IupSetAttribute(header_gutter, "RASTERSIZE", "0x0");
    IupSetAttribute(header_gutter, "VISIBLE", "NO");
    Ihandle *header_row = IupHbox(create_course_header(), header_gutter, NULL);
    IupSetAttribute(header_row, "EXPAND", "HORIZONTAL");
    IupSetAttribute(header_row, "GAP", "0");
    IupSetAttribute(header_row, "MARGIN", "0x0");
    Ihandle *fixed_header = IupVbox(header_row, separator(), NULL);
    IupSetAttribute(fixed_header, "MARGIN", "14x5");
    IupSetAttribute(fixed_header, "GAP", "10");
    IupSetAttribute(fixed_header, "EXPAND", "HORIZONTAL");

    Ihandle *courses = IupVbox(NULL);
    IupSetAttribute(courses, "MARGIN", "14x5");
    IupSetAttribute(courses, "GAP", "10");
    IupSetAttribute(courses, "EXPAND", "YES");
    Ihandle *scroll = IupScrollBox(courses);
    IupSetAttribute(scroll, "EXPAND", "YES");
    IupSetCallback(scroll, "RESIZE_CB", (Icallback)course_scroll_resize_cb);
    IupSetCallback(scroll, "BUTTON_CB", (Icallback)background_click_cb);

    Ihandle *page = IupVbox(fixed_header, scroll, create_drawn_grid_layer(), NULL);
    IupSetAttribute(page, "MARGIN", "0x0");
    IupSetAttribute(page, "GAP", "0");
    IupSetAttribute(page, "EXPAND", "YES");
    return page;
}

static void sync_course_header_gutter(Ihandle *page) {
    Ihandle *fixed_header = IupGetChild(page, 0);
    Ihandle *header_row = fixed_header ? IupGetChild(fixed_header, 0) : NULL;
    Ihandle *gutter = header_row ? IupGetChild(header_row, 1) : NULL;
    Ihandle *scroll = IupGetInt(page, "DRAWN_GRID") ? IupGetChild(IupGetChild(page, 2), 0) : IupGetChild(page, 1);
    if (!gutter || !scroll) return;

    int width = 0;
#ifdef _WIN32
    HWND window = (HWND)IupGetAttribute(scroll, "WID");
    if (window && (GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL)) {
        RECT outer, client;
        GetWindowRect(window, &outer);
        GetClientRect(window, &client);
        width = (outer.right - outer.left) - (client.right - client.left);
        if (width < 0) width = 0;
    }
#endif
    if (IupGetInt(gutter, "GUTTER_WIDTH") == width) return;
    IupSetInt(gutter, "GUTTER_WIDTH", width);
    IupSetStrf(gutter, "RASTERSIZE", "%dx0", width);
    IupSetAttribute(gutter, "VISIBLE", width ? "YES" : "NO");
    IupRefresh(fixed_header);
}

static int course_scroll_resize_cb(Ihandle *scroll, int width, int height) {
    (void)width;
    (void)height;
    Ihandle *page = IupGetParent(scroll);
    if (page) sync_course_header_gutter(page);
    return IUP_DEFAULT;
}

static Ihandle *get_stack_page(int pos) {
    /* IupZbox 的子元素从 0 开始索引。 */
    return IupGetChild(gui->tab_stack, pos);
}

static void hide_drawn_editor(Ihandle *canvas) {
    Ihandle *layer = IupGetParent(canvas);
    Ihandle *editor = layer ? IupGetChild(layer, 1) : NULL;
    if (!editor || !IupGetInt(editor, "EDITOR_ACTIVE")) return;
    if (IupGetFocus() == editor && gui->focus_sink) IupSetFocus(gui->focus_sink);
    IupSetInt(editor, "EDITOR_ACTIVE", 0);
    IupSetAttribute(editor, "VISIBLE", "NO");
}

static void position_drawn_editor(Ihandle *canvas) {
    Ihandle *layer = IupGetParent(canvas);
    Ihandle *editor = layer ? IupGetChild(layer, 1) : NULL;
    if (!editor || !IupGetInt(editor, "EDITOR_ACTIVE") || IupGetInt(layer, "POSITIONING_EDITOR")) return;
    int width = 0, height = 0;
    IupGetIntInt(canvas, "DRAWSIZE", &width, &height);
    DrawnColumns col = drawn_columns(width);
    int y = 5 + IupGetInt(editor, "ROW_COURSE_IDX") * DRAWN_ROW_PITCH - (int)IupGetFloat(canvas, "POSY");
    if (y < 0 || y + DRAWN_ROW_HEIGHT > height) {
        hide_drawn_editor(canvas);
        return;
    }
    int field = IupGetInt(editor, "COURSE_FIELD");
    int x = field == 1 ? col.name : field == 2 ? col.credit : col.score;
    int field_width = field == 1 ? col.name_width : field == 2 ? 60 : 80;
    bool layout_changed = IupGetInt(editor, "CX") != x ||
        IupGetInt(editor, "CY") != y + 2 ||
        IupGetInt(editor, "EDITOR_WIDTH") != field_width ||
        !IupGetInt(editor, "VISIBLE");
#ifdef _WIN32
    HWND window = (HWND)IupGetAttribute(editor, "WID");
    HWND canvas_window = (HWND)IupGetAttribute(canvas, "WID");
    RECT actual, canvas_rect;
    bool native_changed = window && canvas_window &&
        GetWindowRect(window, &actual) && GetWindowRect(canvas_window, &canvas_rect) &&
        (actual.left != canvas_rect.left + x || actual.top != canvas_rect.top + y + 2 ||
         actual.right - actual.left != field_width || actual.bottom - actual.top != 23);
    if (!layout_changed && !native_changed) return;
#else
    if (!layout_changed) return;
#endif
    IupSetInt(layer, "POSITIONING_EDITOR", 1);
    if (layout_changed) {
        IupSetInt(editor, "CX", x);
        IupSetInt(editor, "CY", y + 2);
        IupSetInt(editor, "EDITOR_WIDTH", field_width);
        IupSetStrf(editor, "RASTERSIZE", "%dx23", field_width);
        IupSetAttribute(editor, "VISIBLE", "YES");
        /* Windows 只移动编辑框，避免点击时重新布局整个画布。 */
#ifndef _WIN32
        IupRefreshChildren(layer);
        drawn_grid_fill_layer(layer);
#endif
        IupUpdate(canvas);
    }
#ifdef _WIN32
    if (window && IsWindow(window) && canvas_window &&
        GetWindowRect(canvas_window, &canvas_rect)) {
        POINT origin = {canvas_rect.left + x, canvas_rect.top + y + 2};
        ScreenToClient(GetParent(window), &origin);
        SetWindowPos(window, HWND_TOP, origin.x, origin.y, field_width, 23, SWP_NOACTIVATE);
    }
#endif
    IupSetInt(layer, "POSITIONING_EDITOR", 0);
}

static void sync_drawn_page_visibility(void) {
    if (!gui || !g_app || !gui->tab_stack) return;
    for (int si = 0; si < g_app->set_count; ++si) {
        Ihandle *page = get_stack_page(si);
        if (!page || !IupGetInt(page, "DRAWN_GRID")) continue;
        Ihandle *layer = IupGetChild(page, 2);
        Ihandle *canvas = IupGetChild(layer, 0);
        bool current = si == g_app->current_set;
        if (!current) hide_drawn_editor(canvas);
        IupSetAttribute(canvas, "VISIBLE", current ? "YES" : "NO");
        if (current) {
            drawn_grid_fill_layer(layer);
            position_drawn_editor(canvas);
        }
    }
}

static Ihandle *create_drawn_editor(int si, int ci, int field) {
    const Course *course = &g_app->sets[si].courses[ci];
    Ihandle *editor = IupText(NULL);
    char credit_value[MAX_CREDIT_TEXT];
    if (field == 1) IupSetAttribute(editor, "VALUE", course->name);
    else if (field == 2) {
        snprintf(credit_value, sizeof(credit_value), "%.2f", course->credit);
        IupStoreAttribute(editor, "VALUE", credit_value);
    } else IupSetAttribute(editor, "VALUE", course->score_str);
    IupSetAttribute(editor, "PADDING", field == 1 ? "6x5" : "4x5");
    IupSetAttribute(editor, "BGCOLOR", "255 255 255");
    IupSetInt(editor, "SET_IDX", si);
    IupSetInt(editor, "ROW_COURSE_IDX", ci);
    IupSetInt(editor, "COURSE_FIELD", field);
    IupSetCallback(editor, "GETFOCUS_CB", (Icallback)row_getfocus_cb);
    if (field == 1) {
        IupSetAttribute(editor, "TIP", "课程名可留空");
        IupSetCallback(editor, "VALUECHANGED_CB", (Icallback)name_changed_cb);
        IupSetCallback(editor, "KILLFOCUS_CB", (Icallback)text_killfocus_cb);
    } else if (field == 2) {
        IupSetAttribute(editor, "TIP", "非负数，最多两位小数；0 表示不计入统计");
        IupSetCallback(editor, "VALUECHANGED_CB", (Icallback)credit_changed_cb);
        IupSetCallback(editor, "KILLFOCUS_CB", (Icallback)credit_killfocus_cb);
    } else {
        IupSetAttribute(editor, "TIP", course->type == SCORE_FIVELEVEL
            ? "优秀/良好/中等/及格/不及格"
            : course->type == SCORE_PASSFAIL ? "通过/不通过" : "0 - 100");
        score_validation_color(editor, course);
        IupSetCallback(editor, "VALUECHANGED_CB", (Icallback)score_changed_cb);
        IupSetCallback(editor, "KILLFOCUS_CB", (Icallback)score_killfocus_cb);
    }
    return editor;
}

static void activate_drawn_editor(Ihandle *canvas, int ci, int field) {
    int si = IupGetInt(canvas, "SET_IDX");
    if (si < 0 || si >= g_app->set_count || ci < 0 ||
        ci >= g_app->sets[si].count || field < 1 || field > 3) return;
    Ihandle *layer = IupGetParent(canvas);
    int height = 0;
    IupGetIntInt(canvas, "DRAWSIZE", NULL, &height);
    int y = 5 + ci * DRAWN_ROW_PITCH - (int)IupGetFloat(canvas, "POSY");
    if (y < 0) IupSetFloat(canvas, "POSY", 5 + ci * DRAWN_ROW_PITCH);
    else if (y + DRAWN_ROW_HEIGHT > height)
        IupSetFloat(canvas, "POSY", 5 + ci * DRAWN_ROW_PITCH + DRAWN_ROW_HEIGHT - height);
    Ihandle *editor = IupGetChild(layer, 1);
    if (editor && (IupGetInt(editor, "ROW_COURSE_IDX") != ci || IupGetInt(editor, "COURSE_FIELD") != field)) {
        hide_drawn_editor(canvas);
        IupDetach(editor);
        IupDestroy(editor);
        editor = NULL;
    }
    if (!editor) {
        editor = create_drawn_editor(si, ci, field);
        int width = 0;
        IupGetIntInt(canvas, "DRAWSIZE", &width, NULL);
        DrawnColumns col = drawn_columns(width);
        int x = field == 1 ? col.name : field == 2 ? col.credit : col.score;
        int field_width = field == 1 ? col.name_width : field == 2 ? 60 : 80;
        int row_y = 5 + ci * DRAWN_ROW_PITCH - (int)IupGetFloat(canvas, "POSY");
        /* Cbox 子控件在映射前就需要确定坐标和尺寸。 */
        IupSetInt(editor, "CX", x);
        IupSetInt(editor, "CY", row_y + 2);
        IupSetInt(editor, "EDITOR_WIDTH", field_width);
        IupSetStrf(editor, "RASTERSIZE", "%dx23", field_width);
        IupAppend(layer, editor);
        IupMap(editor);
#ifndef _WIN32
        IupRefreshChildren(layer);
#endif
    }
    IupSetInt(editor, "EDITOR_ACTIVE", 1);
    position_drawn_editor(canvas);
    IupSetFocus(editor);
    caret_to_end(editor);
}

static void rebuild_drawn_grid(int si) {
    Ihandle *page = get_stack_page(si);
    Ihandle *scroll = IupGetChild(page, 1);
    Ihandle *layer = IupGetChild(page, 2);
    Ihandle *canvas = IupGetChild(layer, 0);
    if (!IupGetInt(page, "DRAWN_GRID")) {
        Ihandle *vbox = IupGetChild(scroll, 0);
        Ihandle *child;
        if (gui->focus_sink && course_index_from_control(IupGetFocus()) >= 0) IupSetFocus(gui->focus_sink);
        while (vbox && (child = IupGetChild(vbox, 0)) != NULL) {
            IupDetach(child);
            IupDestroy(child);
        }
    }
    float old_pos = IupGetFloat(canvas, "POSY");
    Ihandle *row = IupGetChild(layer, 1);
    if (row) {
        if (gui->focus_sink && course_index_from_control(IupGetFocus()) >= 0) IupSetFocus(gui->focus_sink);
        IupDetach(row);
        IupDestroy(row);
    }
    IupSetInt(canvas, "SET_IDX", si);
    IupSetInt(page, "DRAWN_GRID", 1);
    IupSetInt(page, "RENDERED_COUNT", g_app->sets[si].count);
    IupSetInt(page, "ROWS_READY", 1);
    IupSetAttribute(scroll, "VISIBLE", "NO");
    IupSetAttribute(scroll, "FLOATING", "YES");
    IupSetAttribute(layer, "FLOATING", "NO");
    IupSetAttribute(layer, "VISIBLE", "YES");
    IupRefresh(page);
    drawn_grid_fill_layer(layer);
    drawn_grid_metrics(canvas);
    IupSetFloat(canvas, "POSY", old_pos);
    IupUpdate(canvas);
    sync_course_header_gutter(page);
    sync_drawn_page_visibility();
}

static void update_drawn_grid(int si) {
    Ihandle *page = get_stack_page(si);
    if (!page || !IupGetInt(page, "DRAWN_GRID")) return;
    if (g_app->sets[si].count <= DRAWN_GRID_AFTER) {
        rebuild_page(si);
        return;
    }
    Ihandle *layer = IupGetChild(page, 2);
    Ihandle *canvas = IupGetChild(layer, 0);
    Ihandle *row = IupGetChild(layer, 1);
    if (row) {
        if (gui->focus_sink && course_index_from_control(IupGetFocus()) >= 0) IupSetFocus(gui->focus_sink);
        IupDetach(row);
        IupDestroy(row);
    }
    IupSetInt(page, "RENDERED_COUNT", g_app->sets[si].count);
    drawn_grid_fill_layer(layer);
    drawn_grid_metrics(canvas);
    sync_course_header_gutter(page);
    drawn_grid_fill_layer(layer);
    IupUpdate(canvas);
}

static void rebuild_page(int si) {
    if (!gui || si < 0 || si >= g_app->set_count) return;

    Ihandle *page = get_stack_page(si);
    if (!page) return;
    if (g_app->sets[si].count > DRAWN_GRID_AFTER) {
        rebuild_drawn_grid(si);
        return;
    }
    if (IupGetInt(page, "DRAWN_GRID")) {
        IupSetInt(page, "DRAWN_GRID", 0);
        Ihandle *layer = IupGetChild(page, 2);
        IupSetAttribute(IupGetChild(layer, 0), "VISIBLE", "NO");
        Ihandle *row = IupGetChild(layer, 1);
        if (row) IupSetAttribute(row, "VISIBLE", "NO");
        IupSetAttribute(IupGetChild(page, 2), "VISIBLE", "NO");
        IupSetAttribute(IupGetChild(page, 2), "FLOATING", "YES");
        IupSetAttribute(IupGetChild(page, 1), "FLOATING", "NO");
        IupSetAttribute(IupGetChild(page, 1), "VISIBLE", "YES");
        IupRefresh(page);
    }

    /* 表头留在 ScrollBox 外，只替换滚动区域中的课程控件。 */
    Ihandle *scroll = IupGetChild(page, 1);
    Ihandle *vbox = scroll ? IupGetChild(scroll, 0) : NULL;
    if (!vbox) return;
    int total = g_app->sets[si].count;
    float old_pos = IupGetFloat(scroll, "POSY");
    IupSetInt(page, "ROWS_READY", 0);
    gui->rebuilding = true;
    Ihandle *old_child;
    while ((old_child = IupGetChild(vbox, 0)) != NULL) {
        IupDetach(old_child);
        IupDestroy(old_child);
    }
    for (int ci = 0; ci < total; ci++) {
        Ihandle *row = create_row(si, ci);
        IupAppend(vbox, row);
    }

    if (total == 0) {
        Ihandle *empty = IupLabel("当前无课程");
        IupSetAttribute(empty, "FGCOLOR", "100 116 139");
        IupSetAttribute(empty, "ALIGNMENT", "ACENTER");
        IupSetCallback(empty, "BUTTON_CB", (Icallback)background_click_cb);
        Ihandle *empty_row = IupHbox(horizontal_fill(), empty, horizontal_fill(), NULL);
        IupSetAttribute(empty_row, "EXPAND", "HORIZONTAL");
        Ihandle *empty_space = IupVbox(IupFill(), empty_row, IupFill(), NULL);
        IupSetAttribute(empty_space, "EXPAND", "YES");
        IupSetAttribute(empty_space, "ALIGNMENT", "ACENTER");
        IupAppend(vbox, empty_space);
    } else {
        IupAppend(vbox, IupFill());
    }

    /* 添加课程按钮 */
    Ihandle *add_btn = action_button("＋ 添加课程", true);
    IupSetAttribute(add_btn, "BORDER", "NO");
    IupSetAttribute(add_btn, "RASTERSIZE", "200x32");
    IupSetInt(add_btn, "SET_IDX", si);
    IupSetCallback(add_btn, "FLAT_ACTION", (Icallback)add_row_cb);
    Ihandle *add_row = IupHbox(horizontal_fill(), add_btn, horizontal_fill(), NULL);
    IupSetAttribute(add_row, "EXPAND", "HORIZONTAL");
    IupAppend(vbox, add_row);

    for (int i = 0; i < IupGetChildCount(vbox); ++i) IupMap(IupGetChild(vbox, i));
    IupSetInt(page, "RENDERED_COUNT", total);
    IupRefresh(page);
    if (old_pos > 0) IupSetStrf(scroll, "SCROLLTO", "0,%d", (int)old_pos);
    IupSetInt(page, "ROWS_READY", 1);
    gui->rebuilding = false;
    sync_course_header_gutter(page);
    sync_drawn_page_visibility();
}

static void append_course_rows(int si) {
    if (!gui || si < 0 || si >= g_app->set_count) return;
    Ihandle *page = get_stack_page(si);
    if (page && IupGetInt(page, "DRAWN_GRID")) {
        update_drawn_grid(si);
        return;
    }
    Ihandle *scroll = page ? IupGetChild(page, 1) : NULL;
    Ihandle *vbox = scroll ? IupGetChild(scroll, 0) : NULL;
    if (!vbox) return;

    int rendered = IupGetInt(page, "RENDERED_COUNT");
    int total = g_app->sets[si].count;
    if (total > DRAWN_GRID_AFTER) {
        rebuild_page(si);
        return;
    }
    if (!IupGetInt(page, "ROWS_READY") || rendered <= 0 || rendered > total || IupGetChildCount(vbox) != rendered + 2) {
        rebuild_page(si);
        return;
    }
    if (rendered == total) return;

    Ihandle *fill = IupGetChild(vbox, rendered);
    gui->rebuilding = true;
    for (int ci = rendered; ci < total; ++ci) {
        Ihandle *row = create_row(si, ci);
        if (!IupInsert(vbox, fill, row)) {
            IupDestroy(row);
            gui->rebuilding = false;
            rebuild_page(si);
            return;
        }
        IupMap(row);
    }
    IupSetInt(page, "RENDERED_COUNT", total);
    gui->rebuilding = false;
    IupRefreshChildren(page);
    sync_course_header_gutter(page);
}

static void set_course_row_index(Ihandle *row, int ci) {
    Ihandle *number = IupGetChild(row, 0);
    if (number) IupSetStrf(number, "TITLE", "%d", ci + 1);
    IupSetInt(row, "ROW_COURSE_IDX", ci);
}

static void remove_course_row(int si, int ci) {
    if (!gui || si < 0 || si >= g_app->set_count) return;
    Ihandle *page = get_stack_page(si);
    if (page && IupGetInt(page, "DRAWN_GRID")) {
        update_drawn_grid(si);
        return;
    }
    Ihandle *scroll = page ? IupGetChild(page, 1) : NULL;
    Ihandle *vbox = scroll ? IupGetChild(scroll, 0) : NULL;
    if (!vbox) return;

    int rendered = IupGetInt(page, "RENDERED_COUNT");
    int total = g_app->sets[si].count;
    if (!IupGetInt(page, "ROWS_READY") || total == 0 || rendered != total + 1 || ci < 0 || ci >= rendered || IupGetChildCount(vbox) != rendered + 2) {
        rebuild_page(si);
        return;
    }

    Ihandle *row = IupGetChild(vbox, ci);
    gui->rebuilding = true;
    IupDetach(row);
    IupDestroy(row);
    for (int index = ci; index < total; ++index) set_course_row_index(IupGetChild(vbox, index), index);
    IupSetInt(page, "RENDERED_COUNT", total);
    gui->rebuilding = false;
    IupRefreshChildren(page);
    sync_course_header_gutter(page);
}

static void ensure_page_ready(int si) {
    Ihandle *page = get_stack_page(si);
    if (page && !IupGetInt(page, "ROWS_READY")) rebuild_page(si);
}

static void rebuild_current_page(void) {
    if (g_app && g_app->current_set >= 0) rebuild_page(g_app->current_set);
}

static void rebuild_stack_pages(void) {
    gui->rebuilding = true;
    while (IupGetChildCount(gui->tab_stack) > g_app->set_count) {
        int last = IupGetChildCount(gui->tab_stack) - 1;
        Ihandle *child = IupGetChild(gui->tab_stack, last);
        IupDetach(child);
        IupDestroy(child);
    }
    while (IupGetChildCount(gui->tab_stack) < g_app->set_count) {
        Ihandle *page = create_course_page();
        IupAppend(gui->tab_stack, page);
        IupMap(page);
    }
    for (int i = 0; i < g_app->set_count; ++i)
        IupSetInt(get_stack_page(i), "ROWS_READY", 0);
    gui->rebuilding = false;
    IupSetInt(gui->tab_stack, "VALUEPOS", g_app->current_set);
    rebuild_tab_bar();
    rebuild_current_page();
    refresh_results();
    update_toolbar_state();
}

/* ─────────────────── Tab 切换 ─────────────────── */

static int tab_button_cb(Ihandle *toggle) {
    if (gui->rebuilding) return IUP_DEFAULT;
    int si = IupGetInt(toggle, "SET_IDX");
    if (si < 0 || si >= g_app->set_count) return IUP_DEFAULT;
    if (si == g_app->current_set) return IUP_DEFAULT;

    g_app->current_set = si;
    IupSetInt(gui->tab_stack, "VALUEPOS", si);
    ensure_page_ready(si);
    refresh_results();
    refresh_tab_selection();
    return IUP_DEFAULT;
}

/* 汉堡菜单点击项 */
static int hamburger_item_cb(Ihandle *item) {
    int si = IupGetInt(item, "SET_IDX");
    if (si >= 0 && si < g_app->set_count) {
        IupHide(IupGetDialog(item));
        g_app->current_set = si;
        IupSetInt(gui->tab_stack, "VALUEPOS", si);
        ensure_page_ready(si);
        refresh_results();
        refresh_tab_selection();
    }
    return IUP_DEFAULT;
}

static int hamburger_btn_cb(Ihandle *btn) {
    Ihandle *items = IupVbox(NULL);
    IupSetAttribute(items, "MARGIN", "6x6");
    IupSetAttribute(items, "GAP", "3");
    int item_width = 210;
    for (int i = 0; i < g_app->set_count; ++i) {
        int text_width = measure_utf8_text_width(g_app->sets[i].name, 10, false,
                                                  (int)strlen(g_app->sets[i].name) * 8);
        if (item_width < text_width + 24) item_width = text_width + 24;
    }
    for (int i = 0; i < g_app->set_count; i++) {
        Ihandle *item = action_button(g_app->sets[i].name, i == g_app->current_set);
        IupSetStrf(item, "RASTERSIZE", "%dx32", item_width);
        IupSetAttribute(item, "ALIGNMENT", "ALEFT");
        IupSetAttribute(item, "TIP", g_app->sets[i].name);
        IupSetInt(item, "SET_IDX", i);
        IupSetCallback(item, "FLAT_ACTION", (Icallback)hamburger_item_cb);
        IupAppend(items, item);
    }
    Ihandle *scroll = IupScrollBox(items);
    IupSetAttribute(scroll, "SCROLLBAR", "YES");
    int menu_height = g_app->set_count * 35 + 9;
    bool vertical_scroll = menu_height > 272;
    if (menu_height > 272) menu_height = 272;
    int scrollbar_width = 0;
    int max_menu_width = 720;
#ifdef _WIN32
    MONITORINFO monitor = {0};
    monitor.cbSize = sizeof(monitor);
    HWND parent_window = (HWND)IupGetAttribute(gui->dlg, "WID");
    bool have_monitor = parent_window &&
        GetMonitorInfoW(MonitorFromWindow(parent_window, MONITOR_DEFAULTTONEAREST), &monitor);
    if (have_monitor) {
        int monitor_limit = monitor.rcWork.right - monitor.rcWork.left - 40;
        if (max_menu_width > monitor_limit) max_menu_width = monitor_limit;
    }
    if (vertical_scroll) scrollbar_width = GetSystemMetrics(SM_CXVSCROLL);
#else
    bool have_monitor = false;
#endif
    int menu_width = item_width + 12 + scrollbar_width;
    if (menu_width > max_menu_width) menu_width = max_menu_width;
    IupSetStrf(scroll, "RASTERSIZE", "%dx%d", menu_width, menu_height);
    IupSetStrf(scroll, "MAXSIZE", "%dx%d", menu_width, menu_height);
    Ihandle *menu = IupDialog(scroll);
    IupSetAttribute(menu, "TITLE", "选择成绩组");
    IupSetAttribute(menu, "RESIZE", "NO");
    IupSetAttributeHandle(menu, "PARENTDIALOG", gui->dlg);
    IupMap(menu);
#ifdef _WIN32
    set_window_icon(menu);
#endif
    int x = 0, y = 0;
    IupGetIntInt(btn, "SCREENPOSITION", &x, &y);
    y += 38;
#ifdef _WIN32
    if (have_monitor) {
        HWND menu_window = (HWND)IupGetAttribute(menu, "WID");
        RECT rect = {0};
        if (menu_window && GetWindowRect(menu_window, &rect)) {
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            if (x + width > monitor.rcWork.right) x = monitor.rcWork.right - width;
            if (x < monitor.rcWork.left) x = monitor.rcWork.left;
            if (y + height > monitor.rcWork.bottom) y = monitor.rcWork.bottom - height;
            if (y < monitor.rcWork.top) y = monitor.rcWork.top;
        }
    }
#endif
    IupPopup(menu, x, y);
    IupDestroy(menu);
    return IUP_DEFAULT;
}

static Ihandle *tab_button(const char *title, bool selected, int width) {
    Ihandle *button = action_button(title, false);
    IupSetStrf(button, "RASTERSIZE", "%dx34", width);
    IupSetAttribute(button, "ALIGNMENT", "ACENTER");
    IupSetAttribute(button, "FONT", selected ? "Microsoft YaHei UI Bold 10" : "Microsoft YaHei UI 10");
    IupSetAttribute(button, "BGCOLOR", selected ? "226 241 253" : "255 255 255");
    IupSetAttribute(button, "FGCOLOR", selected ? "28 93 145" : "71 85 105");
    return button;
}

static int current_tab_draw_cb(Ihandle *canvas, float x, float y) {
    (void)x;
    (void)y;
    IupDrawBegin(canvas);
    int width = 0, height = 0, text_width = 0, text_height = 0;
    IupDrawGetSize(canvas, &width, &height);
    IupSetAttribute(canvas, "DRAWSTYLE", "FILL");
    IupSetAttribute(canvas, "DRAWCOLOR", "226 241 253");
    IupDrawRectangle(canvas, 0, 0, width - 1, height - 1);
    IupSetAttribute(canvas, "DRAWCOLOR", "52 152 219");
    IupDrawLine(canvas, 0, 0, width - 1, 0);
    IupDrawLine(canvas, 0, height - 1, width - 1, height - 1);
    IupDrawLine(canvas, 0, 0, 0, height - 1);
    IupDrawLine(canvas, width - 1, 0, width - 1, height - 1);
    const char *title = IupGetAttribute(canvas, "CURRENT_TITLE");
    if (title) {
        IupSetAttribute(canvas, "DRAWCOLOR", "28 93 145");
        int length = (int)strlen(title);
        IupDrawGetTextSize(canvas, title, length, &text_width, &text_height);
        IupDrawText(canvas, title, length, (width - text_width) / 2,
                    (height - text_height) / 2, text_width, text_height);
    }
    IupDrawEnd(canvas);
    return IUP_DEFAULT;
}

static Ihandle *current_tab_display(const char *title, int width) {
    Ihandle *canvas = IupCanvas(NULL);
    IupSetStrf(canvas, "RASTERSIZE", "%dx34", width);
    IupSetAttribute(canvas, "BORDER", "NO");
    IupSetAttribute(canvas, "SCROLLBAR", "NO");
    IupSetAttribute(canvas, "CANFOCUS", "NO");
    IupSetAttribute(canvas, "FONT", "Microsoft YaHei UI Bold 10");
    IupStoreAttribute(canvas, "CURRENT_TITLE", title);
    IupSetCallback(canvas, "ACTION", (Icallback)current_tab_draw_cb);
    IupSetCallback(canvas, "BUTTON_CB", (Icallback)background_click_cb);
    return canvas;
}

static void tab_caption(const char *name, int tab_width, char out[MAX_NAME_LEN]) {
    size_t in = 0, used = 0;
    size_t name_len = strlen(name);
    int available = tab_width - 16;
    if (measure_utf8_text_width(name, 10, true, (int)name_len * 8) <= available) {
        memcpy(out, name, name_len + 1);
        return;
    }
    while (in < name_len) {
        unsigned char lead = (unsigned char)name[in];
        size_t width = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        if (in + width > name_len || used + width + sizeof("…") > MAX_NAME_LEN) break;
        memcpy(out + used, name + in, width);
        out[used + width] = '\0';
        strcat(out, "…");
        if (measure_utf8_text_width(out, 10, true, (int)strlen(out) * 8) > available)
            break;
        used += width;
        in += width;
    }
    out[used] = '\0';
    strcat(out, "…");
}

/* ─────────────────── Tab 栏重建 ─────────────────── */

static void rebuild_tab_bar(void) {
    if (!gui || !gui->tab_bar) return;

    gui->rebuilding = true;

    /* 清空旧 Tab 栏 */
    Ihandle *child;
    while ((child = IupGetChild(gui->tab_bar, 0)) != NULL) {
        IupDetach(child);
        IupDestroy(child);
    }

    int n = g_app->set_count;
    int bar_width = TAB_BAR_MIN_WIDTH;
    if (n <= DEFAULT_TAB_MAX && n * TAB_NORMAL_WIDTH > bar_width)
        bar_width = n * TAB_NORMAL_WIDTH;
    IupSetStrf(gui->tab_bar, "RASTERSIZE", "%dx36", bar_width);

    if (n <= DEFAULT_TAB_MAX) {
        int side = (bar_width - TAB_NORMAL_WIDTH * n) / 2;
        if (side) IupAppend(gui->tab_bar, fixed_spacer(side));
        for (int i = 0; i < n; i++) {
            char caption[MAX_NAME_LEN];
            tab_caption(g_app->sets[i].name, TAB_NORMAL_WIDTH, caption);
            Ihandle *t = tab_button(caption, i == g_app->current_set, TAB_NORMAL_WIDTH);
            IupSetAttribute(t, "TIP", g_app->sets[i].name);
            IupSetInt(t, "SET_IDX", i);
            IupSetCallback(t, "FLAT_ACTION", (Icallback)tab_button_cb);
            IupAppend(gui->tab_bar, t);
        }
        int right = bar_width - side - TAB_NORMAL_WIDTH * n;
        if (right) IupAppend(gui->tab_bar, fixed_spacer(right));
    } else {
        char caption[MAX_NAME_LEN];
        tab_caption(g_app->sets[g_app->current_set].name, TAB_MENU_WIDTH, caption);
        Ihandle *current_t = current_tab_display(caption, TAB_MENU_WIDTH);
        IupSetAttribute(current_t, "TIP", g_app->sets[g_app->current_set].name);

        Ihandle *ham_btn = action_button("☰", false);
        IupSetStrf(ham_btn, "RASTERSIZE", "%dx34", TAB_MENU_BUTTON_WIDTH);
        IupSetAttribute(ham_btn, "TIP", "选择其他成绩组");
        IupSetCallback(ham_btn, "FLAT_ACTION", (Icallback)hamburger_btn_cb);

        int current_left = (bar_width - TAB_MENU_WIDTH) / 2;
        int menu_tab_gap = 10;
        IupAppend(gui->tab_bar, fixed_spacer(current_left - TAB_MENU_BUTTON_WIDTH - menu_tab_gap));
        IupAppend(gui->tab_bar, ham_btn);
        IupAppend(gui->tab_bar, fixed_spacer(menu_tab_gap));
        IupAppend(gui->tab_bar, current_t);
        IupAppend(gui->tab_bar, fixed_spacer(bar_width - current_left - TAB_MENU_WIDTH));
    }

    for (int i = 0; i < IupGetChildCount(gui->tab_bar); ++i)
        IupMap(IupGetChild(gui->tab_bar, i));
    gui->rebuilding = false;
    IupRefresh(gui->dlg);
    sync_drawn_page_visibility();
}

static void refresh_tab_selection(void) {
    if (!gui || !gui->tab_bar) return;
    sync_drawn_page_visibility();
    for (int i = 0; i < IupGetChildCount(gui->tab_bar); ++i) {
        Ihandle *child = IupGetChild(gui->tab_bar, i);
        if (g_app->set_count <= DEFAULT_TAB_MAX) {
            if (!IupGetAttribute(child, "SET_IDX")) continue;
            bool selected = IupGetInt(child, "SET_IDX") == g_app->current_set;
            IupSetAttribute(child, "FONT", selected ? "Microsoft YaHei UI Bold 10" : "Microsoft YaHei UI 10");
            IupSetAttribute(child, "BGCOLOR", selected ? "226 241 253" : "255 255 255");
            IupSetAttribute(child, "FGCOLOR", selected ? "28 93 145" : "71 85 105");
            IupUpdate(child);
        } else if (IupGetAttribute(child, "CURRENT_TITLE")) {
            char caption[MAX_NAME_LEN];
            tab_caption(g_app->sets[g_app->current_set].name, TAB_MENU_WIDTH, caption);
            IupStoreAttribute(child, "CURRENT_TITLE", caption);
            IupSetAttribute(child, "TIP", g_app->sets[g_app->current_set].name);
            IupUpdate(child);
        }
    }
}

/* ─────────────────── 工具栏回调 ─────────────────── */

static int add_set_cb(Ihandle *btn) {
    (void)btn;
    char *name = gui_input("新建成绩组", "请输入组名称：", "");
    if (!name) return IUP_DEFAULT;
    int idx = state_add_set(name[0] ? name : NULL);
    free(name);
    if (idx < 0) {
        gui_status("已达成绩组上限或内存不足");
        return IUP_DEFAULT;
    }

    /* 为新组创建一个 page */
    Ihandle *new_page = create_course_page();
    IupAppend(gui->tab_stack, new_page);
    IupMap(new_page);

    g_app->current_set = idx;
    IupSetInt(gui->tab_stack, "VALUEPOS", idx);
    rebuild_tab_bar();
    rebuild_page(idx);
    refresh_results();
    update_toolbar_state();
    mark_dirty();
    gui_status("已新建成绩组");
    return IUP_DEFAULT;
}

static int del_set_cb(Ihandle *btn) {
    (void)btn;
    if (g_app->set_count <= 1) {
        gui_status("至少保留一个成绩组");
        return IUP_DEFAULT;
    }
    int si = g_app->current_set;
    char msg[512];
    snprintf(msg, sizeof(msg), "确定要删除成绩组「%s」吗？", g_app->sets[si].name);
    if (!gui_confirm("删除成绩组", msg)) return IUP_DEFAULT;

    if (!state_remove_set(si)) {
        gui_status("删除失败：内存不足");
        return IUP_DEFAULT;
    }

    /* 状态更新成功后再移除对应 page */
    gui->rebuilding = true;
    Ihandle *page = get_stack_page(si);
    if (page) {
        IupDetach(page);
        IupDestroy(page);
    }
    gui->rebuilding = false;

    for (int i = si; i < g_app->set_count; ++i) IupSetInt(get_stack_page(i), "ROWS_READY", 0);
    IupSetInt(gui->tab_stack, "VALUEPOS", g_app->current_set);

    rebuild_tab_bar();
    rebuild_current_page();
    refresh_results();
    update_toolbar_state();
    mark_dirty();
    gui_status("已删除成绩组");
    return IUP_DEFAULT;
}

static int rename_set_cb(Ihandle *btn) {
    (void)btn;
    int si = g_app->current_set;
    char *name = gui_input("重命名", "请输入新的组名称：", g_app->sets[si].name);
    if (name && name[0]) {
        if (state_rename_set(si, name)) {
            rebuild_tab_bar();
            mark_dirty();
            gui_status("已重命名为「%s」", name);
        } else {
            gui_status("重命名失败：内存不足");
        }
    }
    free(name);
    return IUP_DEFAULT;
}

static void remember_course_focus(void) {
    gui->restore_field = 0;
    Ihandle *entry = IupGetFocus();
    if (!entry) return;
    int field = IupGetInt(entry, "COURSE_FIELD");
    if (field < 1 || field > 3) return;
    gui->restore_set = IupGetInt(entry, "SET_IDX");
    gui->restore_course = course_index_from_control(entry);
    gui->restore_field = field;
    gui->restore_start = gui->restore_end = 0;
#ifdef _WIN32
    HWND window = (HWND)IupGetAttribute(entry, "WID");
    if (window) {
        DWORD start = 0, end = 0;
        SendMessageW(window, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        gui->restore_start = (int)start;
        gui->restore_end = (int)end;
    }
#endif
}

static void restore_course_focus_after_rebuild(void) {
    if (gui->restore_field)
        IupPostMessage(gui->dlg, "RESTORE_COURSE_FOCUS", 0, 0.0, NULL);
}

static int undo_cb(Ihandle *btn) {
    (void)btn;
    remember_course_focus();
    if (!state_undo()) {
        gui->restore_field = 0;
        return IUP_DEFAULT;
    }

    rebuild_stack_pages();
    restore_course_focus_after_rebuild();
    mark_dirty();
    gui_status("已撤销");
    return IUP_DEFAULT;
}

static int redo_cb(Ihandle *btn) {
    (void)btn;
    remember_course_focus();
    if (!state_redo()) {
        gui->restore_field = 0;
        return IUP_DEFAULT;
    }

    rebuild_stack_pages();
    restore_course_focus_after_rebuild();
    mark_dirty();
    gui_status("已重做");
    return IUP_DEFAULT;
}

static int about_close_cb(Ihandle *button) {
    IupHide(IupGetDialog(button));
    return IUP_DEFAULT;
}

static int about_cb(Ihandle *button) {
    (void)button;
    Ihandle *heading = IupLabel(APP_NAME);
    IupSetAttribute(heading, "FONT", "Microsoft YaHei UI Bold 18");
    IupSetAttribute(heading, "FGCOLOR", "41 128 185");
    IupSetAttribute(heading, "EXPAND", "HORIZONTAL");
    IupSetAttribute(heading, "ALIGNMENT", "ACENTER");
    Ihandle *subtitle = IupLabel("北航成绩与绩点计算工具");
    IupSetAttribute(subtitle, "EXPAND", "HORIZONTAL");
    IupSetAttribute(subtitle, "ALIGNMENT", "ACENTER");

    Ihandle *github = IupLink(APP_URL, APP_URL);
    Ihandle *github_row = IupHbox(IupLabel("GitHub："), github, NULL);
    IupSetAttribute(github_row, "MARGIN", "0x0");
    IupSetAttribute(github_row, "GAP", "0");
    Ihandle *website = IupLink(WEB_URL, WEB_URL);
    Ihandle *website_row = IupHbox(IupLabel("网页版："), website, NULL);
    IupSetAttribute(website_row, "MARGIN", "0x0");
    IupSetAttribute(website_row, "GAP", "0");
    Ihandle *homepage = IupLink(HOMEPAGE_URL, HOMEPAGE_URL);
    Ihandle *homepage_row = IupHbox(IupLabel("个人网站："), homepage, NULL);
    IupSetAttribute(homepage_row, "MARGIN", "0x0");
    IupSetAttribute(homepage_row, "GAP", "0");
    Ihandle *email = IupLink("mailto:" APP_EMAIL, APP_EMAIL);
    Ihandle *email_row = IupHbox(IupLabel("邮箱："), email, NULL);
    IupSetAttribute(email_row, "MARGIN", "0x0");
    IupSetAttribute(email_row, "GAP", "0");
    Ihandle *close_button = action_button("关闭", true);
    IupSetAttribute(close_button, "BORDER", "NO");
    IupSetAttribute(close_button, "RASTERSIZE", "50x30");
    IupSetCallback(close_button, "FLAT_ACTION", (Icallback)about_close_cb);
    Ihandle *spacer = IupLabel(NULL);
    IupSetAttribute(spacer, "RASTERSIZE", "x20");
    Ihandle *close_row = IupHbox(horizontal_fill(), close_button, horizontal_fill(), NULL);
    IupSetAttribute(close_row, "EXPAND", "HORIZONTAL");
    IupSetAttribute(close_row, "MARGIN", "0x0");

    Ihandle *body = IupVbox(
        heading,
        subtitle,
        IupLabel("版本：" APP_VERSION),
        IupLabel("作者：WangNan"),
        email_row,
        website_row,
        github_row,
        homepage_row,
        IupLabel("版权：" APP_COPYRIGHT),
        IupLabel("许可证：AGPL-3.0"),
        IupLabel("IUP 等第三方组件遵循各自的许可证。"),
        IupLabel("成绩与绩点计算结果仅供参考。"),
        spacer,
        close_row, NULL
    );
    IupSetAttribute(body, "MARGIN", "24x20");
    IupSetAttribute(body, "GAP", "10");

    Ihandle *dialog = IupDialog(body);
    IupSetAttribute(dialog, "TITLE", "关于 " APP_NAME);
    IupSetAttribute(dialog, "DIALOGFRAME", "YES");
    IupSetAttributeHandle(dialog, "PARENTDIALOG", gui->dlg);
    IupSetAttribute(dialog, "RASTERSIZE", "430x");
    IupMap(dialog);
#ifdef _WIN32
    set_window_icon(dialog);
#endif
    IupPopup(dialog, IUP_CENTERPARENT, IUP_CENTERPARENT);
    IupDestroy(dialog);
    return IUP_DEFAULT;
}

static int save_cb(Ihandle *btn) {
    (void)btn;
    if (!g_app->config_path[0]) {
        if (!config_default_path(g_app->config_path, MAX_PATH_LEN)) {
            gui_status("保存失败：默认配置路径过长或不可用");
            return IUP_DEFAULT;
        }
    }
    if (config_save(g_app->config_path)) {
        gui_status("已保存到 %s", g_app->config_path);
    } else {
        gui_status("保存失败，请检查目录权限");
    }
    update_toolbar_state();
    return IUP_DEFAULT;
}

static int load_cb(Ihandle *btn) {
    (void)btn;
    Ihandle *fd = IupFileDlg();
    IupSetAttribute(fd, "TITLE", "加载配置文件");
    /* EXTFILTER 按“显示名称|匹配模式”成对解析，末尾 | 是结束符。 */
    IupSetAttribute(fd, "EXTFILTER", "INI 配置文件 (*.ini)|*.ini|所有文件 (*.*)|*.*|");
    IupSetAttribute(fd, "DIALOGTYPE", "OPEN");
    IupSetAttributeHandle(fd, "PARENTDIALOG", gui->dlg);
    IupPopup(fd, IUP_CENTERPARENT, IUP_CENTERPARENT);
    int status = IupGetInt(fd, "STATUS");
    if (status == 0) {
        const char *path = IupGetAttribute(fd, "VALUE");
        if (!path || strlen(path) >= MAX_PATH_LEN) {
            gui_status("加载失败：文件路径过长或不可用");
        } else {
            int choice = 2;
            if (g_app->dirty)
                choice = gui_choice("未保存的更改", "加载另一份 INI 前保存当前更改吗？",
                                    "保存", "不保存", "取消");
            if (choice == 1) {
                save_cb(NULL);
                if (g_app->dirty) choice = 3;
            }
            if (choice != 1 && choice != 2) {
                IupDestroy(fd);
                return IUP_DEFAULT;
            }
            if (config_load(path)) {
                IupSetAttribute(gui->autosave_timer, "RUN", "NO");
                strcpy(g_app->config_path, path);
                g_app->config_loaded = true;
                rebuild_stack_pages();
                refresh_settings_controls();
                gui_status("已加载 %s", path);
            } else gui_status("加载失败：文件格式错误或不存在");
        }
    }
    IupDestroy(fd);
    return IUP_DEFAULT;
}

/* ─────────────────── 工具栏状态更新 ─────────────────── */

static void update_toolbar_state(void) {
    if (!gui) return;
    IupSetAttribute(gui->undo_btn, "ACTIVE", state_can_undo() ? "YES" : "NO");
    IupSetAttribute(gui->redo_btn, "ACTIVE", state_can_redo() ? "YES" : "NO");
}

/* ─────────────────── 快捷键 ─────────────────── */

static int main_k_any(Ihandle *self, int c) {
    (void)self;
    if (c == K_cN) {
        add_set_cb(NULL);
        return IUP_IGNORE;
    }
    if (c == K_cO) {
        load_cb(NULL);
        return IUP_IGNORE;
    }
    /* Ctrl+Y 重做 */
    if ((c == K_cY || c == iup_XkeyShift(K_cZ)) && state_can_redo()) {
        redo_cb(NULL);
        return IUP_IGNORE;
    }
    /* Ctrl+Z 撤销 */
    if (c == K_cZ && state_can_undo()) {
        undo_cb(NULL);
        return IUP_IGNORE;
    }
    /* Ctrl+S 保存 */
    if (c == K_cS) {
        save_cb(NULL);
        return IUP_IGNORE;
    }
    if (c == K_F2) {
        rename_set_cb(NULL);
        return IUP_IGNORE;
    }
    if (c == K_mF1) {
        about_cb(NULL);
        return IUP_IGNORE;
    }
    return IUP_DEFAULT;
}

/* 关闭窗口 */
static int main_close_cb(Ihandle *self) {
    (void)self;
    if (g_app && g_app->dirty) {
        int choice = gui_choice("未保存的更改", "退出前保存更改吗？",
                                "保存", "不保存", "取消");
        if (choice == 3) return IUP_IGNORE;
        if (choice == 1) {
            save_cb(NULL);
            if (g_app->dirty) return IUP_IGNORE;
        }
    }
    return IUP_DEFAULT;
}

/* ─────────────────── 主窗口构建 ─────────────────── */

int gui_run(int argc, char **argv) {
    IupSetGlobal("UTF8MODE", "YES");
    if (IupOpen(&argc, &argv) != IUP_NOERROR) return 1;

    /* 设置默认字体 (确保中文显示) */
    IupSetGlobal("DEFAULTFONT", "Microsoft YaHei UI 10");

    gui = calloc(1, sizeof(GuiState));
    if (!gui) {
        IupClose();
        return 1;
    }

    /* 自动保存定时器 */
    gui->autosave_timer = IupTimer();
    IupSetInt(gui->autosave_timer, "TIME", AUTOSAVE_DELAY);
    IupSetCallback(gui->autosave_timer, "ACTION_CB", (Icallback)autosave_timer_cb);
    gui->results_timer = IupTimer();
    IupSetInt(gui->results_timer, "TIME", 80);
    IupSetCallback(gui->results_timer, "ACTION_CB", (Icallback)results_timer_cb);
    gui->status_timer = IupTimer();
    IupSetCallback(gui->status_timer, "ACTION_CB", (Icallback)status_timer_cb);
    min_course_name_width = measure_course_name_width();

    /* ── 顶部品牌信息 ── */
    Ihandle *brand = copyable_text(APP_NAME, "Microsoft YaHei UI Bold 18", 320);
    IupSetAttribute(brand, "FONT", "Microsoft YaHei UI Bold 18");
    IupSetAttribute(brand, "FGCOLOR", "41 128 185");
    Ihandle *subtitle = copyable_text("北航成绩与绩点计算  ·  v" APP_VERSION, "Microsoft YaHei UI Bold 8", 320);
    IupSetAttribute(subtitle, "FGCOLOR", "100 116 139");
    Ihandle *brand_box = IupVbox(brand, subtitle, NULL);
    IupSetAttribute(brand_box, "GAP", "2");
    IupSetAttribute(brand_box, "EXPAND", "NO");
    IupSetAttribute(brand_box, "MARGIN", "0x0");
    Ihandle *top_bar = IupHbox(horizontal_fill(), brand_box, horizontal_fill(), NULL);
    IupSetAttribute(top_bar, "EXPAND", "HORIZONTAL");
    IupSetAttribute(top_bar, "ALIGNMENT", "ACENTER");
    IupSetAttribute(top_bar, "MARGIN", "0x0");
    IupSetAttribute(top_bar, "RASTERSIZE", "x58");
    IupSetAttribute(top_bar, "MAXSIZE", "99999x58");

    /* ── 工具栏 ── */
    gui->undo_btn = action_button("撤销 (Ctrl+Z)", false);
    gui->redo_btn = action_button("重做 (Ctrl+Y)", false);
    Ihandle *add_set_btn = action_button("新建组 (Ctrl+N)", true);
    Ihandle *del_set_btn = action_button("删除组", false);
    Ihandle *rename_btn   = action_button("重命名 (F2)", false);
    Ihandle *save_btn_loc = action_button("保存 (Ctrl+S)", false);
    Ihandle *load_btn_loc = action_button("加载 (Ctrl+O)", false);
    Ihandle *about_btn = action_button("关于 (Alt+F1)", false);
    Ihandle *toolbar_buttons[] = {
        save_btn_loc, load_btn_loc, add_set_btn, del_set_btn,
        rename_btn, gui->undo_btn, gui->redo_btn, about_btn
    };
    for (size_t i = 0; i < sizeof toolbar_buttons / sizeof toolbar_buttons[0]; ++i) {
        IupSetAttribute(toolbar_buttons[i], "FONT", "Microsoft YaHei UI 9");
        IupSetAttribute(toolbar_buttons[i], "RASTERSIZE", "x25");
        IupSetAttribute(toolbar_buttons[i], "PADDING", "5x2");
    }
    style_danger_button(del_set_btn);
    IupSetAttribute(gui->undo_btn, "TIP", "撤销 (Ctrl+Z)");
    IupSetAttribute(gui->redo_btn, "TIP", "重做 (Ctrl+Y 或 Ctrl+Shift+Z)");
    IupSetAttribute(save_btn_loc, "TIP", "保存到当前配置文件 (Ctrl+S)");
    IupSetAttribute(load_btn_loc, "TIP", "加载另一份 INI 配置 (Ctrl+O)");
    IupSetAttribute(del_set_btn, "TIP", "删除当前成绩组");
    IupSetAttribute(add_set_btn, "TIP", "新建成绩组 (Ctrl+N)");
    IupSetAttribute(rename_btn, "TIP", "重命名当前成绩组 (F2)");
    IupSetAttribute(about_btn, "TIP", "软件信息 (Alt+F1)");

    IupSetCallback(gui->undo_btn, "FLAT_ACTION", (Icallback)undo_cb);
    IupSetCallback(gui->redo_btn, "FLAT_ACTION", (Icallback)redo_cb);
    IupSetCallback(add_set_btn, "FLAT_ACTION", (Icallback)add_set_cb);
    IupSetCallback(del_set_btn, "FLAT_ACTION", (Icallback)del_set_cb);
    IupSetCallback(rename_btn, "FLAT_ACTION", (Icallback)rename_set_cb);
    IupSetCallback(save_btn_loc, "FLAT_ACTION", (Icallback)save_cb);
    IupSetCallback(load_btn_loc, "FLAT_ACTION", (Icallback)load_cb);
    IupSetCallback(about_btn, "FLAT_ACTION", (Icallback)about_cb);

    Ihandle *toolbar = IupHbox(
        save_btn_loc, load_btn_loc,
        toolbar_spacer(),
        add_set_btn, del_set_btn, rename_btn,
        toolbar_spacer(),
        gui->undo_btn, gui->redo_btn,
        toolbar_spacer(),
        about_btn,
        NULL
    );
    IupSetAttribute(toolbar, "GAP", "8");
    IupSetAttribute(toolbar, "EXPAND", "NO");
    IupSetAttribute(toolbar, "MARGIN", "0x0");
    Ihandle *toolbar_center = IupHbox(horizontal_fill(), toolbar, horizontal_fill(), NULL);
    IupSetAttribute(toolbar_center, "EXPAND", "HORIZONTAL");
    IupSetAttribute(toolbar_center, "MARGIN", "0x0");
    IupSetAttribute(toolbar_center, "RASTERSIZE", "x40");
    IupSetAttribute(toolbar_center, "MAXSIZE", "99999x40");

    /* ── Tab 栏 ── */
    gui->tab_bar = IupHbox(NULL);
    IupSetAttribute(gui->tab_bar, "EXPAND", "NO");
    IupSetAttribute(gui->tab_bar, "MARGIN", "0x0");
    IupSetAttribute(gui->tab_bar, "GAP", "0");
    IupSetStrf(gui->tab_bar, "RASTERSIZE", "%dx36", TAB_BAR_MIN_WIDTH);
    Ihandle *tabs_center = IupHbox(horizontal_fill(), gui->tab_bar, horizontal_fill(), NULL);
    IupSetAttribute(tabs_center, "EXPAND", "HORIZONTAL");
    IupSetAttribute(tabs_center, "MARGIN", "0x0");
    IupSetAttribute(tabs_center, "GAP", "0");
    IupSetAttribute(tabs_center, "RASTERSIZE", "x36");
    IupSetAttribute(tabs_center, "MAXSIZE", "99999x36");

    /* ── Tab 内容区 (Stack) ── */
    gui->tab_stack = IupZbox(NULL);
    IupSetAttribute(gui->tab_stack, "EXPAND", "YES");
    for (int i = 0; i < g_app->set_count; i++) {
        Ihandle *page = create_course_page();
        IupAppend(gui->tab_stack, page);
    }

    /* ── 结果区 ── */
    const char *labels[] = {"算术平均分", "加权平均分", "GPA", "总学分", "有效课程"};
    const wchar_t *result_samples[] = {
        L"100.000000", L"100.000000", L"4.000000", L"9999.00", L"9999"
    };
    const int result_fallback_widths[] = {130, 130, 130, 80, 80};
    int result_widths[5];
    int result_columns_width = 0;
    for (int i = 0; i < 5; ++i) {
        result_widths[i] = measure_text_width(result_samples[i], 14, true, result_fallback_widths[i]) + 16;
        if (result_widths[i] < result_fallback_widths[i])
            result_widths[i] = result_fallback_widths[i];
        result_columns_width += result_widths[i];
    }
    Ihandle *result_hbox = IupHbox(NULL);
    for (int i = 0; i < 5; i++) {
        Ihandle *caption = IupLabel(labels[i]);
        IupSetAttribute(caption, "FGCOLOR", "100 116 139");
        IupSetAttribute(caption, "ALIGNMENT", "ACENTER");
        IupSetCallback(caption, "BUTTON_CB", (Icallback)background_click_cb);
        Ihandle *col = IupVbox(caption, NULL);
        gui->result_labels[i] = copyable_text("—", "Microsoft YaHei UI Bold 14", result_widths[i]);
        IupSetAttribute(gui->result_labels[i], "FGCOLOR", "41 128 185");
        IupAppend(col, gui->result_labels[i]);
        IupSetAttribute(col, "ALIGNMENT", "ACENTER");
        IupSetAttribute(col, "GAP", "3");
        IupSetAttribute(col, "MARGIN", "0x0");
        IupAppend(result_hbox, col);
        if (i < 4) {
            Ihandle *divider = IupCanvas(NULL);
            IupSetAttribute(divider, "EXPAND", "NO");
            IupSetAttribute(divider, "RASTERSIZE", "2x45");
            IupSetAttribute(divider, "BORDER", "NO");
            IupSetAttribute(divider, "SCROLLBAR", "NO");
            IupSetAttribute(divider, "CANFOCUS", "NO");
            IupSetCallback(divider, "ACTION", (Icallback)result_divider_draw_cb);
            IupSetCallback(divider, "BUTTON_CB", (Icallback)background_click_cb);
            IupAppend(result_hbox, divider);
        }
    }
    IupSetAttribute(result_hbox, "GAP", "4");
    IupSetAttribute(result_hbox, "MARGIN", "0x0");
    IupSetAttribute(result_hbox, "ALIGNMENT", "ATOP");
    IupSetAttribute(result_hbox, "EXPAND", "NO");

    Ihandle *result_title = copyable_text("计算结果", "Microsoft YaHei UI Bold 12", 120);
    Ihandle *result_title_row = IupHbox(horizontal_fill(), result_title, horizontal_fill(), NULL);
    IupSetAttribute(result_title_row, "EXPAND", "HORIZONTAL");
    Ihandle *result_values_row = IupHbox(horizontal_fill(), result_hbox, horizontal_fill(), NULL);
    IupSetAttribute(result_values_row, "EXPAND", "HORIZONTAL");
    IupSetAttribute(result_values_row, "ALIGNMENT", "ATOP");
    Ihandle *result_body = IupVbox(result_title_row, result_values_row, NULL);
    IupSetAttribute(result_body, "EXPAND", "HORIZONTAL");
    IupSetAttribute(result_body, "MARGIN", "4x4");
    IupSetAttribute(result_body, "GAP", "2");
    Ihandle *result_frame = IupFrame(result_body);
    int result_frame_width = result_columns_width + 4 * 2 + 8 * 4 + 2 * 4 + 2 * 10 + 10;
    IupSetStrf(result_frame, "RASTERSIZE", "%dx101", result_frame_width);
    IupSetAttribute(result_frame, "EXPAND", "NO");

    gui->autosave_switch = IupCanvas(NULL);
    IupSetAttribute(gui->autosave_switch, "RASTERSIZE", "40x20");
    IupSetAttribute(gui->autosave_switch, "BORDER", "NO");
    IupSetAttribute(gui->autosave_switch, "CANFOCUS", "NO");
    IupSetAttribute(gui->autosave_switch, "SCROLLBAR", "NO");
    IupSetAttribute(gui->autosave_switch, "BGCOLOR", "245 247 250");
    IupSetAttribute(gui->autosave_switch, "TIP", "单击切换自动保存");
    IupSetCallback(gui->autosave_switch, "ACTION", (Icallback)autosave_draw_cb);
    IupSetCallback(gui->autosave_switch, "BUTTON_CB", (Icallback)autosave_switch_cb);
    gui->precision_list = IupList(NULL);
    IupSetAttribute(gui->precision_list, "DROPDOWN", "YES");
    IupSetAttribute(gui->precision_list, "RASTERSIZE", "40x");
    for (int i = 0; i <= 7; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "%d", i + 1);
        IupSetInt(gui->precision_list, key, i);
    }
    IupSetInt(gui->precision_list, "VALUE", g_app->precision + 1);
    IupSetCallback(gui->precision_list, "ACTION", (Icallback)precision_changed_cb);
    IupSetCallback(gui->precision_list, "DROPDOWN_CB", (Icallback)precision_dropdown_cb);
    Ihandle *autosave_caption = IupLabel("自动保存");
    Ihandle *precision_caption = IupLabel("小数位数");
    IupSetCallback(autosave_caption, "BUTTON_CB", (Icallback)background_click_cb);
    IupSetCallback(precision_caption, "BUTTON_CB", (Icallback)background_click_cb);
    IupSetAttribute(autosave_caption, "FONT", "Microsoft YaHei UI 8");
    IupSetAttribute(precision_caption, "FONT", "Microsoft YaHei UI 8");
    IupSetAttribute(gui->precision_list, "FONT", "Microsoft YaHei UI 8");
    IupSetAttribute(autosave_caption, "RASTERSIZE", "50x");
    IupSetAttribute(precision_caption, "RASTERSIZE", "50x");
    Ihandle *autosave_row = IupHbox(autosave_caption, gui->autosave_switch, NULL);
    Ihandle *precision_row = IupHbox(precision_caption, gui->precision_list, NULL);
    IupSetAttribute(autosave_row, "GAP", "4");
    IupSetAttribute(precision_row, "GAP", "4");
    IupSetAttribute(autosave_row, "ALIGNMENT", "ACENTER");
    IupSetAttribute(precision_row, "ALIGNMENT", "ACENTER");
    IupSetInt(autosave_row, "CX", 0);
    IupSetInt(autosave_row, "CY", 23);
    IupSetInt(precision_row, "CX", 0);
    IupSetInt(precision_row, "CY", 53);
    Ihandle *settings = IupCbox(autosave_row, precision_row, NULL);
    const int settings_width = 94;
    IupSetStrf(settings, "RASTERSIZE", "%dx100", settings_width);
    IupSetAttribute(settings, "EXPAND", "NO");
    Ihandle *right_balance = IupLabel(NULL);
    IupSetStrf(right_balance, "RASTERSIZE", "%dx0", settings_width);
    Ihandle *results_center = IupHbox(settings, horizontal_fill(), result_frame, horizontal_fill(), right_balance, NULL);
    IupSetAttribute(results_center, "EXPAND", "HORIZONTAL");
    IupSetAttribute(results_center, "ALIGNMENT", "ACENTER");
    IupSetAttribute(results_center, "GAP", "0");
    IupSetAttribute(results_center, "MARGIN", "0x0");
    IupSetAttribute(results_center, "RASTERSIZE", "x120");
    IupSetAttribute(results_center, "MAXSIZE", "99999x120");

    /* ── 状态栏 ── */
    gui->status_label = copyable_text("就绪", NULL, 0);
    IupSetAttribute(gui->status_label, "EXPAND", "HORIZONTAL");
    IupSetAttribute(gui->status_label, "ALIGNMENT", "ALEFT");
    IupSetAttribute(gui->status_label, "FGCOLOR", "100 116 139");

    gui->focus_sink = IupCanvas(NULL);
    IupSetAttribute(gui->focus_sink, "RASTERSIZE", "1x1");
    IupSetAttribute(gui->focus_sink, "FLOATING", "YES");
    IupSetAttribute(gui->focus_sink, "CANFOCUS", "YES");
    IupSetAttribute(gui->focus_sink, "BORDER", "NO");
    IupSetAttribute(gui->focus_sink, "SCROLLBAR", "NO");
    IupSetAttribute(gui->focus_sink, "BGCOLOR", "245 247 250");

    /* ── 主布局 ── */
    Ihandle *vbox = IupVbox(
        top_bar,
        toolbar_center,
        separator(),
        tabs_center,
        gui->tab_stack,
        separator(),
        results_center,
        gui->status_label,
        gui->focus_sink,
        NULL
    );
    IupSetAttribute(vbox, "MARGIN", "18x16");
    IupSetAttribute(vbox, "GAP", "8");
    IupSetAttribute(vbox, "EXPAND", "YES");

    /* ── 主窗口 ── */
    gui->dlg = IupDialog(vbox);
    IupSetAttribute(gui->dlg, "TITLE", APP_NAME " v" APP_VERSION);
    int window_min_width = min_course_name_width + 620;
    int native_chrome_width = 16;
#ifdef _WIN32
    native_chrome_width = 2 * GetSystemMetrics(SM_CXSIZEFRAME);
#endif
    int results_min_width = result_frame_width + settings_width * 2 + 10 * 2 + 18 * 2 + native_chrome_width;
    if (window_min_width < results_min_width) window_min_width = results_min_width;
    IupSetStrf(gui->dlg, "RASTERSIZE", "%dx730", window_min_width);
    IupSetAttribute(gui->dlg, "POSITION", "CENTER");
    IupSetStrf(gui->dlg, "MINSIZE", "%dx730", window_min_width);
    IupSetAttribute(gui->dlg, "BGCOLOR", "245 247 250");
    IupSetCallback(gui->dlg, "K_ANY", (Icallback)main_k_any);
    IupSetCallback(gui->dlg, "CLOSE_CB", (Icallback)main_close_cb);
    IupSetCallback(gui->dlg, "POSTMESSAGE_CB", (Icallback)main_postmessage_cb);
    IupSetCallback(gui->dlg, "RESIZE_CB", (Icallback)main_resize_cb);
    IupSetAttributeHandle(gui->dlg, "STARTFOCUS", gui->focus_sink);

    /* 初始化 */
    rebuild_tab_bar();
    rebuild_current_page();
    refresh_results();
    refresh_settings_controls();
    update_toolbar_state();

    /* 尝试自动加载同目录配置 */
    if (!config_default_path(g_app->config_path, MAX_PATH_LEN)) {
        gui_status("默认配置路径过长或不可用");
    } else if (config_load(g_app->config_path)) {
        g_app->config_loaded = true;
        rebuild_stack_pages();
        refresh_settings_controls();
        gui_status("已自动加载配置文件");
    } else {
        if (config_file_exists(g_app->config_path)) {
            gui_status("现有配置文件无效，请检查 config.ini");
        } else if (config_save(g_app->config_path)) {
            gui_status("已创建空白配置文件");
        } else {
            gui_status("无法在程序目录创建配置文件");
        }
    }

    IupShowXY(gui->dlg, IUP_CENTER, IUP_CENTER);
    sync_drawn_page_visibility();
    for (int i = 0; i < g_app->set_count; ++i) {
        if (IupGetInt(get_stack_page(i), "DRAWN_GRID")) drawn_grid_fill_layer(IupGetChild(get_stack_page(i), 2));
        sync_course_header_gutter(get_stack_page(i));
    }
#ifdef _WIN32
    set_window_icon(gui->dlg);
    HWND main_window = (HWND)IupGetAttribute(gui->dlg, "WID");
    if (main_window) SetWindowSubclass(main_window, background_focus_subclass, 1, 0);
    HWND result_window = (HWND)IupGetAttribute(result_frame, "WID");
    if (result_window) SetWindowSubclass(result_window, background_focus_subclass, 2, 0);
#endif
    int ret = IupMainLoop();

    /* 清理 */
    IupDestroy(gui->autosave_timer);
    IupDestroy(gui->results_timer);
    IupDestroy(gui->status_timer);
    free(gui);
    gui = NULL;
    IupClose();
    return ret;
}
