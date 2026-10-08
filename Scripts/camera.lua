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
    orbit = 0.6,       -- 弧度/秒：无输入时是否绕场景环绕（0 = 静止，便于对照截图）
    radius = 4.4,      -- 环绕半径：大到两个 actor 在任何方位角都在画面里（演示/验收都靠它）
}

function self.update(dt)
    local moveX, moveZ, lookX, lookY = maho.get_input(self.entity)
    local x, y, z = maho.get_position(self.entity)
    local pitch, yaw, roll = maho.get_rotation(self.entity)

    -- 用户拖出来的偏航是**相对**"看向原点"的偏移，必须先取出来 —— 否则下面每帧重算的
    -- "看向原点"会把左右拖拽立刻抹掉（症状：上下能拖、左右弹回原位）。
    local BaseYaw = math.atan(x, z)
    local YawOffset = yaw - BaseYaw

    -- 脚本自己推动的环绕：绕 Y 轴把位置转一个角度，再让朝向始终对着原点（+ 用户偏移）——
    -- 于是物体会一直在画面里、视角在变，同时用户左右拖拽依然有效（改这一行无需重编）。
    local a = self.orbit * dt
    local ca, sa = math.cos(a), math.sin(a)
    x, z = x * ca + z * sa, -x * sa + z * ca
    -- 半径钉住：起手位置离原点多远不影响演示（否则改相机初始 Z 就会有人出画）。
    local d = math.sqrt(x * x + z * z)
    if d > 0.0001 then
        x, z = x / d * self.radius, z / d * self.radius
    end
    yaw = math.atan(x, z) + YawOffset   -- Lua 5.4 没有 math.atan2；math.atan 双参数版就是 atan2(y, x)

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
