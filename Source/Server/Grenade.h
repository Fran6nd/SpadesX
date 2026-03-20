#ifndef GRENADE_H
#define GRENADE_H

#include <Server/Structs/ServerStruct.h>

vector3i_t* getGrenadeNeighbors(vector3i_t pos);
uint8_t     get_grenade_damage(server_t* server, player_t* damaged_player, grenade_t* grenade);
void        grenade_explode_at(server_t* server, player_t* player, vector3f_t pos);
// Trigger a server-originated explosion with no player attribution.
// Uses player_id=32 as the network sentinel (out of range for real players).
// All players can be damaged regardless of team.
void        grenade_explode_at_server(server_t* server, vector3f_t pos);
void        handle_grenade(server_t* server, player_t* player);

#endif
