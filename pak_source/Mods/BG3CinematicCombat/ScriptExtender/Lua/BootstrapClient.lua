-- BG3 Cinematic Combat - Client Bootstrap
-- Owns HUD visibility during kill/crit cams: hides a curated set of HUD
-- widgets via the Noesis UI tree (deterministic Visibility writes - NO key
-- simulation; the old F10 SendInput approach froze input and desynced).
-- Driven by "BG3CC_HUD" net messages from the server-side detector.

Ext.Utils.Print("[BG3CinematicCombat] Client loaded")

-- Diagnostic UI tree dump (writes CinematicCombat_uitree.json). OFF for releases.
local DEBUG_UI_DUMP = false

-- Widget names verified via UI tree dump 2026-07-06 (direct children of
-- "ContentRoot"). Deliberately NOT hidden: ScreenFade, Overlay,
-- AlwaysOnTopOverlay, WorldContextMenu, PassiveRoll, DragAndDropPreview.
local HIDE_WIDGETS = {
    HudIndicator = true,
    OverheadInfo = true,
    Minimap = true,
    TargetInfo = true,
    TurnModeInfo = true,
    CombatLog = true,
    CombatantsOverlay = true,
    WorldTooltips = true,
    HotBar = true,
    PlayerPortraits = true,
    CursorText = true,
    NotificationWidget = true,
}

-- ===== Noesis tree helpers =====

local function ChildrenOf(node)
    local children = {}
    local count = 0
    pcall(function() count = tonumber(node.VisualChildrenCount) or 0 end)
    if count <= 0 then return children end
    -- Noesis is 0-based; fall back to 1-based if 0 yields nothing
    for i = 0, count - 1 do
        local c = nil
        pcall(function() c = node:VisualChild(i) end)
        if c ~= nil then children[#children + 1] = c end
    end
    if #children == 0 then
        for i = 1, count do
            local c = nil
            pcall(function() c = node:VisualChild(i) end)
            if c ~= nil then children[#children + 1] = c end
        end
    end
    return children
end

local function NameOf(node)
    local name = nil
    pcall(function()
        local n = node.Name
        if n ~= nil and tostring(n) ~= "" then name = tostring(n) end
    end)
    return name
end

-- Breadth-first search for a node by name (the HUD container sits only a few
-- levels below the root, so the depth limit keeps this cheap)
local function FindByName(root, wanted, maxDepth)
    local queue = { { node = root, depth = 0 } }
    local head = 1
    while queue[head] do
        local cur = queue[head]
        head = head + 1
        if NameOf(cur.node) == wanted then return cur.node end
        if cur.depth < maxDepth then
            for _, c in ipairs(ChildrenOf(cur.node)) do
                queue[#queue + 1] = { node = c, depth = cur.depth + 1 }
            end
        end
    end
    return nil
end

-- ===== HUD hide/restore =====

-- IMPORTANT LESSON (diagnosed via client log): stored widget REFERENCES die
-- within seconds (restore failed with ok=0 fail=12 on stale handles - even
-- reads threw). Only NAMES survive, so both hide and restore do a fresh
-- lookup and state is kept as name -> previous visibility string.
local hiddenPrev = nil    -- map: widget name -> previous visibility string
local restoreAt = nil     -- MonotonicTime (ms) deadline for restoring
local lastHideId = nil    -- id of the most recent hide message

-- Diagnostics: lifecycle log written to CinematicCombat_client.json so a
-- failing hide/restore can be analyzed from data instead of guesses.
local CLIENT_DEBUG = false
local clientLog = {}
local function CLog(ev, detail)
    if not CLIENT_DEBUG then return end
    clientLog[#clientLog + 1] = { ev = ev, d = detail }
    while #clientLog > 120 do table.remove(clientLog, 1) end
    local ok, data = pcall(Ext.Json.Stringify, clientLog)
    if ok then Ext.IO.SaveFile("CinematicCombat_client.json", data) end
end

local function RestoreHud(reason)
    if not hiddenPrev then return end
    -- Fresh lookup - stored references would be stale by now
    local okCount, failCount = 0, 0
    local readback = {}
    local okRoot, root = pcall(Ext.UI.GetRoot)
    if okRoot and root ~= nil then
        local content = FindByName(root, "ContentRoot", 6)
        if content ~= nil then
            for _, w in ipairs(ChildrenOf(content)) do
                local name = NameOf(w)
                if name and hiddenPrev[name] ~= nil then
                    local ok = pcall(function() w.Visibility = hiddenPrev[name] end)
                    if ok then okCount = okCount + 1 else failCount = failCount + 1 end
                    pcall(function() readback[#readback + 1] = name .. "=" .. tostring(w.Visibility) end)
                end
            end
        end
    end
    hiddenPrev = nil
    restoreAt = nil
    CLog("restore", (reason or "?") .. " ok=" .. okCount .. " fail=" .. failCount ..
        " readback=" .. table.concat(readback, ","))
end

local function HideHud()
    if hiddenPrev then return end   -- already hidden (chained cams)
    local ok, root = pcall(Ext.UI.GetRoot)
    if not ok or root == nil then return end
    local content = FindByName(root, "ContentRoot", 6)
    if content == nil then
        Ext.Utils.Print("[BG3CinematicCombat] HUD hide: ContentRoot not found")
        return
    end

    local prevMap = {}
    local count = 0
    for _, w in ipairs(ChildrenOf(content)) do
        local name = NameOf(w)
        if name and HIDE_WIDGETS[name] then
            local prev = "Visible"
            pcall(function() prev = tostring(w.Visibility) end)
            local okSet = pcall(function() w.Visibility = "Collapsed" end)
            if okSet then
                prevMap[name] = prev
                count = count + 1
            end
        end
    end
    if count > 0 then
        hiddenPrev = prevMap
    end
    CLog("hide", "widgets=" .. tostring(count))
end

local function HideHudFor(durationMs)
    HideHud()
    -- Restore deadline, extended by chained cams. Checked every frame via
    -- Ext.Events.Tick below - client-side Ext.Timer proved unreliable (the
    -- HUD never came back in testing because the timer never fired).
    local target = Ext.Utils.MonotonicTime() + durationMs
    if restoreAt == nil or target > restoreAt then
        restoreAt = target
    end
end

-- ===== Camera anchor hold (kill cams) =====
-- The engine re-targets the game camera to the main character the moment
-- a kill ends the combat - WHILE the slow motion is still running. During
-- the kill-cam window we re-assert GameCameraBehavior.Target every frame
-- (fresh entity lookup each tick - stored refs go stale, see HUD lesson)
-- so the camera stays anchored on the killer; when the window expires the
-- engine takes over again and the vanilla pan to the main character plays.
local camHold = nil   -- { deadline, startedAt, anchorGuid, frz, samples, writes, fails }

-- vec3 reads may return views into component memory - always deep-copy
local function Vec3Copy(v)
    if v == nil then return nil end
    local ok, c = pcall(function() return { v[1], v[2], v[3] } end)
    if ok then return c end
    return nil
end

local function Vec3Str(v)
    if type(v) == "table" then
        return string.format("%.1f,%.1f,%.1f", v[1] or 0, v[2] or 0, v[3] or 0)
    end
    return tostring(v)
end

local function CamHoldTick()
    if not camHold then return end
    local now = Ext.Utils.MonotonicTime()
    if now >= camHold.deadline then
        CLog("camhold-end", "writes=" .. tostring(camHold.writes) ..
            " fails=" .. tostring(camHold.fails) ..
            " snaps=" .. tostring(camHold.snaps or 0))
        if CLIENT_DEBUG then
            pcall(function()
                Ext.IO.SaveFile("CinematicCombat_camdiag.json", Ext.Json.Stringify(camHold.samples))
            end)
        end
        camHold = nil
        return
    end
    local okAll, ents = pcall(Ext.Entity.GetAllEntitiesWithComponent, "GameCameraBehavior")
    if not okAll or type(ents) ~= "table" then
        camHold.fails = camHold.fails + 1
        return
    end
    local anchorEnt = nil
    if camHold.anchorGuid then
        pcall(function() anchorEnt = Ext.Entity.Get(camHold.anchorGuid) end)
    end
    for _, e in ipairs(ents) do
        local okW = pcall(function()
            local beh = e.GameCameraBehavior

            -- Freeze reference: camera position targets from the FIRST tick
            -- of the kill cam (Target proved nil in combat mode - the pan
            -- runs on these position fields instead)
            if camHold.frz == nil then
                camHold.frz = {
                    cur     = Vec3Copy(beh.TargetCurrent),
                    curLow  = Vec3Copy(beh.TargetCurrentLow),
                    dst     = Vec3Copy(beh.TargetDestination),
                    dstHigh = Vec3Copy(beh.TargetDestinationHigh),
                }
                CLog("camhold-freeze", "cur=" .. Vec3Str(camHold.frz.cur) ..
                    " dst=" .. Vec3Str(camHold.frz.dst) ..
                    " anchorResolved=" .. tostring(anchorEnt ~= nil))
            end

            -- ~10 Hz time series -> CinematicCombat_camdiag.json (reads
            -- happen BEFORE this tick's writes = they show engine values)
            if CLIENT_DEBUG and now - (camHold.lastSample or 0) >= 100 then
                camHold.lastSample = now
                local s = { t = now - camHold.startedAt }
                pcall(function() s.t1 = tostring(beh.Targets[1]) end)
                pcall(function() s.ntgts = #beh.Targets end)
                pcall(function() s.cur = Vec3Str(Vec3Copy(beh.TargetCurrent)) end)
                pcall(function() s.dst = Vec3Str(Vec3Copy(beh.TargetDestination)) end)
                pcall(function() s.mov = beh.MovingToTarget end)
                pcall(function() s.mode = beh.CameraMode end)
                camHold.samples[#camHold.samples + 1] = s
            end

            -- Intervention v3 (camdiag 2026-07-06): the combat-end pan
            -- re-derives TargetDestination from the Targets ARRAY every
            -- frame - position pinning only fights the integrator one step
            -- behind (visible jitter). The real lever is Targets[1] =
            -- killer: the engine then computes the pan goal itself and
            -- there is nothing left to fight. (IsPaused proved inert.)
            if anchorEnt ~= nil then
                local okT = pcall(function() beh.Targets[1] = anchorEnt end)
                if not okT then
                    pcall(function() beh.Targets = { anchorEnt } end)
                end
            end
            -- Safety net: only if the anchor still drifts off (>1.5m from
            -- the freeze point) snap it back - never fires in micro-steps,
            -- so it cannot jitter while the Targets lever works
            local c = Vec3Copy(beh.TargetCurrent)
            if c ~= nil and camHold.frz.cur ~= nil then
                local dx = c[1] - camHold.frz.cur[1]
                local dz = c[3] - camHold.frz.cur[3]
                if dx * dx + dz * dz > 2.25 then
                    beh.TargetCurrent = camHold.frz.cur
                    beh.TargetDestination = camHold.frz.cur
                    camHold.snaps = (camHold.snaps or 0) + 1
                end
            end
        end)
        if okW then camHold.writes = camHold.writes + 1
        else camHold.fails = camHold.fails + 1 end
    end
end

-- Backup restore path: frame tick + deadline. (Primary path is the
-- server-sent "show" message below.)
local tickAlive = 0
pcall(function()
    Ext.Events.Tick:Subscribe(function()
        tickAlive = tickAlive + 1
        CamHoldTick()
        if hiddenPrev == nil then return end
        if restoreAt == nil then
            RestoreHud("tick-nodeadline")
            return
        end
        local ok, now = pcall(Ext.Utils.MonotonicTime)
        if ok and now >= restoreAt then
            RestoreHud("tick-deadline")
        end
    end)
end)

-- Server drives both directions: "hide" at cam start, "show" (hide=false)
-- with the same id after the duration. Server timers are proven reliable.
Ext.Events.NetMessage:Subscribe(function(e)
    if e.Channel ~= "BG3CC_HUD" then return end
    local ok, msg = pcall(Ext.Json.Parse, e.Payload)
    if not ok or type(msg) ~= "table" then return end
    if msg.hide then
        lastHideId = msg.id
        CLog("msg-hide", "id=" .. tostring(msg.id) .. " dur=" .. tostring(msg.dur) ..
            " tickAlive=" .. tostring(tickAlive))
        HideHudFor(tonumber(msg.dur) or 3000)
    else
        CLog("msg-show", "id=" .. tostring(msg.id) .. " match=" ..
            tostring(msg.id == lastHideId) .. " tickAlive=" .. tostring(tickAlive))
        -- Only the show matching the LATEST hide restores (chained cams
        -- extend the window; stale shows are ignored)
        if msg.id == lastHideId then
            RestoreHud("server-show")
        end
    end
end)

-- Camera anchor hold, driven by the server-side kill detector
Ext.Events.NetMessage:Subscribe(function(e)
    if e.Channel ~= "BG3CC_CAM" then return end
    local ok, msg = pcall(Ext.Json.Parse, e.Payload)
    if not ok or type(msg) ~= "table" then return end
    local dur = tonumber(msg.dur) or 2400
    local now = Ext.Utils.MonotonicTime()
    camHold = {
        deadline = now + dur,
        startedAt = now,
        anchorGuid = msg.anchor,
        frz = nil,
        samples = {},
        writes = 0,
        fails = 0,
    }
    CLog("camhold-start", "anchor=" .. tostring(msg.anchor) .. " dur=" .. tostring(dur))
end)

-- Fresh session = fresh UI; drop any stale bookkeeping
Ext.Events.SessionLoaded:Subscribe(function()
    hiddenPrev = nil
    restoreAt = nil
    camHold = nil
end)

-- ===== Diagnostic UI tree dump =====
if DEBUG_UI_DUMP then
    local function DumpUiTree()
        local nodes = {}
        local count = 0
        local function visit(node, path, depth)
            if node == nil or count >= 5000 or depth > 16 then return end
            count = count + 1
            local entry = { p = path }
            pcall(function() entry.t = tostring(node) end)
            local nm = NameOf(node)
            if nm then entry.n = nm end
            pcall(function() entry.v = tostring(node.Visibility) end)
            nodes[#nodes + 1] = entry
            local kids = ChildrenOf(node)
            for i, c in ipairs(kids) do
                visit(c, path .. "/" .. i, depth + 1)
            end
        end
        local ok, root = pcall(Ext.UI.GetRoot)
        if not ok or root == nil then return end
        visit(root, "r", 0)
        local ok2, data = pcall(Ext.Json.Stringify, nodes)
        if ok2 then
            Ext.IO.SaveFile("CinematicCombat_uitree.json", data)
            Ext.Utils.Print("[BG3CinematicCombat] UI tree dumped: " .. tostring(#nodes) .. " nodes")
        end
    end
    Ext.Events.SessionLoaded:Subscribe(function()
        pcall(Ext.Timer.WaitFor, 8000, function() pcall(DumpUiTree) end)
    end)
end
