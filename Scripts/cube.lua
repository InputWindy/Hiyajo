-- cube.lua — 演示 gameplay：一个自转的立方体 entity。
--
-- 连接方式（唯一、显式）：系统加载本文件后，把整数 entity id 写进返回的表里，
-- 于是脚本里的 `self.entity` 就是自己的 id。`update(dt)` 只收一个 float —— Lua 的
-- `Call` 只支持 0/1 个参数，所以"我是谁"必须由脚本自己持有，其余状态靠 id 回查。
--
-- 想让立方体转快点/慢点，只改下面 self.speed：不需要重新编译任何 C++。

local self = {
    speed = 1.2,    -- 弧度/秒（绕 Y 轴）
    tilt = 0.42,    -- 固定的 X 轴倾角，让三个轴都被真正用到（原来是渲染侧写死的）
    -- 材质：覆盖 C++ 组件里那条 `_BaseColor`（组件给的是绿色）。值在这里 = 改这一行不用重编引擎，
    -- 重启后画面颜色就该是下面的黄色（证明游戏侧参数真的走到了 set2 的材质块里）。
    base_color = { 0.95, 0.82, 0.20, 1.0 },
}

function self.update(dt)
    local rx, ry, rz = maho.get_rotation(self.entity)
    maho.set_rotation(self.entity, self.tilt, ry + self.speed * dt, rz)

    -- 材质参数（第一帧写一次就够；每帧写也可以，就是多几条冗余发布）。参数名大小写不敏感，
    -- 这里用 `_basecolor` 也能命中 shader 里的 `_BaseColor` —— 故意用小写来证明这一点。
    if not self.material_applied then
        self.material_applied = true
        maho.set_material_color(self.entity, "_basecolor",
            self.base_color[1], self.base_color[2], self.base_color[3], self.base_color[4])
    end
end

return self
