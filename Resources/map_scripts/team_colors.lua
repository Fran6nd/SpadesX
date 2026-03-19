-- team_colors.lua — lock players to their team's build color
--
-- The player's tool color is kept in sync with their team color at all times:
--   - Any color change attempt is rejected and corrected immediately, so the
--     player's color picker always shows the right color and future blocks
--     are placed in the correct color on their own client too.
--   - Block placements are overridden as a safety net for the map state and
--     for other clients, covering the window before the first color correction.

local function team_color(player_id)
    local team = player.get_team(player_id)
    if team == Team.A or team == Team.B then
        return server.get_team_color(team)
    end
end

-- Correct the player's tool color whenever they attempt to change it.
-- set_color_broadcast pushes the correction back to the player's own client
-- so their color picker immediately reflects the team color.
on.color_change(function(player_id, color)
    local tc = team_color(player_id)
    if not tc then return end
    player.set_color_broadcast(player_id, tc)
    return tc
end)

-- Safety net: override the stored block color in the map and in the broadcast
-- to other clients, and correct the placer's tool color if it drifted.
on.block_place(function(player_id, x, y, z, color)
    local tc = team_color(player_id)
    if not tc then return end
    local pc = player.get_color(player_id)
    if pc.r ~= tc.r or pc.g ~= tc.g or pc.b ~= tc.b then
        player.set_color_broadcast(player_id, tc)
    end
    return tc
end)
