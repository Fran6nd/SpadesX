// Copyright CircumScriptor and DarkNeutrino 2021
#include "MapScanner.h"

#include <Util/Alloc.h>
#include <Util/Log.h>
#include <Util/Utlist.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Helper structure for sorting
typedef struct map_entry {
    char* name;
    struct map_entry *next, *prev;
} map_entry_t;

static int
file_exists(const char* directory, const char* filename, const char* extension)
{
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s%s", directory, filename, extension);

    struct stat st;
    return (stat(filepath, &st) == 0 && S_ISREG(st.st_mode));
}

static int
compare_map_names(map_entry_t* a, map_entry_t* b)
{
    return strcmp(a->name, b->name);
}

string_node_t*
scan_maps_directory(const char* directory, uint8_t* map_count, int alphabetic)
{
    DIR* dir = opendir(directory);
    if (!dir) {
        LOG_ERROR("Failed to open maps directory: %s", directory);
        *map_count = 0;
        return NULL;
    }

    map_entry_t*   temp_list = NULL;
    struct dirent* entry;
    uint8_t        count = 0;

    LOG_INFO("Scanning for maps in: %s", directory);

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. and hidden files
        if (entry->d_name[0] == '.') {
            continue;
        }

        // Check if file ends with .vxl
        size_t name_len = strlen(entry->d_name);
        if (name_len < 5 || strcmp(entry->d_name + name_len - 4, ".vxl") != 0) {
            continue;
        }

        // Extract map name (without .vxl extension)
        char map_name[256];
        strncpy(map_name, entry->d_name, name_len - 4);
        map_name[name_len - 4] = '\0';

        // Check if corresponding .toml exists
        if (!file_exists(directory, map_name, ".toml")) {
            LOG_WARNING("Map '%s' has .vxl file but missing .toml config - skipping", map_name);
            continue;
        }

        // Valid map found - add to temporary list
        map_entry_t* new_entry = spadesx_malloc(sizeof(map_entry_t));
        new_entry->name        = spadesx_malloc(strlen(map_name) + 1);
        strcpy(new_entry->name, map_name);

        DL_APPEND(temp_list, new_entry);
        count++;

        LOG_INFO("Found valid map: %s", map_name);
    }

    closedir(dir);

    // Check for orphaned .toml files (config without map)
    dir = opendir(directory);
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') {
                continue;
            }

            size_t name_len = strlen(entry->d_name);
            if (name_len < 6 || strcmp(entry->d_name + name_len - 5, ".toml") != 0) {
                continue;
            }

            // Extract map name (without .toml extension)
            char map_name[256];
            strncpy(map_name, entry->d_name, name_len - 5);
            map_name[name_len - 5] = '\0';

            if (!file_exists(directory, map_name, ".vxl")) {
                LOG_WARNING("Map '%s' has .toml config but missing .vxl file - ignored", map_name);
            }
        }
        closedir(dir);
    }

    if (count == 0) {
        LOG_ERROR("No valid maps found in %s", directory);
        *map_count = 0;
        return NULL;
    }

    // Sort alphabetically if requested
    if (alphabetic) {
        DL_SORT(temp_list, compare_map_names);
        LOG_INFO("Maps sorted alphabetically");
    }

    // Convert temporary list to string_node_t list
    string_node_t* result = NULL;
    map_entry_t*   entry_iter;

    DL_FOREACH(temp_list, entry_iter)
    {
        string_node_t* node = spadesx_malloc(sizeof(string_node_t));
        node->string        = entry_iter->name; // Transfer ownership
        DL_APPEND(result, node);
    }

    // Free temporary list (but not the strings, we transferred them)
    map_entry_t *temp_iter, *temp_tmp;
    DL_FOREACH_SAFE(temp_list, temp_iter, temp_tmp)
    {
        DL_DELETE(temp_list, temp_iter);
        free(temp_iter); // Don't free temp_iter->name, it's been transferred
    }

    *map_count = count;
    LOG_STATUS("Loaded %d map%s", count, count == 1 ? "" : "s");

    return result;
}

void
free_string_list(string_node_t* list)
{
    string_node_t *iter, *tmp;
    DL_FOREACH_SAFE(list, iter, tmp)
    {
        DL_DELETE(list, iter);
        free(iter->string);
        free(iter);
    }
}
