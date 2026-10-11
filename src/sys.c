#include "sys.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

int sys_mkdir_p(const char* path) {
	char tmp[1024];
	size_t n = strlen(path);
	if (n == 0 || n >= sizeof(tmp)) return 0;
	memcpy(tmp, path, n + 1);
	for (size_t i = 1; i <= n; i++) {
		if (tmp[i] == '/' || tmp[i] == '\\' || tmp[i] == 0) {
			char c = tmp[i];
			tmp[i] = 0;
#if defined(_WIN32)
			_mkdir(tmp);
#else
			mkdir(tmp, 0755);
#endif
			tmp[i] = c;
		}
	}
	return sys_file_exists(path);
}

int sys_file_exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

int64_t sys_mtime(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	return (int64_t) st.st_mtime;
}

int sys_remove(const char* path) { return remove(path) == 0; }

void sys_list_dir(const char* path, void (*cb)(const char*, void*), void* ctx) {
#if defined(_WIN32)
	char pat[1100];
	snprintf(pat, sizeof(pat), "%s\\*", path);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) cb(fd.cFileName, ctx);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
#else
	DIR* d = opendir(path);
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' && (e->d_name[1] == 0 || (e->d_name[1] == '.' && e->d_name[2] == 0))) continue;
		cb(e->d_name, ctx);
	}
	closedir(d);
#endif
}

int sys_strcasecmp(const char* a, const char* b) {
	while (*a && *b) {
		int ca = tolower((unsigned char) *a), cb = tolower((unsigned char) *b);
		if (ca != cb) return ca - cb;
		a++; b++;
	}
	return tolower((unsigned char) *a) - tolower((unsigned char) *b);
}

void sys_stem(const char* f, char* out, size_t n) {
	const char* s = strrchr(f, '/');
	const char* s2 = strrchr(f, '\\');
	if (s2 && (!s || s2 > s)) s = s2;
	s = s ? s + 1 : f;
	snprintf(out, n, "%s", s);
	char* dot = strrchr(out, '.');
	if (dot) *dot = 0;
}

int sys_format_time(int64_t t, char* out, size_t n) {
	time_t tt = (time_t) t;
	struct tm* tm = localtime(&tt);
	if (!tm) { out[0] = 0; return 0; }
	return (int) strftime(out, n, "%d/%m/%Y  %H:%M", tm);
}
