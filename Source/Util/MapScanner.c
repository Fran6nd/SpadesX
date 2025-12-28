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
is_directory(const char* path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int
file_exists(const char* path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
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

    // Scan for map folders
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. and hidden files
        if (entry->d_name[0] == '.') {
            continue;
        }

        // Build full path to potential map folder
        char folder_path[512];
        snprintf(folder_path, sizeof(folder_path), "%s/%s", directory, entry->d_name);

        // Must be a directory
        if (!is_directory(folder_path)) {
            continue;
        }

        // Folder name is the map name
        const char* map_name = entry->d_name;

        // Check if folder contains <map_name>.vxl and <map_name>.toml
        char vxl_path[1024];
        char toml_path[1024];
        snprintf(vxl_path, sizeof(vxl_path), "%s/%s.vxl", folder_path, map_name);
        snprintf(toml_path, sizeof(toml_path), "%s/%s.toml", folder_path, map_name);

        int has_vxl  = file_exists(vxl_path);
        int has_toml = file_exists(toml_path);

        if (!has_vxl && !has_toml) {
            LOG_WARNING("Map folder '%s' does not contain %s.vxl or %s.toml - skipping", map_name, map_name, map_name);
            continue;
        }

        if (!has_vxl) {
            LOG_WARNING("Map folder '%s' missing %s.vxl - skipping", map_name, map_name);
            continue;
        }

        if (!has_toml) {
            LOG_WARNING("Map folder '%s' missing %s.toml config - skipping", map_name, map_name);
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

    if (count == 0) {
        LOG_ERROR("No valid maps found in %s", directory);
        LOG_ERROR("Each map must be in a folder matching the map name (e.g., MyMap/MyMap.vxl and MyMap/MyMap.toml)");
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
