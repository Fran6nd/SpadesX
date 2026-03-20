-- example_gamemode.lua — Babel-style gamemode (platform, team colors, trail)

local player_trail = {}

on.map_load(function(map_name)
    log.info("Initializing " .. map_name .. " gamemode")
    local platform_color = Color(0, 255, 255)
    for x = 206, 306 do
        for y = 240, 272 do
            map.set_block(x, y, 1, platform_color)
        end
    end
    log.info("Platform created")
end)

on.map_unload(function(map_name)
    player_trail = {}
end)

on.block_destroy(function(player_id, x, y, z, tool)
    local on_platform =
        (x >= 206 and x <= 306 and y >= 240 and y <= 272 and (z == 0 or z == 2)) or
        (x >= 205 and x <= 307 and y >= 239 and y <= 273 and z == 1)
    if on_platform then
        player.send_notice(player_id, "You should try to destroy the enemy's tower... Not the platform!")
        return false
    end
    if tool == Tool.SPADE then
        return true
    end
    local team = player.get_team(player_id)
    if (team == Team.B and x > 292) or (team == Team.A and x < 220) then
        player.send_notice(player_id, "You should try to destroy the enemy's tower... It is not on this side of the map!")
        return false
    end
    return true
end)

on.block_place(function(player_id, x, y, z, color)
    local team = player.get_team(player_id)
    if team ~= Team.A and team ~= Team.B then return end
    local tc = server.get_team_color(team)
    if player.get_blocks(player_id) < 10 then
        player.restock(player_id)
    end
    local pc = player.get_color(player_id)
    if pc.r ~= tc.r or pc.g ~= tc.g or pc.b ~= tc.b then
        player.set_color_broadcast(player_id, tc)
    end
    return tc
end)

on.color_change(function(player_id, color)
    local team = player.get_team(player_id)
    if team ~= Team.A and team ~= Team.B then return end
    local tc = server.get_team_color(team)
    player.set_color_broadcast(player_id, tc)
    return tc
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
    local trail_color = Color(255, 255, 0)
    for id, name in player.iterate() do
        local pos = player.get_position(id)
        if pos then
            local bx = math.floor(pos.x)
            local by = math.floor(pos.y)
            local bz = math.floor(pos.z) - 3
            local trail = player_trail[id]
            local moved = not trail or trail.x ~= bx or trail.y ~= by or trail.z ~= bz
            if moved and map.is_valid(bx, by, bz) then
                map.set_block(bx, by, bz, trail_color)
                player_trail[id] = {x = bx, y = by, z = bz}
            end
        end
    end
end)
