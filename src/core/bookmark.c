#include <stdlib.h>
#include <stdio.h>
#include <json-c/json.h>
#include <unistd.h>
#include <stdbool.h>

const char *BOOKMARK_PATH = "/etc/flintzilla/bookmarks.txt";

bool create_bookmark_file()
{
    const FILE *bookmark_file;
    bookmark_file = fopen(BOOKMARK_PATH, "w+");
    fclose(bookmark_file);
    return true;
}

bool is_bookmark_file_exists()
{
    char *bookmarks_file = BOOKMARK_PATH;
    if (access(bookmarks_file, F_OK) == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void add_new_bookmark(json_object *data)
{
    const FILE *bookmarks_file;
    bookmarks_file = fopen(BOOKMARK_PATH, "a");

    char *stringify_data;
    stringify_data = json_object_to_json_string(data);

    fputs(stringify_data, bookmarks_file);

    fclose(bookmarks_file);
}