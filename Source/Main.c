// Copyright DarkNeutrino 2021
#include <Server/Server.h>
#include <Server/Structs/ServerStruct.h>
#include <Server/Structs/StartStruct.h>
#include <Util/JSONHelpers.h>
#include <Util/TOMLHelpers.h>
#include <Util/Log.h>
#include <Util/Types.h>
#include <Util/Utlist.h>
#include <Util/Alloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tomlc99/toml.h>

int main(void)
{
    uint16_t    port;
    uint8_t     master;
    uint8_t     gamemode;
    uint8_t     capture_limit;
    const char* manager_passwd;
    const char* admin_passwd;
    const char* mod_passwd;
    const char* guard_passwd;
    const char* trusted_passwd;
    const char* server_name;
    const char* team1_name;
    const char* team2_name;
    color_t     team1_color;
    color_t     team2_color;
    uint8_t     periodic_delays[5];

    string_node_t* map_list              = NULL;
    string_node_t* welcome_message_list  = NULL;
    string_node_t* periodic_message_list = NULL;
    const char*         rotation_mode_str     = NULL;
    map_rotation_mode_t rotation_mode         = MAP_ROTATION_TOML_DEFINED;

    size_t map_list_len              = 0;
    size_t welcome_message_list_len;
    size_t periodic_message_list_len;

    toml_table_t* parsed;
    toml_table_t* server_table;
    TOMLH_READ_FROM_FILE(parsed, "config.toml");
    TOMLH_GET_TABLE(parsed, server_table, "server");

    /* [server] */
    TOMLH_GET_STRING(server_table, server_name, "name", "SpadesX server", 0);
    TOMLH_GET_INT(server_table, port, "port", DEFAULT_SERVER_PORT, 0);
    TOMLH_GET_BOOL(server_table, master, "master", 1, 0);
    TOMLH_GET_INT(server_table, gamemode, "gamemode", 0, 0);
    TOMLH_GET_INT(server_table, capture_limit, "capture_limit", 10, 0);

    // Parse maps: each entry is an inline table { name = "...", scripts = [...] }
    {
        toml_array_t* maps_array = toml_array_in(server_table, "maps");
        if (!maps_array) {
            LOG_ERROR("Cannot find 'maps' array in config.toml [server]");
            exit(EXIT_FAILURE);
        }
        map_list_len = toml_array_nelem(maps_array);
        for (size_t i = 0; i < map_list_len; i++) {
            toml_table_t* entry = toml_table_at(maps_array, i);
            if (!entry) {
                LOG_ERROR("maps[%zu] is not a table — expected { name = \"...\", scripts = [...] }", i);
                exit(EXIT_FAILURE);
            }
            toml_datum_t name_datum = toml_string_in(entry, "name");
            if (!name_datum.ok) {
                LOG_ERROR("maps[%zu] missing required 'name' field", i);
                exit(EXIT_FAILURE);
            }
            string_node_t* node  = spadesx_malloc(sizeof(string_node_t));
            node->string         = name_datum.u.s;
            node->scripts        = NULL;
            node->scripts_count  = 0;

            toml_array_t* scripts_arr = toml_array_in(entry, "scripts");
            if (scripts_arr) {
                node->scripts_count = toml_array_nelem(scripts_arr);
                node->scripts       = spadesx_malloc(node->scripts_count * sizeof(char*));
                for (size_t j = 0; j < node->scripts_count; j++) {
                    toml_datum_t s = toml_string_at(scripts_arr, j);
                    if (!s.ok) {
                        LOG_ERROR("maps[%zu].scripts[%zu]: failed to read script name", i, j);
                        exit(EXIT_FAILURE);
                    }
                    node->scripts[j] = s.u.s;
                }
            }
            DL_APPEND(map_list, node);
        }
    }

    if (map_list_len == 0) {
        LOG_ERROR("No maps defined in config.toml [server] maps array");
        LOG_ERROR("Please add at least one map to the 'maps' array in your config");
        exit(EXIT_FAILURE);
    }

    // Get map rotation mode
    const char* rotation_mode_default = "toml";
    TOMLH_GET_STRING(server_table, rotation_mode_str, "map_rotation_mode", rotation_mode_default, 0);
    if (rotation_mode_str != NULL) {
        if (strcmp(rotation_mode_str, "alphabetic") == 0) {
            rotation_mode = MAP_ROTATION_ALPHABETIC;
        } else if (strcmp(rotation_mode_str, "random") == 0) {
            rotation_mode = MAP_ROTATION_RANDOM;
        } else if (strcmp(rotation_mode_str, "toml") == 0) {
            rotation_mode = MAP_ROTATION_TOML_DEFINED;
        } else {
            LOG_WARNING("Unknown map_rotation_mode '%s', defaulting to toml", rotation_mode_str);
            rotation_mode = MAP_ROTATION_TOML_DEFINED;
        }
    }


    TOMLH_GET_INT_ARRAY(server_table, periodic_delays, "periodic_delays", 5, ((uint8_t[]){1, 5, 10, 30, 60}), 1);
    TOMLH_GET_STRING_ARRAY_AS_DL(server_table, welcome_message_list, welcome_message_list_len, "welcome_messages", 1);
    TOMLH_GET_STRING_ARRAY_AS_DL(server_table, periodic_message_list, periodic_message_list_len, "periodic_messages", 1);

    /* [teams] */
    toml_table_t* teams_table;
    toml_table_t* team1_table;
    toml_table_t* team2_table;
    TOMLH_GET_TABLE(parsed, teams_table, "teams");

    TOMLH_GET_TABLE(teams_table, team1_table, "team1");
    TOMLH_GET_STRING(team1_table, team1_name, "name", "Blue Team", 0);
    TOMLH_GET_RGB_COLOR(team1_table, team1_color, "color", ((uint8_t[]){0, 0, 255}), 0);

    TOMLH_GET_TABLE(teams_table, team2_table, "team2");
    TOMLH_GET_STRING(team2_table, team2_name, "name", "Red Team", 0);
    TOMLH_GET_RGB_COLOR(team2_table, team2_color, "color", ((uint8_t[]){255, 0, 0}), 0);

    /* [passwords] */
    toml_table_t* passwords_table;
    TOMLH_GET_TABLE(parsed, passwords_table, "passwords");
    TOMLH_GET_STRING(passwords_table, manager_passwd, "manager", "", 0);
    TOMLH_GET_STRING(passwords_table, admin_passwd, "admin", "", 0);
    TOMLH_GET_STRING(passwords_table, mod_passwd, "moderator", "", 0);
    TOMLH_GET_STRING(passwords_table, guard_passwd, "guard", "", 0);
    TOMLH_GET_STRING(passwords_table, trusted_passwd, "trusted", "", 0);

    server_args args = {.port                      = port,
                        .connections               = 64,
                        .channels                  = 2,
                        .in_bandwidth              = 0,
                        .out_bandwidth             = 0,
                        .master                    = master,
                        .map_list                  = map_list,
                        .map_count                 = map_list_len,
                        .welcome_message_list      = welcome_message_list,
                        .welcome_message_list_len  = welcome_message_list_len,
                        .periodic_message_list     = periodic_message_list,
                        .periodic_message_list_len = periodic_message_list_len,
                        .periodic_delays           = periodic_delays,
                        .manager_password          = manager_passwd,
                        .admin_password            = admin_passwd,
                        .mod_password              = mod_passwd,
                        .guard_password            = guard_passwd,
                        .trusted_password          = trusted_passwd,
                        .server_name               = server_name,
                        .team1_name                = team1_name,
                        .team2_name                = team2_name,
                        .team1_color               = team1_color,
                        .team2_color               = team2_color,
                        .gamemode                  = gamemode,
                        .capture_limit             = capture_limit,
                        .map_rotation_mode         = rotation_mode};

    server_start(args);

    free((char*) server_name);
    free((char*) manager_passwd);
    free((char*) admin_passwd);
    free((char*) mod_passwd);
    free((char*) guard_passwd);
    free((char*) trusted_passwd);
    free((char*) team1_name);
    free((char*) team2_name);
    if (rotation_mode_str != rotation_mode_default) {
        free((char*) rotation_mode_str);
    }
    toml_free(parsed);

    return 0;
}
