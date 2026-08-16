-- Maho game script entry (loaded as Scripts/main.lua on engine init).
-- Optional globals called from the engine:
--   OnUpdate(dt)
--   OnFixedUpdate(fixedDt)

maho.log("Scripts/main.lua loaded")
maho.log_warn("Scripts/main.lua loaded")
maho.log_error("Scripts/main.lua loaded")

local Accumulated = 0.0

function OnUpdate(dt)
	Accumulated = Accumulated + dt
	-- Uncomment to spam the log while testing:
	-- maho.log(string.format("OnUpdate dt=%.4f acc=%.2f", dt, Accumulated))
end

function OnFixedUpdate(fixedDt)
	-- physics / fixed-step gameplay
end
