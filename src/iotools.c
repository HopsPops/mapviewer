#include <stdlib.h>
#include <dirent.h>
#include "gtk-3.0/gtk/gtk.h"

gboolean createDirectory(gchar *name) {
	gchar cmd[1000] = {
			0 };
	sprintf(cmd, "mkdir %s", name);
	return system(cmd) != -1;
}

gboolean removeDirectory(gchar *name) {
	gchar cmd[1000] = {
			0 };
	sprintf(cmd, "rmdir /S /Q %s", name);
	return system(cmd) != -1;
}

gboolean fileExists(gchar *path) {
	FILE * f = fopen(path, "r");
	if (f == NULL) {
		return FALSE;
	}
	fclose(f);
	return TRUE;
}

gboolean directoryExists(gchar *path) {
	DIR *dir = opendir(path);
	if (dir == NULL) {
		return FALSE;
	}
	closedir(dir);
	return TRUE;
}
