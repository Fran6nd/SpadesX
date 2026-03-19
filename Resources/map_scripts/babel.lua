-- babel.lua — Tower of Babel gamemode
--
-- Original gamemode by Yourself, modifications by izzy.
-- Rewritten for SpadesX by the SpadesX contributors.
--
-- Teams build towers from their base toward the central platform where the
-- enemy flag floats at sky level (z=0). First team to capture wins (cap=1).
--
-- Rules:
--   - Central platform is indestructible and cannot be built on.
--   - Players cannot destroy their own tower blocks with the spade.
--   - Players cannot shoot/grenade blocks from their own side of the map.
--   - Players cannot build while standing inside the enemy's tower zone.
--   - Players spawn at a random point around their base.
--   - Players cannot line-build while in or near their own tower zone.

-- ============================================================================
-- Configuration
-- ============================================================================

local CENTER_X        = 256
local CENTER_Y        = 256
local PLATFORM_HALF_W = 50    -- PLATFORM_WIDTH  / 2  (100 / 2)
local PLATFORM_HALF_H = 16    -- PLATFORM_HEIGHT / 2  (32 / 2)
local PLATFORM_Z      = 1     -- topmost buildable layer (near sky)
local FLAG_Z          = 0     -- sky level — players must build up to reach it

local BLUE_FLAG_X  = CENTER_X - PLATFORM_HALF_W + 1  -- 207
local GREEN_FLAG_X = CENTER_X + PLATFORM_HALF_W - 1  -- 305
local FLAG_Y       = CENTER_Y

local BLUE_BASE_X  = CENTER_X - 138   -- 118
local GREEN_BASE_X = CENTER_X + 138   -- 394
local BASE_Y       = CENTER_Y

-- Tower protection zones (player-position based)
local BLUE_TOWER  = { x1 = 128, x2 = 211, y1 = 240, y2 = 272 }
local GREEN_TOWER = { x1 = 301, x2 = 384, y1 = 240, y2 = 272 }

local PLATFORM_COLOR = Color(255, 255, 255)

-- ============================================================================
-- Helpers
-- ============================================================================

local function on_platform(x, y, z)
    if z <= 2 then
        if x >= CENTER_X - PLATFORM_HALF_W and x <= CENTER_X + PLATFORM_HALF_W
           and y >= CENTER_Y - PLATFORM_HALF_H and y <= CENTER_Y + PLATFORM_HALF_H
        then
            return true
        end
    end
    -- One-block border at z == 1
    if z == 1 then
        if x >= CENTER_X - PLATFORM_HALF_W - 1 and x <= CENTER_X + PLATFORM_HALF_W + 1
           and y >= CENTER_Y - PLATFORM_HALF_H - 1 and y <= CENTER_Y + PLATFORM_HALF_H + 1
        then
            return true
        end
    end
    return false
end

local function in_zone(zone, x, y)
    return x >= zone.x1 and x <= zone.x2 and y >= zone.y1 and y <= zone.y2
end

-- ============================================================================
-- Map initialization
-- ============================================================================

on.map_load(function(map_name)
    -- Build the central white platform
    for x = CENTER_X - PLATFORM_HALF_W, CENTER_X + PLATFORM_HALF_W do
        for y = CENTER_Y - PLATFORM_HALF_H, CENTER_Y + PLATFORM_HALF_H do
            map.set_block(x, y, PLATFORM_Z, PLATFORM_COLOR)
        end
    end

    -- Place flags at sky level on the platform edges
    server.set_intel_position(Team.A, BLUE_FLAG_X,  FLAG_Y, FLAG_Z)
    server.set_intel_position(Team.B, GREEN_FLAG_X, FLAG_Y, FLAG_Z)

    -- Bases far from center (players spawn and score here)
    local z_a = map.find_top(BLUE_BASE_X,  BASE_Y) or 63
    local z_b = map.find_top(GREEN_BASE_X, BASE_Y) or 63
    server.set_base_position(Team.A, BLUE_BASE_X,  BASE_Y, z_a)
    server.set_base_position(Team.B, GREEN_BASE_X, BASE_Y, z_b)

    -- First capture wins
    server.set_capture_limit(1)

    log.info("Babel: platform built — blue flag at (" .. BLUE_FLAG_X  .. "," .. FLAG_Y .. ")"
          .. ", green flag at (" .. GREEN_FLAG_X .. "," .. FLAG_Y .. ")")
end)

-- ============================================================================
-- Block destruction rules
-- ============================================================================

on.block_destroy(function(player_id, x, y, z)
    -- Platform is indestructible
    if on_platform(x, y, z) then
        return false
    end

    local team = player.get_team(player_id)
    if team ~= Team.A and team ~= Team.B then return end

    local pos  = player.get_position(player_id)
    if not pos then return end

    local tool = player.get_tool(player_id)
    local px   = math.floor(pos.x)
    local py   = math.floor(pos.y)

    if team == Team.A then
        -- Cannot spade own tower blocks
        if tool == Tool.SPADE and in_zone(BLUE_TOWER, px, py) then
            player.send_notice(player_id, "You can't destroy your team's blocks here. Attack the enemy's tower!")
            return false
        end
        -- Must cross midfield before shooting/grenading blocks
        if px <= 288 and (tool == Tool.GUN or tool == Tool.GRENADE) then
            player.send_notice(player_id, "You must be closer to the enemy's base to shoot blocks!")
            return false
        end

    elseif team == Team.B then
        -- Cannot spade own tower blocks
        if tool == Tool.SPADE and in_zone(GREEN_TOWER, px, py) then
            player.send_notice(player_id, "You can't destroy your team's blocks here. Attack the enemy's tower!")
            return false
        end
        -- Must cross midfield before shooting/grenading blocks
        if px >= 224 and (tool == Tool.GUN or tool == Tool.GRENADE) then
            player.send_notice(player_id, "You must be closer to the enemy's base to shoot blocks!")
            return false
        end
    end
end)

-- ============================================================================
-- Block placement rules
-- ============================================================================

on.block_place(function(player_id, x, y, z, color)
    -- Cannot build on or inside the platform
    if on_platform(x, y, z) then
        return false
    end

    local team = player.get_team(player_id)
    if team ~= Team.A and team ~= Team.B then return end

    local pos = player.get_position(player_id)
    if not pos then return end

    local px = math.floor(pos.x)
    local py = math.floor(pos.y)

    -- Blue cannot build while standing inside the enemy's tower zone
    if team == Team.A and in_zone(GREEN_TOWER, px, py) then
        player.send_notice(player_id, "You can't build near the enemy's tower!")
        return false
    end

    -- Green cannot build while standing inside the enemy's tower zone
    if team == Team.B and in_zone(BLUE_TOWER, px, py) then
        player.send_notice(player_id, "You can't build near the enemy's tower!")
        return false
    end
end)

-- ============================================================================
-- Flag events
-- ============================================================================

on.intel_take(function(player_id, team)
    server.broadcast(player.get_name(player_id) .. " picked up the flag!")
end)

on.intel_drop(function(player_id, team, pos)
    server.broadcast(player.get_name(player_id) .. " dropped the flag!")
end)

on.intel_capture(function(player_id, team)
    server.broadcast(player.get_name(player_id) .. " captured the flag! Game over!")
end)

-- ============================================================================
-- Spawn override — random position around team base
-- ============================================================================

on.player_spawn(function(player_id)
    local team = player.get_team(player_id)
    if team ~= Team.A and team ~= Team.B then return end

    local base_x = (team == Team.A) and BLUE_BASE_X or GREEN_BASE_X
    local x = math.max(0, math.min(511, base_x + math.random(-20, 20)))
    local y = math.max(0, math.min(511, BASE_Y  + math.random(-20, 20)))
    local z = map.find_top(x, y) or 63

    return Vector3D(x + 0.5, y + 0.5, z - 2.4)
end)

-- ============================================================================
-- Line-build rules — same zone restrictions as single-block builds
-- ============================================================================

on.block_line(function(player_id, start_pos, end_pos)
    local team = player.get_team(player_id)
    if team ~= Team.A and team ~= Team.B then return end

    local pos = player.get_position(player_id)
    if not pos then return end

    local px = math.floor(pos.x)
    local py = math.floor(pos.y)

    -- Cannot line-build on or through the platform
    if on_platform(start_pos.x, start_pos.y, start_pos.z) or
       on_platform(end_pos.x,   end_pos.y,   end_pos.z)
    then
        player.send_notice(player_id, "You can't build on the central platform!")
        return false
    end

    -- Blue cannot line-build while standing in enemy tower zone
    if team == Team.A and in_zone(GREEN_TOWER, px, py) then
        player.send_notice(player_id, "You can't build near the enemy's tower!")
        return false
    end

    -- Green cannot line-build while standing in enemy tower zone
    if team == Team.B and in_zone(BLUE_TOWER, px, py) then
        player.send_notice(player_id, "You can't build near the enemy's tower!")
        return false
    end
end)
