#include "wad.h"

#define WIN32_LEAN_AND_MEAN
#undef UNICODE
#include <windows.h>

#include <stdint.h>

int
wad_open(struct wad *wad, const char *path)
{
	DWORD rd;

	wad->fd = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (wad->fd == INVALID_HANDLE_VALUE) return WAD_ERROR_FILE_OPEN;

	if (!ReadFile(wad->fd, &wad->hd, WAD_HEADER_SIZE, &rd, 0)) return WAD_ERROR_FILE_READ;
	if (rd != WAD_HEADER_SIZE) return WAD_ERROR_FILE_READ;

	return WAD_SUCCESS;
}

void
wad_close(struct wad *wad)
{
	CloseHandle(wad->fd);
}

int
wad_seek_first_dentry(const struct wad *wad)
{
	if (SetFilePointer(wad->fd, wad->hd.directory_offset, 0, FILE_BEGIN) != wad->hd.directory_offset) {
		return WAD_ERROR_FILE_SEEK;
	}
	return WAD_SUCCESS;
}

int
wad_read_next_dentry(const struct wad *wad, struct wad_dentry *dentry)
{
	DWORD rd;

	if (!ReadFile(wad->fd, dentry, WAD_DENTRY_SIZE, &rd, 0)) return WAD_ERROR_FILE_READ;
	if (rd != WAD_DENTRY_SIZE) return WAD_ERROR_FILE_READ;
	dentry->name[8] = '\0';

	return WAD_SUCCESS;
}
