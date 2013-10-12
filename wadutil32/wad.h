#ifndef WAD_HEADER
#define WAD_HEADER

#define LE_FOURCC(a, b, c, d) ( \
		((unsigned)(a)) | \
		((unsigned)(b) << 8) | \
		((unsigned)(c) << 16) | \
		((unsigned)(d) << 24) \
	)

#define WAD_HEADER_SIZE (4 + 4 + 4)
#define WAD_DENTRY_SIZE (4 + 4 + 8)

enum wad_error {
	WAD_SUCCESS = 0,
	WAD_ERROR_FILE_OPEN = -1,
	WAD_ERROR_FILE_READ = -2,
	WAD_ERROR_FILE_SEEK = -3
};

enum wad_type {
	WAD_TYPE_IWAD = LE_FOURCC('I', 'W', 'A' , 'D'),
	WAD_TYPE_PWAD = LE_FOURCC('P', 'W', 'A' , 'D')
};

struct wad_header {
	enum wad_type type;
	int lump_count;
	int directory_offset;
};

struct wad {
	void *fd;
	struct wad_header hd;
};

struct wad_dentry {
	int offset;
	int size;
	char name[8 + 1];
};

int wad_open(struct wad *wad, const char *path);
void wad_close(struct wad *wad);

int wad_seek_first_dentry(const struct wad *wad);
int wad_read_next_dentry(const struct wad *wad, struct wad_dentry *dentry);


#endif // WAD_HEADER
