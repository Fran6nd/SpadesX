-- headshot_only.lua — deny non-headshot, non-melee hits

on.player_hit(function(shooter_id, victim_id, hit_type, weapon)
    -- hit_type: 0=torso, 1=head, 2=arms, 3=legs, 4=melee
    if hit_type ~= 1 and hit_type ~= 4 then
        player.send_notice(shooter_id, "Headshots only!")
        return false
    end
end)
