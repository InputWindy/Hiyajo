-- cube2.lua — 第二个 actor 的 gameplay：绕 Z 轴自转，比 cube.lua 快。
--
-- 与 cube.lua 是同一个 C++ 系统装出来的，唯一区别在脚本里；两边的状态互不干扰
-- （每个 entity 加载脚本时拿到的是自己那一份表）。两个 actor 同屏 + 各自转速不同，
-- 就是"gameplay 在 Lua 里、且逐 entity 独立"的证据。

local self = {
    speed = 2.4,    -- 弧度/秒（绕 Z 轴）
    tilt = 0.25,    -- 固定的 X 轴倾角
}

function self.update(dt)
    local rx, ry, rz = maho.get_rotation(self.entity)
    maho.set_rotation(self.entity, self.tilt, ry, rz + self.speed * dt)
end

return self
