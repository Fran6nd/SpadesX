-- shotguns_are_grenade_launchers.lua
--
-- Each shotgun shot that connects with a player or destroys a block triggers
-- a real grenade explosion at the point of impact, attributed to the shooter.
--
-- Debounce: a per-shooter tick counter ensures at most one explosion fires per
-- shot, regardless of how many of the 8 pellets report a hit in the same tick.

local tick_count        = 0
local last_explode_tick = {}  -- [player_id] = tick on which last explosion fired

on.tick(function()
    tick_count = tick_count + 1
end)

-- Pellet hit a player.
on.player_hit(function(shooter_id, victim_id, hit_type, weapon)
    if weapon ~= Weapon.SHOTGUN then return end
    if last_explode_tick[shooter_id] == tick_count then return end
    last_explode_tick[shooter_id] = tick_count
    local pos = player.get_position(victim_id)
    server.explode(shooter_id, pos.x, pos.y, pos.z)
end)

-- Pellet destroyed a block (client-reported BlockAction).
on.block_destroy(function(player_id, x, y, z)
    if player.get_weapon(player_id) ~= Weapon.SHOTGUN then return end
    if last_explode_tick[player_id] == tick_count then return end
    last_explode_tick[player_id] = tick_count
    server.explode(player_id, x + 0.5, y + 0.5, z + 0.5)
end)
