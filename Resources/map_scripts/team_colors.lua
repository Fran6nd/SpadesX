-- team_colors.lua — enforce team colors when building
--
-- Players can only build with their team's color (configured in config.toml).
-- Any block placed is silently recolored to the team color.
-- Any manual color change is overridden to the team color.

on.block_place(function(player_id, x, y, z, color)
    local team = player.get_team(player_id)
    if team == Team.A or team == Team.B then
        return server.get_team_color(team)
    end
end)

on.color_change(function(player_id, color)
    local team = player.get_team(player_id)
    if team == Team.A or team == Team.B then
        return server.get_team_color(team)
    end
end)
