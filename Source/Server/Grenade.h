#ifndef GRENADE_H
#define GRENADE_H

#include <Server/Structs/ServerStruct.h>

vector3i_t* getGrenadeNeighbors(vector3i_t pos);
uint8_t     get_grenade_damage(server_t* server, player_t* damaged_player, grenade_t* grenade);
void        grenade_explode_at(server_t* server, player_t* player, vector3f_t pos);
void        handle_grenade(server_t* server, player_t* player);

#endif
