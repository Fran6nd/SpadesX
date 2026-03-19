-- restock_on_kill.lua — refill blocks and grenades on kill

on.player_kill(function(killer_id, victim_id, kill_reason)
    player.restock(killer_id)
end)
