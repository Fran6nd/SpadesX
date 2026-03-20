-- headshot_only.lua — deny non-headshot, non-melee hits

on.player_hit(function(shooter_id, victim_id, hit_type, weapon)
    if weapon == Weapon.GRENADE then return end
    if hit_type ~= HitType.HEAD and hit_type ~= HitType.MELEE then
        player.send_notice(shooter_id, "Headshots only!")
        return false
    end
end)
