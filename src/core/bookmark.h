#include <stdbool.h>
#include <json-c/json_types.h>

/* Create bookmark file */
bool create_bookmark_file();

/* Check if the bookmark file exists or not */
bool is_bookmark_file_exists();

/* Opens a file handle and then stringify data for writing in bookmarks file. */
void add_new_bookmark(json_object *data);