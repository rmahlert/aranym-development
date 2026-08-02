#include "sysdeps.h"

#ifdef OS_cygwin

#include "tools.h"
#include <sys/cygwin.h>

char *cygwin_path_to_win32(char *path, size_t size)
{
	char winpath[1024];
	/* FIXME: this can fail if result exceeds MAX_PATH */
	cygwin_conv_path(CCP_POSIX_TO_WIN_A | CCP_ABSOLUTE, path, winpath, sizeof(winpath));
	safe_strncpy(path, winpath, size);
	return path;
}

char *cygwin_path_to_posix(char *path, size_t size)
{
	char posixpath[1024];
	cygwin_conv_path(CCP_WIN_A_TO_POSIX | CCP_ABSOLUTE, path, posixpath, sizeof(posixpath));
	safe_strncpy(path, posixpath, size);
	return path;
}

#endif /* OS_cygwin */
