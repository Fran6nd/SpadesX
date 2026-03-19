-- babel.lua — ONE_CTF: one shared flag at the center of the map
--
-- Both flags spawn at (256, 256). When a player picks up the enemy flag,
-- their own team's flag is moved to an unreachable position so only one
-- flag is active at a time. On drop the hidden flag reappears at the drop
-- location; on capture both flags reset to center.

local SPAWN_X = 256
local SPAWN_Y = 256

-- z=64 is below the map boundary — clients display the flag as unreachable.
local HIDE_X, HIDE_Y, HIDE_Z = 0, 0, 64

local function spawn_z()
    return map.find_top(SPAWN_X, SPAWN_Y) or 0
end

local function reset_both_flags()
    local z = spawn_z()
    server.set_intel_position(Team.A, SPAWN_X, SPAWN_Y, z)
    server.set_intel_position(Team.B, SPAWN_X, SPAWN_Y, z)
end

on.map_load(function(map_name)
    reset_both_flags()
    log.info("Babel: flags placed at center (" .. SPAWN_X .. ", " .. SPAWN_Y .. ")")
end)

-- When a player takes the enemy flag, hide their own team's flag so only
-- one flag is in play at a time.
-- team: the enemy team whose flag was taken.
on.intel_take(function(player_id, team)
    local own_team = (team == Team.A) and Team.B or Team.A
    server.set_intel_position(own_team, HIDE_X, HIDE_Y, HIDE_Z)
    server.broadcast(player.get_name(player_id) .. " picked up the flag!")
end)

-- When the flag is dropped, restore the hidden flag at the drop location
-- so both flags are again accessible at the same spot.
-- team: the enemy team whose flag was dropped; pos: drop Vector3D.
on.intel_drop(function(player_id, team, pos)
    local own_team = (team == Team.A) and Team.B or Team.A
    server.set_intel_position(own_team, pos.x, pos.y, pos.z)
    server.broadcast(player.get_name(player_id) .. " dropped the flag!")
end)

-- On capture, reset both flags to the center spawn.
-- team: the enemy team whose flag was captured.
on.intel_capture(function(player_id, team)
    reset_both_flags()
    server.broadcast(player.get_name(player_id) .. " captured the flag!")
end)
