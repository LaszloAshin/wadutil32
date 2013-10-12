#include "wad.h"

#define WIN32_LEAN_AND_MEAN
#undef UNICODE
#include <windows.h>
#include <commctrl.h> // toolbar
#include <commdlg.h> // file open dialog
#include <stdio.h> // snprintf

#define TITLE "WAD util"

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))

enum defaults {
	WINDOW_WIDTH = 640,
	WINDOW_HEIGHT = 480
};

enum command {
	CMD_NEW = 4096,
	CMD_OPEN,
	CMD_SAVE,
	CMD_LISTBOX,
	CMD_DELETE,
	CMD_CLEAR,
	CMD_MOVE_UP,
	CMD_MOVE_DOWN,
	CMD_COPY,
	CMD_EDIT,
	CMD_RENAME,
	CMD_ABOUT
};

static HINSTANCE inst;
static HWND hToolbar, hStatus, hList, hEdit;

struct item {
	struct wad_dentry dentry;
	char *source;
};

static int item_count = 0;
static int item_capacity = 0;
static struct item *items = 0;
static char wad_path[MAX_PATH];
static int list_bottom;

static int
resize_items(int new_capacity)
{
	int size = sizeof(struct item) * new_capacity;

	if (items) {
		struct item *p = (struct item *)HeapReAlloc(GetProcessHeap(), 0, items, size);
		if (!p) {
			MessageBox(0, "realloc", 0, MB_ICONERROR | MB_OK);
			return -1;
		}
		items = p;
	} else {
		items = (struct item *)HeapAlloc(GetProcessHeap(), 0, size);
		if (!items) {
			MessageBox(0, "alloc", 0, MB_ICONERROR | MB_OK);
			return -2;
		}
	}

	item_capacity = new_capacity;
	return 0;
}

static void
free_items(void)
{
	int i;

	for (i = 0; i < item_count; ++i) {
		if (items[i].source) {
			HeapFree(GetProcessHeap(), 0, items[i].source);
			items[i].source = 0;
		}
	}
	item_count = 0;
	SendMessage(hList, LB_SETCOUNT, item_count, 0);
}

static void
createMenus(HWND hWnd)
{
	HMENU hMenubar = CreateMenu();

	{
		HMENU hMenu = CreateMenu();

		AppendMenu(hMenu, MF_STRING, CMD_OPEN, "&Open\tCtrl+O");
		AppendMenu(hMenu, MF_STRING, CMD_SAVE, "&Save As\tCtrl+S");
		AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
		AppendMenu(hMenu, MF_STRING, CMD_CLEAR, "&Clear");
		AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
		AppendMenu(hMenu, MF_STRING, IDCANCEL, "&Quit\tEscape");

		AppendMenu(hMenubar, MF_POPUP, (UINT_PTR)hMenu, "&File");
	}

	{
		HMENU hMenu = CreateMenu();

		AppendMenu(hMenu, MF_STRING, CMD_NEW, "&New\tInsert");
		AppendMenu(hMenu, MF_STRING, CMD_DELETE, "&Delete\tDelete");
		AppendMenu(hMenu, MF_STRING, CMD_COPY, "&Copy to file");
		AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
		AppendMenu(hMenu, MF_STRING, CMD_MOVE_UP, "Move &Up\tCtrl+Up");
		AppendMenu(hMenu, MF_STRING, CMD_MOVE_DOWN, "Move D&own\tCtrl+Down");

		AppendMenu(hMenubar, MF_POPUP, (UINT_PTR)hMenu, "&Lump");
	}

	AppendMenu(hMenubar, MF_STRING | MF_HELP, CMD_ABOUT, "&About");
	SetMenu(hWnd, hMenubar);
}

static void
createToolbar(HWND hWnd)
{
	static struct {
		enum command cmd;
		int bitmap;
	} button_commands[] = {
		{ CMD_OPEN, STD_FILEOPEN },
		{ CMD_SAVE, STD_FILESAVE },
		{ 0, 0 },
		{ CMD_NEW, STD_FILENEW },
		{ CMD_DELETE, STD_DELETE },
		{ CMD_COPY, STD_COPY },
		{ CMD_MOVE_UP, STD_UNDO },
		{ CMD_MOVE_DOWN, STD_REDOW },
	};

	TBBUTTON buttons[ARRAY_LENGTH(button_commands)];
	TBADDBITMAP tbab;
	int i;

	hToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, 0, WS_CHILD /*| WS_TABSTOP*/ | WS_VISIBLE, 0, 0, 0, 0, hWnd, 0, inst, 0);

	SendMessage(hToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	tbab.hInst = HINST_COMMCTRL;
	tbab.nID = IDB_STD_SMALL_COLOR;
	SendMessage(hToolbar, TB_ADDBITMAP, 0, (LPARAM)&tbab);

	ZeroMemory(buttons, sizeof(buttons));
	for (i = 0; i < ARRAY_LENGTH(buttons); ++i) {
		buttons[i].iBitmap = button_commands[i].bitmap;
		buttons[i].fsState = TBSTATE_ENABLED;
		buttons[i].fsStyle = button_commands[i].cmd ? TBSTYLE_BUTTON : TBSTYLE_SEP;
		buttons[i].idCommand = button_commands[i].cmd;
	}

	SendMessage(hToolbar, TB_ADDBUTTONS, ARRAY_LENGTH(buttons), (LPARAM)&buttons);
}

static void
createStatusBar(HWND hWnd)
{
	int widths[] = { 100, -1 };
	hStatus = CreateWindowEx(0, STATUSCLASSNAME, 0, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hWnd, 0, inst, 0);

    SendMessage(hStatus, SB_SETPARTS, ARRAY_LENGTH(widths), (LPARAM)widths);
}

static WNDPROC orig_list_wndproc;

static LRESULT CALLBACK
new_list_wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_ERASEBKGND:
		return TRUE;
	default:
		return CallWindowProc(orig_list_wndproc, hWnd, msg, wParam, lParam);
	}
}

static void
createClient(HWND hWnd)
{
//	int tabstops[] = { 12 * 4, 24 * 4 };

	hList = CreateWindow(
		"listbox", 0,
		WS_CHILD | WS_TABSTOP | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL | LBS_NODATA | LBS_OWNERDRAWFIXED,
		0, 0, 0, 0, hWnd, (HMENU)CMD_LISTBOX, inst, 0
	);
	orig_list_wndproc = (WNDPROC)SetWindowLong(hList, GWL_WNDPROC, (long)new_list_wndproc);
	hEdit = CreateWindow("edit", 0, WS_CHILD | WS_TABSTOP | WS_VISIBLE | ES_UPPERCASE, 0, 0, 0, 0, hWnd, (HMENU)CMD_EDIT, inst, 0);
	SendMessage(hEdit, EM_LIMITTEXT, 8, 0);
//	hRename = CreateWindow("button", "rename", WS_CHILD | WS_TABSTOP | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)CMD_RENAME, inst, 0);
//	SendMessage(hList, LB_SETTABSTOPS, ARRAY_LENGTH(tabstops), (LPARAM)tabstops);
//	hDetails = CreateWindow("static", 0, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, 0, inst, 0);
}

static void
sureQuit(HWND hWnd)
{
	if (MessageBox(hWnd, "Are you sure to quit?", "Message", MB_OKCANCEL) == IDOK) {
		SendMessage(hWnd, WM_CLOSE, 0, 0);
	}
}

static void
openWad(HWND hWnd)
{
	OPENFILENAME ofn;
	char filename[MAX_PATH] = "";

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.hInstance = inst;
	ofn.lpstrFilter = "WAD Files\0*.wad\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = sizeof(filename);
	ofn.Flags = OFN_FILEMUSTEXIST;
	ofn.lpstrDefExt = "wad";

	if (GetOpenFileName(&ofn)) {
		struct wad w;
		int ret;
		int i;

		ret = wad_open(&w, filename);
		if (ret != WAD_SUCCESS) {
			MessageBox(hWnd, filename, 0, MB_ICONERROR | MB_OK);
			return;
		}
		free_items();
		resize_items(w.hd.lump_count);
		wad_seek_first_dentry(&w);
		for (i = w.hd.lump_count; i; --i) {
			char buf[128];

			wad_read_next_dentry(&w, &items[item_count].dentry);
			items[item_count].source = 0;
			sprintf_s(
				buf, sizeof(buf),
				"%s\t%d\t%d",
				items[item_count].dentry.name,
				items[item_count].dentry.offset,
				items[item_count].dentry.size
			);
			++item_count;
		}
		SendMessage(hList, LB_SETCOUNT, item_count, 0);
	    SendMessage(hStatus, SB_SETTEXT, 1, (LPARAM)filename);
		lstrcpy(wad_path, filename);
		wad_close(&w);
	}
}

static void
list_select()
{
	unsigned sel = (unsigned)SendMessage(hList, LB_GETCURSEL, 0, 0);
	if (sel < (unsigned)item_count) {
		char buf[32];
		struct wad_dentry *d = &items[sel].dentry;
		SetWindowText(hEdit, d->name);
		sprintf_s(buf, sizeof(buf), "%d/%d", sel + 1, item_count);
	    SendMessage(hStatus, SB_SETTEXT, 0, (LPARAM)buf);
	}
}

static void
list_delete()
{
	int count = (int)SendMessage(hList, LB_GETSELCOUNT, 0, 0);

	if ((count == LB_ERR) || (count < 0)) return;

	while (count > 0) {
		int sels[8];
		int j;
		int ret = (int)SendMessage(hList, LB_GETSELITEMS, ARRAY_LENGTH(sels), (LPARAM)sels);

		if (ret == LB_ERR) break;
		for (j = ret - 1; j >= 0; --j) {
			int sel = sels[j];
			int i;

			if (items[sel].source) {
				HeapFree(GetProcessHeap(), 0, items[sel].source);
			}
//			SetWindowText(hDetails, "");
			for (i = sel; i < item_count - 1; ++i) {
				items[i] = items[i + 1];
			}
			--item_count;
			--count;
		}
	}

	{
		int top = SendMessage(hList, LB_GETTOPINDEX, 0, 0);
		SendMessage(hList, LB_SETCOUNT, item_count, 0);
		if (top >= item_count) top = item_count - 1;
		SendMessage(hList, LB_SETTOPINDEX, top, 0);
	}
}

static void
gen_short_name(char short_name[8 + 1], const char *long_name)
{
	int i = 0, j = 0;

	while (i < 8) {
		char ch = long_name[j];
		if (ch) {
			++j;
			if ((ch >= 'a') && (ch <= 'z')) {
				ch -= 'a' - 'A';
			} else if ((ch >= 'A') && (ch <= 'Z')) {
			} else if ((ch >= '0') && (ch <= '9')) {
			} else if ((ch == '[') || (ch == ']')) {
			} else if ((ch == '-') || (ch == '_')) {
			} else continue;
		}
		short_name[i++] = ch;
	}
	short_name[i] = '\0';
}

static void
list_add(HWND hWnd)
{
	OPENFILENAME ofn;
	char path[MAX_PATH] = "";
	char filename[MAX_PATH] = "";

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.hInstance = inst;
	ofn.lpstrFilter = "All Files\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = sizeof(path);
	ofn.Flags = OFN_FILEMUSTEXIST;
	ofn.lpstrFileTitle = filename;
	ofn.nMaxFileTitle = sizeof(filename);

	if (GetOpenFileName(&ofn)) {
		HANDLE fd = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

		if (fd == INVALID_HANDLE_VALUE) {
			MessageBox(hWnd, "Failed to open file", 0, MB_ICONERROR | MB_OK);
			return;
		}
		if (item_capacity == item_count) {
			resize_items(item_capacity ? (item_capacity * 2) : 8);
		}
		// TODO: insert it before selection
		items[item_count].dentry.size = GetFileSize(fd, 0);
		CloseHandle(fd);
		items[item_count].dentry.offset = 0;
		gen_short_name(items[item_count].dentry.name, filename);
		items[item_count].source = (char *)HeapAlloc(GetProcessHeap(), 0, lstrlen(path));
		lstrcpy(items[item_count].source, path);
		++item_count;
		SendMessage(hList, LB_SETCOUNT, item_count, 0);
	}
}

static int
copy_between_fds(HANDLE dest, HANDLE src, size_t length)
{
	char buf[BUFSIZ];
	DWORD rd, wr;
	int count = 0;

	while (length) {
		size_t chunk_size = sizeof(buf);
		if (chunk_size > length) chunk_size = length;
		if (!ReadFile(src, buf, chunk_size, &rd, 0)) break;
		if (!WriteFile(dest, buf, rd, &wr, 0)) break;
		count += wr;
		length -= wr;
		if (wr != rd) break;
	}
	return count;
}

static int
copy_from_file(HANDLE dest, const char *filename)
{
	HANDLE src = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	int ret = -1;

	if (src) {
		size_t size = GetFileSize(src, 0);
		ret = copy_between_fds(dest, src, size);
		if ((ret > 0) && (ret != size)) ret = -1;
		CloseHandle(src);
	}
	return ret;
}

static int
save_wad_to(const char *path)
{
	HANDLE fd = CreateFile(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	HANDLE wfd = 0;
	DWORD wr;
	struct wad_header hd;
	int ret;
	int i;

	if (!fd) return -1;

	if (wad_path[0]) {
		wfd = CreateFile(wad_path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	}

	ret = -1;

	hd.type = WAD_TYPE_IWAD;
	hd.lump_count = item_count;
	hd.directory_offset = 0; // we don't know yet
	if (!WriteFile(fd, &hd, WAD_HEADER_SIZE, &wr, 0)) goto cleanup;
	if (wr != WAD_HEADER_SIZE) goto cleanup;

	for (i = 0; i < item_count; ++i) {
		struct item *it = items + i;
		int final_offset = SetFilePointer(fd, 0, 0, FILE_CURRENT);

		if (it->source) {
			ret = copy_from_file(fd, it->source);
		} else {
			if (SetFilePointer(wfd, it->dentry.offset, 0, FILE_BEGIN) != it->dentry.offset) {
				ret = -1;
				goto cleanup;
			}
			ret = copy_between_fds(fd, wfd, it->dentry.size);
			if ((ret > 0) && (ret != it->dentry.size)) ret = -1;
		}
		if (ret < 0) goto cleanup;
		it->dentry.offset = it->dentry.size ? final_offset : 0;
	}
	hd.directory_offset = SetFilePointer(fd, 0, 0, FILE_CURRENT);
	for (i = 0; i < item_count; ++i) {
		struct item *it = items + i;
		if (!WriteFile(fd, &it->dentry, WAD_DENTRY_SIZE, &wr, 0)) goto cleanup;
		if (wr != WAD_DENTRY_SIZE) goto cleanup;
	}
	SetFilePointer(fd, 0, 0, FILE_BEGIN);
	if (!WriteFile(fd, &hd, WAD_HEADER_SIZE, &wr, 0)) goto cleanup;
	if (wr != WAD_HEADER_SIZE) goto cleanup;

	ret = 0;
cleanup:
	if (wfd) CloseHandle(wfd);
	CloseHandle(fd);
	return ret;
}

static void
save_wad(HWND hWnd)
{
	OPENFILENAME ofn;
	char path[MAX_PATH] = "";

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.hInstance = inst;
	ofn.lpstrFilter = "WAD Files\0*.wad\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = sizeof(path);
	ofn.Flags = 0;

	if (GetSaveFileName(&ofn)) {
		DWORD attr = GetFileAttributes(path);
		if (attr != 0xffffffff) {
			if (!lstrcmp(path, wad_path)) {
				MessageBox(hWnd, "Cannot move a file to itself.", 0, MB_ICONERROR | MB_OK);
				return;
			}
			if (MessageBox(hWnd, "Overwrite?", "File exists", MB_YESNO | MB_ICONQUESTION) != IDYES) {
				return;
			}
		}
		if (save_wad_to(path)) {
			MessageBox(hWnd, "Failed to save wad file.", 0, MB_ICONERROR | MB_OK);
		} else {
			MessageBox(hWnd, "File saved.", "Report", MB_ICONINFORMATION | MB_OK);
		}
	}
}

static void
validate_edit(void)
{
	char buf[8 + 1];
	char buf2[8 + 1];

	GetWindowText(hEdit, buf, sizeof(buf));
	gen_short_name(buf2, buf);
	if (lstrcmp(buf2, buf)) {
		SetWindowText(hEdit, buf2);
	}
}

static void
rename_selected(void)
{
	unsigned sel = (unsigned)SendMessage(hList, LB_GETCURSEL, 0, 0);
	if (sel < (unsigned)item_count) {
		struct wad_dentry *d = &items[sel].dentry;
		GetWindowText(hEdit, d->name, sizeof(d->name));
//		RedrawWindow(hList, 0, 0, RDW_INVALIDATE);
		SetFocus(hList);
	}
}

static void
move(int k)
{
	if (k) {
		int i;

		for (i = item_count - 1; i >= 0; --i) {
			if (SendMessage(hList, LB_GETSEL, i, 0) > 0) {
				struct item tmp = items[i];
				if (i + 1 >= item_count) break;
				items[i] = items[i + 1];
				items[i + 1] = tmp;
				SendMessage(hList, LB_SETSEL, FALSE, i);
				SendMessage(hList, LB_SETSEL, TRUE, i + 1);
			}
		}
	} else {
		int i;

		for (i = 0; i < item_count; ++i) {
			if (SendMessage(hList, LB_GETSEL, i, 0) > 0) {
				struct item tmp = items[i];
				if (!i) break;
				items[i] = items[i - 1];
				items[i - 1] = tmp;
				SendMessage(hList, LB_SETSEL, FALSE, i);
				SendMessage(hList, LB_SETSEL, TRUE, i - 1);
			}
		}
	}
/*	unsigned sel = (unsigned)SendMessage(hList, LB_GETCURSEL, 0, 0);
	unsigned other = sel + k;
	if ((sel < (unsigned)item_count) && (other < (unsigned)item_count)) {
		struct item tmp = items[sel];
		items[sel] = items[other];
		items[other] = tmp;

		SendMessage(hList, LB_SETSEL, FALSE, sel);
		SendMessage(hList, LB_SETSEL, TRUE, other);
		RedrawWindow(hList, 0, 0, RDW_INVALIDATE);
	}*/
}

static int
save_lump_to(int lump, const char *path)
{
	HANDLE fd = CreateFile(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	HANDLE wfd = 0;
	int ret = -1;
	struct item *item = items + lump;

	if (!fd) return -1;

	if (!item->source) {
		wfd = CreateFile(wad_path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

		if (SetFilePointer(wfd, item->dentry.offset, 0, FILE_BEGIN) != item->dentry.offset) {
			ret = -1;
			goto cleanup;
		}
		ret = copy_between_fds(fd, wfd, item->dentry.size);
		if ((ret > 0) && (ret != item->dentry.size)) ret = -1;
	} else {
		ret = copy_from_file(fd, item->source);
	}

	ret = 0;
cleanup:
	if (wfd) CloseHandle(wfd);
	CloseHandle(fd);
	return ret;
}

static void
save_lump(HWND hWnd)
{
	unsigned sel = (unsigned)SendMessage(hList, LB_GETCURSEL, 0, 0);
	if (sel < (unsigned)item_count) {
		OPENFILENAME ofn;
		char path[MAX_PATH] = "";

		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = hWnd;
		ofn.hInstance = inst;
		ofn.lpstrFilter = "All Files\0*.*\0";
		ofn.lpstrFile = path;
		ofn.nMaxFile = sizeof(path);
		ofn.Flags = 0;

		if (GetSaveFileName(&ofn)) {
			DWORD attr = GetFileAttributes(path);
			if (attr != 0xffffffff) {
				if (MessageBox(hWnd, "Overwrite?", "File exists", MB_YESNO | MB_ICONQUESTION) != IDYES) {
					return;
				}
			}
			if (save_lump_to(sel, path)) {
				MessageBox(hWnd, "Failed to save lump.", 0, MB_ICONERROR | MB_OK);
			} else {
				MessageBox(hWnd, "File saved.", "Report", MB_ICONINFORMATION | MB_OK);
			}
		}
	}
}

static void
processCommand(HWND hWnd, enum command cmd, WORD param)
{
	switch (cmd) {
	case CMD_OPEN:
		openWad(hWnd);
		break;
	case CMD_LISTBOX:
		if (param == LBN_SELCHANGE) {
			list_select();
		}
		break;
	case CMD_DELETE:
		list_delete();
		break;
	case CMD_NEW:
		list_add(hWnd);
		break;
	case CMD_CLEAR:
//		SetWindowText(hDetails, "");
		free_items();
		resize_items(8);
		wad_path[0] = '\0';
		SendMessage(hStatus, SB_SETTEXT, 1, (LPARAM)"");
		break;
	case CMD_SAVE:
		save_wad(hWnd);
		break;
	case CMD_EDIT:
		if (param == EN_CHANGE) validate_edit();
		break;
	case IDOK:
		if (GetFocus() == hEdit) {
			rename_selected();
		} else if (GetFocus() == hList) {
			SetFocus(hEdit);
			SendMessage(hEdit, EM_SETSEL, 8, 8);
		}
		break;
	case CMD_MOVE_UP:
		move(0);
		break;
	case CMD_MOVE_DOWN:
		move(!0);
		break;
	case CMD_COPY:
//		MessageBox(hWnd, "Not implemented yet :(", 0, MB_ICONEXCLAMATION | MB_OK);
		save_lump(hWnd);
		break;
	case IDCANCEL:
		sureQuit(hWnd);
		break;
	case CMD_ABOUT:
		MessageBox(hWnd,
			"WAD manipulation program created by Laszlo on " __DATE__ " at " __TIME__ ".\n"
			"I hope you have as much fun using as I've had making it.\0jfdiofjdso",
			"About", MB_ICONINFORMATION | MB_OK
		);
		break;
	}
}

static void
centerWindow(HWND hWnd)
{
	RECT rc;

	GetWindowRect(hWnd, &rc);
	SetWindowPos(
		hWnd, 0,
		(GetSystemMetrics(SM_CXSCREEN) - rc.right) / 2,
		(GetSystemMetrics(SM_CYSCREEN) - rc.bottom) / 2,
		0, 0, SWP_NOZORDER | SWP_NOSIZE
	);
}

static void
resizeMainWindow(HWND hWnd)
{
	RECT toolbar_rect, statusbar_rect, client_rect;
	RECT edit_rect;
	int toolbar_height, statusbar_height, client_height;

	SendMessage(hToolbar, TB_AUTOSIZE, 0, 0);

	GetWindowRect(hToolbar, &toolbar_rect);
	toolbar_height = toolbar_rect.bottom - toolbar_rect.top;

    SendMessage(hStatus, WM_SIZE, 0, 0);

	GetWindowRect(hStatus, &statusbar_rect);
	statusbar_height = statusbar_rect.bottom - statusbar_rect.top;

	GetClientRect(hWnd, &client_rect);
	client_height = client_rect.bottom - toolbar_height - statusbar_height;

	edit_rect.top = 18; // http://msdn.microsoft.com/en-us/library/aa511279.aspx#sizingspacing
	MapDialogRect(hWnd, &edit_rect);
	list_bottom = client_rect.bottom - statusbar_height - edit_rect.top;

	SetWindowPos(hList, 0, client_rect.left, toolbar_height, client_rect.right - client_rect.left, client_height - edit_rect.top, SWP_NOZORDER);
	SetWindowPos(hEdit, 0, client_rect.left, list_bottom, client_rect.right - client_rect.left, edit_rect.top, SWP_NOZORDER);
}

static int
measure_item(HWND hWnd, WPARAM id, LPARAM lp)
{
	MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lp;

	if (id == CMD_LISTBOX) {
		SIZE size;
		HDC hdc = GetDC(hWnd);
		GetTextExtentPoint32(hdc, TITLE, sizeof(TITLE) - 1, &size);
		mis->itemWidth = size.cx;
		mis->itemHeight = size.cy;
		ReleaseDC(hWnd, hdc);
		return !0;
	}

	return 0;
}

static int
draw_item(HWND hWnd, WPARAM id, LPARAM lp)
{
	COLORREF color_selected_text = GetSysColor(COLOR_HIGHLIGHTTEXT);
	COLORREF color_selected_background = GetSysColor(COLOR_HIGHLIGHT);
	DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
	unsigned i = dis->itemID;

	if ((id == CMD_LISTBOX) && (i < (unsigned)item_count)) {
		static int tabstops[] = { 100, 200 };
		COLORREF color_old_text;
		COLORREF color_old_background;
		int selected = dis->itemState & ODS_SELECTED;
		DWORD extent;
		RECT rc;
		HBRUSH brush;
		char buf[128];

		if (items[i].source) {
			sprintf_s(
				buf, sizeof(buf),
				"%s\t%s",
				items[i].dentry.name,
				items[i].source
			);
		} else {
			sprintf_s(
				buf, sizeof(buf),
				"%s\t%d\t%d",
				items[i].dentry.name,
				items[i].dentry.offset,
				items[i].dentry.size
			);
		}

		if (selected) {
			color_old_text = SetTextColor(dis->hDC, color_selected_text);
			color_old_background = SetBkColor(dis->hDC, color_selected_background);
		}

//		ExtTextOut(dis->hDC, dis->rcItem.left, dis->rcItem.top, ETO_OPAQUE, &dis->rcItem, buf, lstrlen(buf), 0);
		extent = GetTabbedTextExtent(dis->hDC, buf, lstrlen(buf), ARRAY_LENGTH(tabstops), tabstops);
		rc = dis->rcItem;
		rc.left = LOWORD(extent);
		brush = CreateSolidBrush(GetBkColor(dis->hDC));
		FillRect(dis->hDC, &rc, brush);
		DeleteObject(brush);

		TabbedTextOut(dis->hDC, dis->rcItem.left, dis->rcItem.top, buf, lstrlen(buf), ARRAY_LENGTH(tabstops), tabstops, 0);

		if (selected) {
			SetTextColor(dis->hDC, color_old_text);
			SetBkColor(dis->hDC, color_old_background);
		}

		if (i == item_count - 1) {
			brush = CreateSolidBrush(GetBkColor(dis->hDC));
			rc = dis->rcItem;
			rc.top = rc.bottom;
			rc.bottom = list_bottom;
			FillRect(dis->hDC, &rc, brush);
			DeleteObject(brush);
		}

		return !0;
	}

	return 0;
}

static LRESULT CALLBACK
winProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_CREATE:
		createMenus(hWnd);
		createToolbar(hWnd);
		createStatusBar(hWnd);
		createClient(hWnd);
		centerWindow(hWnd);
		break;
	case WM_COMMAND:
		processCommand(hWnd, (enum command)(LOWORD(wParam)), HIWORD(wParam));
		return TRUE;
	case WM_SIZE:
		resizeMainWindow(hWnd);
		return TRUE;
	case WM_MEASUREITEM:
		if (measure_item(hWnd, wParam, lParam)) {
			return TRUE;
		}
		break;
	case WM_DRAWITEM:
		if (draw_item(hWnd, wParam, lParam)) {
			return TRUE;
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void __cdecl
WinMainCRTStartup()
{
	inst = GetModuleHandle(0);
	InitCommonControls();

	{
		WNDCLASS wc = { 0 };

		wc.lpszClassName = TITLE;
		wc.hInstance = inst;
		wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
		wc.lpfnWndProc = winProc;
		wc.hCursor = LoadCursor(0, IDC_ARROW);

		RegisterClass(&wc);
	}

	{
		ACCEL acc_table[] = {
			{ FCONTROL | FVIRTKEY, 0x4f, CMD_OPEN },
			{ FCONTROL | FVIRTKEY, 0x53, CMD_SAVE },
			{ FVIRTKEY, VK_INSERT, CMD_NEW },
			{ FVIRTKEY, VK_DELETE, CMD_DELETE },
			{ FCONTROL | FVIRTKEY, VK_UP, CMD_MOVE_UP },
			{ FCONTROL | FVIRTKEY, VK_DOWN, CMD_MOVE_DOWN }
		};
		HWND hWnd = CreateWindow(TITLE, TITLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, inst, 0);
		HACCEL accel = CreateAcceleratorTable(acc_table, ARRAY_LENGTH(acc_table));
		MSG msg;

		ShowWindow(hWnd, SW_SHOWDEFAULT);
		UpdateWindow(hWnd);

		while (GetMessage(&msg, 0, 0, 0)) {
			if (TranslateAccelerator(hWnd, accel, &msg)) continue;
			if (IsDialogMessage(hWnd, &msg)) continue;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

//		DestroyWindow(hWnd);
		ExitProcess(msg.wParam);
	}
}
