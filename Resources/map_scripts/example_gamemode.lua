-- example_gamemode.lua — Babel-style gamemode (platform, team colors, trail)

local TOOL_SPADE = 0

local TEAM_COLOR = {
    [0] = {r = 0,   g = 0,   b = 255},
    [1] = {r = 255, g = 0,   b = 0},
}

local player_trail = {}

on.map_load(function(map_name)
    log.info("Initializing " .. map_name .. " gamemode")
    for x = 206, 306 do
        for y = 240, 272 do
            map.set_block(x, y, 1, 0, 255, 255)
        end
    end
    log.info("Platform created")
end)

on.map_unload(function(map_name)
    player_trail = {}
end)

on.block_destroy(function(player_id, x, y, z)
    local on_platform =
        (x >= 206 and x <= 306 and y >= 240 and y <= 272 and (z == 0 or z == 2)) or
        (x >= 205 and x <= 307 and y >= 239 and y <= 273 and z == 1)
    if on_platform then
        player.send_notice(player_id, "You should try to destroy the enemy's tower... Not the platform!")
        return false
    end
    if player.get_tool(player_id) == TOOL_SPADE then
        return true
    end
    local team = player.get_team(player_id)
    if (team == 1 and x > 292) or (team == 0 and x < 220) then
        player.send_notice(player_id, "You should try to destroy the enemy's tower... It is not on this side of the map!")
        return false
    end
    return true
end)

on.block_place(function(player_id, x, y, z, r, g, b)
    local team = player.get_team(player_id)
    if not team then return end
    local tc = TEAM_COLOR[team]
    if not tc then return end
    if player.get_blocks(player_id) < 10 then
        player.restock(player_id)
    end
    local pr, pg, pb = player.get_color(player_id)
    if pr ~= tc.r or pg ~= tc.g or pb ~= tc.b then
        player.set_color_broadcast(player_id, tc.r, tc.g, tc.b)
    end
    return tc.r, tc.g, tc.b
end)

on.color_change(function(player_id, r, g, b)
    local team = player.get_team(player_id)
    if not team then return end
    local tc = TEAM_COLOR[team]
    if not tc then return end
    return tc.r, tc.g, tc.b
end)

on.player_connect(function(player_id)
    player.send_notice(player_id, "Welcome to the Babel-style server!")
    player.send_notice(player_id, "Type /restock to refill your blocks and grenades")
    player.send_notice(player_id, "Headshots only mode enabled!")
end)

on.player_disconnect(function(player_id, reason)
    player_trail[player_id] = nil
end)

on.tick(function()
    for id, name in player.iterate() do
        local x, y, z = player.get_position(id)
        if x then
            local bx = math.floor(x)
            local by = math.floor(y)
            local bz = math.floor(z) - 3
            local trail = player_trail[id]
            local moved = not trail or trail.x ~= bx or trail.y ~= by or trail.z ~= bz
            if moved and map.is_valid(bx, by, bz) then
                map.set_block(bx, by, bz, 255, 255, 0)
                player_trail[id] = {x = bx, y = by, z = bz}
            end
        end
    end
end)
