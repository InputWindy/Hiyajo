-- camera.lua — 演示 gameplay：相机也是一个 entity，它的操控逻辑全在这里。
--
-- 输入来自引擎的 `FInputComponent`（世界每帧从平台填好）：
--   moveX/moveZ  = 横移/前后（A/D、S/W，取值 -1/0/1）
--   lookX/lookY  = 本帧的鼠标位移（只在按住左键时有值，免得指针随便一飘就把相机甩走）
--
-- 相机位姿写回自己的 FTransform：渲染侧一行相机代码都没有，它只消费镜像表里的 view-projection。

local self = {
    speed = 3.0,       -- 米/秒
    lookSpeed = 0.004, -- 弧度/像素
    pitchLimit = 1.5,  -- 俯仰上下限（弧度），避免翻过头
    orbit = 0.6,       -- 弧度/秒：无输入时也绕场景环绕（证明相机位姿是脚本给的，不是渲染侧写死的）
}

function self.update(dt)
    local moveX, moveZ, lookX, lookY = maho.get_input(self.entity)
    local x, y, z = maho.get_position(self.entity)
    local pitch, yaw, roll = maho.get_rotation(self.entity)

    -- 脚本自己推动的环绕：绕 Y 轴把位置转一个角度，再让朝向始终对着原点 ——
    -- 于是物体会一直在画面里，但**视角在变**（改这一行就能看到，无需重编）。
    local a = self.orbit * dt
    local ca, sa = math.cos(a), math.sin(a)
    x, z = x * ca + z * sa, -x * sa + z * ca
    yaw = math.atan(x, z)   -- Lua 5.4 没有 math.atan2；math.atan 双参数版就是 atan2(y, x)

    -- 鼠标右移 = 向右看 = 绕 Y 轴负向；鼠标下移 = 低头 = 绕 X 轴负向
    yaw = yaw - lookX * self.lookSpeed
    pitch = pitch - lookY * self.lookSpeed
    if pitch > self.pitchLimit then pitch = self.pitchLimit end
    if pitch < -self.pitchLimit then pitch = -self.pitchLimit end

    -- 相机自己的朝向（yaw）决定前后与横移方向：forward = (-sin yaw, 0, -cos yaw)
    local s = math.sin(yaw)
    local c = math.cos(yaw)
    x = x + (-s * moveZ + c * moveX) * self.speed * dt
    z = z + (-c * moveZ - s * moveX) * self.speed * dt

    maho.set_position(self.entity, x, y, z)
    maho.set_rotation(self.entity, pitch, yaw, roll)
end

return self
