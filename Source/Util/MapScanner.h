// Copyright CircumScriptor and DarkNeutrino 2021
#ifndef MAPSCANNER_H
#define MAPSCANNER_H

#include <Server/Structs/MapStruct.h>
#include <stdint.h>

/**
 * @brief Scans a directory for map files (.vxl) and creates a linked list
 *
 * This function scans the specified directory for .vxl files and validates
 * that each has a corresponding .toml configuration file. Maps without
 * matching configs (or vice versa) are logged as warnings but don't prevent
 * server boot.
 *
 * @param directory Path to the directory to scan (e.g., "Resources/maps/")
 * @param map_count Output parameter - number of valid maps found
 * @param alphabetic If true, sort alphabetically; if false, keep directory order
 * @return Pointer to the head of a doubly-linked list of map names, or NULL if no valid maps
 */
string_node_t* scan_maps_directory(const char* directory, uint8_t* map_count, int alphabetic);

/**
 * @brief Frees a string_node_t linked list
 *
 * @param list Head of the list to free
 */
void free_string_list(string_node_t* list);

#endif // MAPSCANNER_H
