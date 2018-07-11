#include <stdlib.h>
#include <string.h>
#include "gtk-3.0/gtk/gtk.h"
#include "stringtools.h"

// https://stackoverflow.com/a/779960
// You must free the result if result is non-NULL.
gchar *str_replace(gchar *orig, gchar *rep, gchar *with) {
	gchar *result; // the return string
	gchar *ins;    // the next insert point
	gchar *tmp;    // varies
	gint32 len_rep;  // length of rep (the string to remove)
	gint32 len_with; // length of with (the string to replace rep with)
	gint32 len_front; // distance between rep and end of last rep
	gint32 count;    // number of replacements

	// sanity checks and initialization
	if (!orig || !rep) return NULL;
	len_rep = strlen(rep);
	if (len_rep == 0) return NULL; // empty rep causes infinite loop during count
	if (!with) with = "";
	len_with = strlen(with);

	// count the number of replacements needed
	ins = orig;
	for (count = 0; (tmp = strstr(ins, rep)); ++count) {
		ins = tmp + len_rep;
	}

	tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

	if (!result) return NULL;

	// first time through the loop, all the variable are set correctly
	// from here on,
	//    tmp points to the end of the result string
	//    ins points to the next occurrence of rep in orig
	//    orig points to the remainder of orig after "end of rep"
	while (count--) {
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}
