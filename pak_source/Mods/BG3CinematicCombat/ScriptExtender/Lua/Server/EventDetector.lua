-- BG3 Cinematic Combat - Server Event Detector
-- Detects combat events (kills, crits, hits, dashes) via Osiris/SE events and
-- writes them to a JSON file that the native DLL polls.

local EventDetector = {}

-- Analysis/debug mode: writes CinematicCombat_trace.json, _debug.json and
-- _sample.json to the Script Extender folder. OFF for releases.
local DEBUG_MODE = false

-- Unique id per Lua session. The DLL uses this to reset its event watermark
-- after a save reload (the counter below restarts at 0 then).
local sessionId = Ext.Utils.MonotonicTime()

-- Rolling buffer of recent events (the whole buffer is written on each flush;
-- the DLL dedupes via the monotonic counter, so nothing is lost or repeated)
local recentEvents = {}
local MAX_BUFFERED_EVENTS = 30
local eventCounter = 0

-- Events are blocked until SessionLoaded + grace period. Level loading fires
-- Died events for every pre-placed corpse on the map - without this gate they
-- triggered kill cams (incl. F10/slow motion) DURING the loading screen of a
-- new game (log-proven 2026-07-03).
local sessionReadyAt = math.huge

-- Rate limiting for hit events
local lastHitTime = -1000000

-- Server-side behavior config (overridden from MCM when present)
-- Default philosophy: cinematics for kills, crits and for ACTING AGAINST
-- ENEMIES (action cam covers the run-up, since a kill can never be known in
-- advance). Self-buffs, ally spells, dashes and normal-hit impact cams are
-- silent by default - always-on cameras proved exhausting in real play.
local Config = {
    killEnabled = true,
    critEnabled = true,
    hitEnabled = false,
    dashEnabled = false,        -- OPT-IN: dash chase cam
    spellEnabled = false,
    actionEnabled = true,       -- shoulder cam, but ONLY against enemy targets
    killOnlyEnemies = true,     -- ignore deaths of party members
    onlyPlayerAttacks = true,   -- crit/hit cams only for attacks by the party
    enemyCams = false,          -- OPT-IN: kill/crit cams when ENEMIES crit/down YOUR party members
    hideHud = true,             -- hide the HUD during kill/crit cams (client-side Noesis)
    hitMinIntervalMs = 6000,    -- min pause between hit cam events
}

-- Tell the client to hide the HUD for the duration of a kill/crit cam.
-- kind is "kill" or "crit" - the matching MCM timings define the window.
-- The RESTORE is server-driven too (server timers are proven reliable):
-- after the duration a "show" message with the same id goes out. The id
-- lets the client ignore stale shows when cams chain.
local function NotifyHudHide(kind)
    if not Config.hideHud then return end
    local durS = 2.3
    pcall(function()
        if MCM ~= nil then
            local fadeIn = tonumber(MCM.Get(kind .. "_in")) or 0.3
            local hold = tonumber(MCM.Get(kind .. "_hold")) or 1.5
            local fadeOut = tonumber(MCM.Get(kind .. "_out")) or 0.6
            -- -0.1: the HUD comes back slightly BEFORE the fade-out finishes
            -- (user request - the old +0.4 safety margin felt too late)
            durS = fadeIn + hold + fadeOut - 0.1
        end
    end)
    local durMs = math.floor(durS * 1000)
    local id = tostring(Ext.Utils.MonotonicTime())
    pcall(Ext.Net.BroadcastMessage, "BG3CC_HUD",
        Ext.Json.Stringify({ hide = true, dur = durMs, id = id }))
    pcall(Ext.Timer.WaitFor, durMs, function()
        pcall(Ext.Net.BroadcastMessage, "BG3CC_HUD",
            Ext.Json.Stringify({ hide = false, id = id }))
    end)
end

-- Keep the engine camera anchored on the killer for the kill-cam window.
-- The engine re-targets the game camera to the main character the MOMENT
-- the kill registers (while slow motion is still running). The client
-- re-asserts GameCameraBehavior.Target every frame until the window ends;
-- only then does the vanilla pan to the main character play.
-- anchorGuid = the attacker (nil -> client holds whatever is current).
local function NotifyCamHold(kind, anchorGuid)
    local durS = 2.4
    pcall(function()
        if MCM ~= nil then
            local fadeIn = tonumber(MCM.Get(kind .. "_in")) or 0.3
            local hold = tonumber(MCM.Get(kind .. "_hold")) or 1.5
            local fadeOut = tonumber(MCM.Get(kind .. "_out")) or 0.6
            durS = fadeIn + hold + fadeOut
        end
    end)
    pcall(Ext.Net.BroadcastMessage, "BG3CC_CAM", Ext.Json.Stringify({
        anchor = anchorGuid and tostring(anchorGuid) or nil,
        dur = math.floor(durS * 1000),
        id = tostring(Ext.Utils.MonotonicTime()),
    }))
end

local function LogOnce(store, key, msg)
    if not store[key] then
        store[key] = true
        Ext.Utils.Print("[BG3CinematicCombat] " .. msg)
    end
end
local errorLog = {}

-- ===== Helpers =====

local function GetEntityUuid(entity)
    if not entity then return nil end
    local ok, uuid = pcall(function() return entity.Uuid.EntityUuid end)
    if ok and uuid then return uuid end
    return nil
end

local function IsPartyMember(uuidOrGuid)
    if not uuidOrGuid then return false end
    local ok, res = pcall(Osi.IsPartyMember, uuidOrGuid, 1)
    return ok and res == 1
end

-- True only for actual creatures. Destructible OBJECTS (crates, barrels,
-- doors) also have Health components and reach 0 HP when smashed - without
-- this check, destroying a crate triggered the kill cam.
local function IsCharacterEntity(uuidOrGuid)
    if not uuidOrGuid then return false end
    local ok, entity = pcall(Ext.Entity.Get, uuidOrGuid)
    if not ok or not entity then return false end
    local ok2, comp = pcall(function() return entity.IsCharacter end)
    return ok2 and comp ~= nil
end

-- Current HP of an entity, or nil. Defined HERE in the helpers block because
-- HandleActionStart (defined mid-file) needs it - a later definition made the
-- reference nil and silently killed every action handler via pcall.
local function GetHp(uuidOrGuid)
    local ok, entity = pcall(Ext.Entity.Get, uuidOrGuid)
    if not ok or not entity then return nil end
    local ok2, hp = pcall(function() return entity.Health.Hp end)
    if ok2 then return hp end
    return nil
end

-- Pre-placed map corpses count as characters, but hitting them (e.g. with an
-- explosion) fires death events - they must never trigger a kill cam.
local function IsPreplacedCorpse(uuidOrGuid)
    if not uuidOrGuid then return false end
    local ok, entity = pcall(Ext.Entity.Get, uuidOrGuid)
    if not ok or not entity then return false end
    local ok2, comp = pcall(function() return entity.DeadByDefault end)
    return ok2 and comp ~= nil
end

-- Each entity gets at most ONE kill cam, ever. Re-damaging a corpse (AoE,
-- explosions) re-fires attack/death events for an entity that is long dead.
local seenDead = {}

-- Entities observed with HP > 0 during this session. Core insight: at impact
-- time the engine reports even FRESH kills as already dead, so "is it dead
-- now" cannot separate a dying enemy from a long-dead corpse hit by an AoE.
-- "Was it ever seen alive" and "is it in combat" can.
local aliveSeen = {}

local function IsInCombatSafe(uuidOrGuid)
    if not uuidOrGuid then return false end
    local ok, res = pcall(Osi.IsInCombat, uuidOrGuid)
    return ok and res == 1
end

-- Filters out Larian's technical helper entities (e.g. S_TUT_Start_Brinepool,
-- rubble helpers): they carry an IsCharacter component but are not real
-- creatures. Trace-proven separator: Osi.IsDead returns 0/1 for real
-- creatures (even 1 at impact time for fresh kills!) but nil for tech
-- entities - so "IsDead is non-nil" == "real creature".
local function IsRealCreature(uuidOrGuid)
    if not uuidOrGuid then return false end
    local ok, res = pcall(Osi.IsDead, uuidOrGuid)
    return ok and res ~= nil
end

-- Kills further away than this from the party never trigger a kill cam
-- (filters scripted/ambient deaths across the map, e.g. right after loading)
local KILL_MAX_DISTANCE = 30.0

local function IsNearParty(uuidOrGuid, maxDist)
    local ok, host = pcall(Osi.GetHostCharacter)
    if not ok or not host then return true end
    local ok2, dist = pcall(Osi.GetDistanceTo, host, uuidOrGuid)
    if not ok2 or dist == nil then return true end
    return dist <= maxDist
end

-- Returns x, y, z or nil
local function GetEntityPosition(entityUuid)
    if not entityUuid then return nil end
    local ok, entity = pcall(Ext.Entity.Get, entityUuid)
    if not ok or not entity then return nil end
    local ok2, pos = pcall(function() return entity.Transform.Transform.Translate end)
    if ok2 and pos then
        return pos[1], pos[2], pos[3]
    end
    -- older component layout fallback
    local ok3, transform = pcall(function() return entity.Transform end)
    if ok3 and transform and transform.Position then
        return transform.Position[1], transform.Position[2], transform.Position[3]
    end
    return nil
end

local function Flush()
    local ok, data = pcall(Ext.Json.Stringify, { session = sessionId, events = recentEvents })
    if ok then
        Ext.IO.SaveFile("CinematicCombat_events.json", data)
    end
end

-- Queue an event. pos is either {x,y,z} or nil; fallbackUuid is used to look
-- up a position when pos is nil.
local function QueueEvent(eventType, pos, fallbackUuid)
    -- No cinematics while a session is still loading/settling
    if Ext.Utils.MonotonicTime() < sessionReadyAt then return end

    eventCounter = eventCounter + 1

    local x, y, z = 0, 0, 0
    if pos then
        x = pos[1] or pos.x or 0
        y = pos[2] or pos.y or 0
        z = pos[3] or pos.z or 0
    end
    if x == 0 and y == 0 and z == 0 and fallbackUuid then
        local px, py, pz = GetEntityPosition(fallbackUuid)
        if px then x, y, z = px, py, pz end
    end

    table.insert(recentEvents, {
        type = eventType,
        x = x, y = y, z = z,
        time = eventCounter
    })
    while #recentEvents > MAX_BUFFERED_EVENTS do
        table.remove(recentEvents, 1)
    end

    Ext.Utils.Print(string.format(
        "[BG3CinematicCombat] Event: %s at (%.1f, %.1f, %.1f) #%d",
        eventType, x, y, z, eventCounter))

    Flush()
end

-- ===== Kill detection (Osiris Died) =====

-- Timestamp of the last kill queued via the KillingBlow damage flag; used to
-- suppress the duplicate (and much later) Osiris "Died" event for the same kill.
local lastKillingBlowTime = -1000000

-- Diagnostics: counters showing where DealDamage events get filtered out.
-- Written to CinematicCombat_debug.json in the Script Extender folder.
local dbg = {
    dealDamage = 0, noHit = 0, noFlags = 0, missDodge = 0, zeroDamage = 0,
    casterFilterReject = 0, casterUuidFail = 0,
    killingBlow = 0, crit = 0, hit = 0, diedEvents = 0, diedSuppressed = 0,
    actionStart = 0, actionEnd = 0, actionRejected = 0,
    damageEventName = "?",
}
local sampleDumped = false

local function DumpDebug()
    if not DEBUG_MODE then return end
    local ok, data = pcall(Ext.Json.Stringify, dbg)
    if ok then Ext.IO.SaveFile("CinematicCombat_debug.json", data) end
end

-- ===== Event timeline tracer (analysis mode) =====
-- Records EVERY relevant game event with a millisecond timestamp into
-- CinematicCombat_trace.json. One combat round of data shows exactly which
-- event fires when (action click vs. run-up vs. impact vs. death) so camera
-- triggers can be placed precisely instead of guessing.
local traceLog = {}
local TRACE_MAX = 400

local function Trace(ev, detailA, detailB)
    if not DEBUG_MODE then return end
    table.insert(traceLog, {
        t = Ext.Utils.MonotonicTime(),
        ev = ev,
        a = detailA ~= nil and tostring(detailA) or nil,
        b = detailB ~= nil and tostring(detailB) or nil,
    })
    while #traceLog > TRACE_MAX do table.remove(traceLog, 1) end
    local ok, data = pcall(Ext.Json.Stringify, traceLog)
    if ok then Ext.IO.SaveFile("CinematicCombat_trace.json", data) end
end

local function DumpSample(e, flags)
    sampleDumped = true
    if not DEBUG_MODE then return end
    local sample = {
        flagsString = tostring(flags),
        hitTotalDamage = e.Hit and e.Hit.TotalDamageDone or "nil",
        sumsTotalDamage = (e.DamageSums and e.DamageSums.TotalDamageDone) or "nil",
        hasPosition = e.Position ~= nil,
        positionString = e.Position and tostring(e.Position) or "nil",
    }
    local okU, uuid = pcall(function() return e.Caster.Uuid.EntityUuid end)
    sample.casterUuid = okU and tostring(uuid) or ("FAIL: " .. tostring(uuid))
    local ok, data = pcall(Ext.Json.Stringify, sample)
    if ok then Ext.IO.SaveFile("CinematicCombat_sample.json", data) end
end

local function HandleDied(dead)
    Trace("Died", dead)
    dbg.diedEvents = dbg.diedEvents + 1
    if not Config.killEnabled or not dead then return end
    -- KillingBlow (fires at weapon impact) already triggered the kill cam
    if Ext.Utils.MonotonicTime() - lastKillingBlowTime < 3000 then
        dbg.diedSuppressed = dbg.diedSuppressed + 1
        DumpDebug()
        return
    end
    -- Creatures only - destroyed objects also fire death events
    if not IsCharacterEntity(dead) then return end
    if not IsRealCreature(dead) then return end
    -- Only entities we know were alive may produce a (fallback) kill cam -
    -- this keeps corpse destruction and scripted deaths silent
    if not aliveSeen[dead] then return end
    -- One kill cam per entity; pre-placed corpses never qualify
    if seenDead[dead] then return end
    if IsPreplacedCorpse(dead) then
        seenDead[dead] = true
        return
    end
    if IsPartyMember(dead) and not Config.enemyCams and Config.killOnlyEnemies then return end
    if not IsNearParty(dead, KILL_MAX_DISTANCE) then return end
    seenDead[dead] = true
    QueueEvent("kill", nil, dead)
    NotifyHudHide("kill")
    NotifyCamHold("kill", nil)   -- attacker unknown here: hold current anchor
    DumpDebug()
end

Ext.Osiris.RegisterListener("Died", 1, "after", function(dead)
    local ok, err = pcall(HandleDied, dead)
    if not ok then LogOnce(errorLog, "died", "Died handler error: " .. tostring(err)) end
end)

-- ===== Crit / normal hit detection (SE DealDamage event) =====
-- e.Hit.EffectFlags is a bitfield: .Critical, .Hit, .Miss, .Dodge etc.

local function HandleDealDamage(e)
    dbg.dealDamage = dbg.dealDamage + 1
    if dbg.dealDamage % 20 == 0 then DumpDebug() end

    if not e or not e.Hit then
        dbg.noHit = dbg.noHit + 1
        return
    end
    local flags = e.Hit.EffectFlags
    if not flags then
        dbg.noFlags = dbg.noFlags + 1
        return
    end

    Trace("DealtDamage", tostring(flags), "dmg=" .. tostring(e.Hit.TotalDamageDone))

    if not sampleDumped then DumpSample(e, flags) end

    if flags.Miss or flags.Dodge then
        dbg.missDodge = dbg.missDodge + 1
        return
    end

    -- This event only detects CRITS now. Measured: damage totals are always 0
    -- here and KillingBlow never appears - kills and hits are detected via
    -- AttackedBy (which carries the real damage) further below. The Critical
    -- flag check therefore must NOT sit behind a damage>0 gate.
    if not flags.Critical then return end
    dbg.crit = dbg.crit + 1
    if not Config.critEnabled then return end

    -- Creatures only - crits on destructible objects must not trigger cams
    local targetIsCharacter = false
    pcall(function() targetIsCharacter = e.Target.IsCharacter ~= nil end)
    if not targetIsCharacter then return end
    -- ...and neither must crits on pre-placed corpses
    local targetIsCorpse = false
    pcall(function() targetIsCorpse = e.Target.DeadByDefault ~= nil end)
    if targetIsCorpse then return end
    -- ...nor on Larian tech helper entities
    local critTargetUuid = GetEntityUuid(e.Target)
    if critTargetUuid and not IsRealCreature(critTargetUuid) then return end

    local targetUuid = GetEntityUuid(e.Target)

    if Config.onlyPlayerAttacks then
        local casterUuid = GetEntityUuid(e.Caster)
        if not casterUuid then
            -- fail-open: better an extra cinematic than none at all
            dbg.casterUuidFail = dbg.casterUuidFail + 1
        elseif not IsPartyMember(casterUuid) then
            -- enemy crit: only with enemy cinematics enabled, and only
            -- against one of OUR party members
            if not (Config.enemyCams and targetUuid and IsPartyMember(targetUuid)) then
                dbg.casterFilterReject = dbg.casterFilterReject + 1
                return
            end
        end
    end

    QueueEvent("crit", e.Position, targetUuid)
    NotifyHudHide("crit")
end

-- IMPORTANT: "DealDamage" fires BEFORE damage/flags are finalized (measured:
-- TotalDamageDone=0, flags only "Hit"). "DealtDamage" (past tense) fires after
-- application with final values including Critical and KillingBlow.
local dealDamageEvent = nil
if Ext.Events.DealtDamage then
    dealDamageEvent = Ext.Events.DealtDamage
    dbg.damageEventName = "DealtDamage"
elseif Ext.Events.EsvLuaDealtDamage then
    dealDamageEvent = Ext.Events.EsvLuaDealtDamage
    dbg.damageEventName = "EsvLuaDealtDamage"
elseif Ext.Events.DealDamage then
    dealDamageEvent = Ext.Events.DealDamage
    dbg.damageEventName = "DealDamage (fallback!)"
end
if dealDamageEvent then
    dealDamageEvent:Subscribe(function(e)
        local ok, err = pcall(HandleDealDamage, e)
        if not ok then LogOnce(errorLog, "dmg", "DealtDamage handler error: " .. tostring(err)) end
    end)
else
    Ext.Utils.Print("[BG3CinematicCombat] WARNING: no damage event available - kill/crit/hit cams disabled")
end

-- ===== Dash detection (Osiris spell events) =====

local DASH_SPELLS = {
    ["Shout_Dash"] = true,
    ["Shout_Action_Dash"] = true,
    ["Shout_Dash_CunningAction"] = true,
    ["Shout_CunningAction_Dash"] = true,
    ["Shout_StepOfTheWind_Dash"] = true,
    ["Shout_StepOfTheWind"] = true,
}

local function HandleSpell(caster, spell)
    if not caster or not spell then return end
    if Config.dashEnabled and DASH_SPELLS[tostring(spell)] then
        QueueEvent("dash", nil, caster)
    end
end

-- ===== Action cam: shoulder cam from the moment an action starts =====
-- When a party member starts ANY action (attack, spell, shot) the camera
-- zooms in behind them, follows the run-up, catches the hit, and zooms
-- back out when the action finishes (or a kill/crit cam takes over).

local lastActionStart = { caster = nil, time = -1000000 }

-- target is the attack/spell target (when the event provides one) so the
-- camera can face the enemy during the action; falls back to the caster.
local function HandleActionStart(caster, spell, evName, target)
    Trace(evName or "ActionStart", caster, spell)

    -- THE decisive "was alive" snapshot: when an action targets something,
    -- that something is checked BEFORE any damage lands. One-shot victims are
    -- provably alive here (at impact time the engine already reports them as
    -- dead and out of combat, so this is the only reliable pre-hit signal).
    -- Corpses and destructibles have no positive HP here and stay unmarked.
    local targetAlive = nil
    if target then
        -- "Alive" needs BOTH: positive HP and being a real Osiris creature.
        -- Tech entities like the brine pool can carry positive HP in a fresh
        -- save but still report IsDead=nil (not a creature) - HP alone let
        -- them trigger the action cam.
        local thp = GetHp(target)
        targetAlive = (thp ~= nil and thp > 0) and IsRealCreature(target)
        if targetAlive then
            aliveSeen[target] = true
        end
    end

    if not caster then return end
    local spellName = tostring(spell or "")

    -- Dashes have their own camera. Routed BEFORE the target filters below:
    -- a dash targets the caster and would be eaten by the friendly filter.
    if DASH_SPELLS[spellName] then
        HandleSpell(caster, spell)
        return
    end

    -- The action cam exists for ONE moment: acting AGAINST AN ENEMY - the
    -- run-up, the swing, the shot. You never know in advance whether a blow
    -- becomes a kill or a crit, so this is what keeps the approach on screen.
    -- Therefore it requires a living creature target (no objects, doors,
    -- corpses, no ground casts)...
    if not target or not targetAlive then
        if target then dbg.actionRejectedObject = (dbg.actionRejectedObject or 0) + 1 end
        return
    end
    -- ...that is NOT part of your own party (no self-buffs, no ally heals)
    if IsPartyMember(target) then
        dbg.actionRejectedFriendly = (dbg.actionRejectedFriendly or 0) + 1
        return
    end

    if not Config.actionEnabled then return end

    -- Only party member actions - otherwise every NPC turn triggers the cam
    if not IsPartyMember(caster) then
        dbg.actionRejected = dbg.actionRejected + 1
        return
    end

    -- The cast pipeline fires several events for one action - dedupe,
    -- but let an event WITH target info upgrade a targetless one
    local now = Ext.Utils.MonotonicTime()
    if lastActionStart.caster == caster and now - lastActionStart.time < 1500 then
        if not target or lastActionStart.hadTarget then return end
    end
    lastActionStart.caster = caster
    lastActionStart.time = now
    lastActionStart.hadTarget = target ~= nil

    -- Ranged/spell attacks get their own camera mode: frontal view of the
    -- shooter while aiming (you see the bow being drawn / the cast), then the
    -- kill/hit cam flips toward the victim on impact.
    local isRanged = spellName:find("^Projectile_") ~= nil

    dbg.actionStart = dbg.actionStart + 1
    QueueEvent(isRanged and "actionr" or "action", nil, target or caster)
end

local function HandleActionEnd(caster)
    Trace("CastedSpell", caster)
    if not Config.actionEnabled or not caster then return end
    if not IsPartyMember(caster) then return end
    dbg.actionEnd = dbg.actionEnd + 1
    QueueEvent("actionend", nil, caster)
end

-- Camera triggers come ONLY from the target-aware events: UsingSpellOnTarget
-- (entity targets incl. self-casts - lets us verify the target lives) and
-- UsingSpellAtPosition (ground casts, no entity to check). The target-less
-- twins UsingSpell/CastSpell fire for EVERY cast including object attacks and
-- would reintroduce the object zoom through the back door - trace only.
pcall(Ext.Osiris.RegisterListener, "CastSpell", 5, "after", function(caster, spell, _, _, _)
    pcall(Trace, "CastSpell", caster, spell)
end)
pcall(Ext.Osiris.RegisterListener, "UsingSpell", 5, "after", function(caster, spell, _, _, _)
    pcall(Trace, "UsingSpell", caster, spell)
end)
pcall(Ext.Osiris.RegisterListener, "UsingSpellOnTarget", 6, "after", function(caster, target, spell, _, _, _)
    local ok = pcall(HandleActionStart, caster, spell, "UsingSpellOnTarget", target)
    if not ok then LogOnce(errorLog, "spell2", "UsingSpellOnTarget handler error") end
end)
pcall(Ext.Osiris.RegisterListener, "UsingSpellAtPosition", 8, "after", function(caster, _, _, _, spell, _, _, _)
    local ok = pcall(HandleActionStart, caster, spell, "UsingSpellAtPosition")
    if not ok then LogOnce(errorLog, "spell3", "UsingSpellAtPosition handler error") end
end)
pcall(Ext.Osiris.RegisterListener, "CastedSpell", 5, "after", function(caster, spell, _, _, _)
    local ok = pcall(HandleActionEnd, caster)
    if not ok then LogOnce(errorLog, "casted", "CastedSpell handler error") end
end)

-- ===== Spell cast pipeline: TRACE ONLY =====
-- Lesson learned: SpellCastState is created when the skill is merely
-- SELECTED (targeting preview) - triggering the camera there zoomed on
-- skill selection. These trace entries map out which component appears at
-- which moment (selection vs. confirm vs. movement start) so a future
-- "zoom while running" trigger can be placed on real data. NO camera
-- triggers here - the Osiris events below drive the action cam.
pcall(function()
    Ext.Entity.OnCreate("SpellCastState", function(entity)
        pcall(function()
            local st = entity.SpellCastState
            local who = st and st.Caster and GetEntityUuid(st.Caster) or "?"
            local spell = ""
            pcall(function() spell = tostring(st.SpellId.Prototype) end)
            Trace("SpellCastState+", who, spell)
        end)
    end)
end)
-- ===== SpellCastMovement: TRACE ONLY =====
-- This component fires at confirm/run-start (trace-proven) and was briefly
-- used as an early camera trigger. User verdict: zooming during the run-up
-- LOOKS worse than expected - reverted to the Osiris UsingSpell trigger
-- (camera starts when the attack itself begins). Kept as trace for data.
pcall(function()
    Ext.Entity.OnCreate("SpellCastMovement", function(entity)
        pcall(function()
            local st = entity.SpellCastState
            local who = st and st.Caster and GetEntityUuid(st.Caster) or "?"
            Trace("SpellCastMovement+", who)
        end)
    end)
end)

-- ===== Impact detection via AttackedBy =====
-- Measured: DealtDamage always reports dmg=0 and never sets the KillingBlow
-- flag, while AttackedBy fires ~0.2s after impact WITH the real damage and
-- the Osiris Died event lags 1.3-1.9s behind. So the kill check happens
-- here: if the defender's HP hit zero, fire the kill cam immediately.

local function HandleAttacked(defender, attacker, damageAmount)
    if not defender then return end
    if not damageAmount or damageAmount <= 0 then return end

    -- Creatures only: destroying crates/barrels/doors must never trigger cams
    if not IsCharacterEntity(defender) then return end
    -- ...and Larian tech helpers (Brinepool & Co.) neither
    if not IsRealCreature(defender) then return end

    local attackerIsParty = attacker ~= nil and IsPartyMember(attacker)
    local defenderIsParty = IsPartyMember(defender)

    -- Attacks NOT made by the party only matter for the opt-in enemy
    -- cinematics, and only when one of OUR members is the victim
    -- (NPC-vs-NPC infighting never triggers cams).
    if not attackerIsParty then
        if not (Config.enemyCams and defenderIsParty) then return end
    end

    local hp = GetHp(defender)

    -- Remember every entity we saw alive - that is what later separates a
    -- real kill from an AoE hitting something that was already dead
    if hp ~= nil and hp > 0 then
        aliveSeen[defender] = true
    end

    -- Kill: defender is at 0 HP right after the impact
    if hp ~= nil and hp <= 0 then
        if Config.killEnabled then
            -- One kill cam per entity, and never for pre-placed corpses
            -- (hitting a corpse with an explosion re-fires death events)
            if seenDead[defender] then return end
            if IsPreplacedCorpse(defender) then
                seenDead[defender] = true
                return
            end
            -- Instant kill cam only for victims we know were alive: seen with
            -- HP > 0 this session, or actively participating in combat.
            -- Script-killed corpses lying around fulfill neither. Real
            -- out-of-combat one-shot kills still get their cam through the
            -- Died fallback below (a corpse cannot die twice).
            if not aliveSeen[defender] and not IsInCombatSafe(defender) then
                -- NOT marked as seenDead: a genuine out-of-combat one-shot
                -- kill must still reach the Died fallback below.
                dbg.killRejectedNotAlive = (dbg.killRejectedNotAlive or 0) + 1
                return
            end
            -- Party victims (downed/killed) only with enemy cinematics
            -- enabled, or when the user disabled "kill cam only for enemies"
            if defenderIsParty and not Config.enemyCams and Config.killOnlyEnemies then return end
            if not IsNearParty(defender, KILL_MAX_DISTANCE) then return end
            seenDead[defender] = true
            lastKillingBlowTime = Ext.Utils.MonotonicTime()
            dbg.killingBlow = dbg.killingBlow + 1
            QueueEvent("kill", nil, defender)
            NotifyHudHide("kill")
            NotifyCamHold("kill", attacker)
            DumpDebug()
        end
        return
    end

    -- Normal hit
    if Config.hitEnabled then
        local now = Ext.Utils.MonotonicTime()
        if now - lastHitTime >= Config.hitMinIntervalMs then
            lastHitTime = now
            dbg.hit = dbg.hit + 1
            QueueEvent("hit", nil, defender)
        end
    end
end

pcall(Ext.Osiris.RegisterListener, "AttackedBy", 7, "after", function(defender, attackerOwner, _, damageType, damageAmount, _, _)
    if DEBUG_MODE then
        -- Diagnostic detail: was the defender already flagged dead at impact
        -- time, and is it a pre-placed corpse? Decides future filter design.
        local deadFlag, corpseFlag, hpFlag, combatFlag = "?", "?", "?", "?"
        pcall(function() deadFlag = tostring(Osi.IsDead(defender)) end)
        pcall(function() corpseFlag = tostring(IsPreplacedCorpse(defender)) end)
        pcall(function() hpFlag = tostring(GetHp(defender)) end)
        pcall(function() combatFlag = tostring(Osi.IsInCombat(defender)) end)
        pcall(Trace, "AttackedBy", defender,
            tostring(damageType) .. "=" .. tostring(damageAmount) ..
            " dead=" .. deadFlag .. " corpse=" .. corpseFlag ..
            " hp=" .. hpFlag .. " combat=" .. combatFlag)
    end
    local ok, err = pcall(HandleAttacked, defender, attackerOwner, damageAmount)
    if not ok then LogOnce(errorLog, "attacked", "AttackedBy handler error: " .. tostring(err)) end
end)

-- ===== MCM integration =====
-- Reads the values configured ingame and 1) adjusts server-side behavior,
-- 2) pushes everything to the DLL via CinematicCombat_settings.json.

local MCM_SETTING_IDS = {
    "enabled", "global_intensity", "slow_motion_enabled", "pan_to_target",
    "hide_hud", "only_player_attacks", "kill_only_enemies", "enemy_cams",
    "kill_enabled", "kill_zoom", "kill_pitch", "kill_fov", "kill_hold",
    "kill_in", "kill_out", "kill_timescale",
    "crit_enabled", "crit_zoom", "crit_pitch", "crit_fov", "crit_hold",
    "crit_in", "crit_out", "crit_timescale",
    "hit_enabled", "hit_zoom", "hit_pitch", "hit_fov", "hit_hold",
    "hit_in", "hit_out", "hit_timescale", "hit_min_interval",
    "follow_enabled", "follow_zoom", "follow_pitch", "follow_fov",
    "follow_threshold", "follow_min_frames",
    "action_enabled", "action_zoom", "action_pitch", "action_fov",
    "action_in", "action_out", "action_max_duration",
    "spell_enabled", "spell_zoom", "spell_pitch", "spell_fov", "spell_hold",
    "spell_timescale",
}

local function ApplyServerConfig(values)
    if values.action_enabled ~= nil then Config.actionEnabled = values.action_enabled end
    if values.kill_enabled ~= nil then Config.killEnabled = values.kill_enabled end
    if values.crit_enabled ~= nil then Config.critEnabled = values.crit_enabled end
    if values.hit_enabled ~= nil then Config.hitEnabled = values.hit_enabled end
    if values.follow_enabled ~= nil then Config.dashEnabled = values.follow_enabled end
    if values.spell_enabled ~= nil then Config.spellEnabled = values.spell_enabled end
    if values.kill_only_enemies ~= nil then Config.killOnlyEnemies = values.kill_only_enemies end
    if values.only_player_attacks ~= nil then Config.onlyPlayerAttacks = values.only_player_attacks end
    if values.enemy_cams ~= nil then Config.enemyCams = values.enemy_cams end
    if values.hide_hud ~= nil then Config.hideHud = values.hide_hud end
    if values.hit_min_interval ~= nil then Config.hitMinIntervalMs = values.hit_min_interval * 1000 end
    if values.enabled == false then
        Config.killEnabled = false
        Config.critEnabled = false
        Config.hitEnabled = false
        Config.dashEnabled = false
        Config.spellEnabled = false
        Config.actionEnabled = false
    end
end

local function PushSettingsToDll()
    if MCM == nil then return end

    local out = {}
    local count = 0
    for _, id in ipairs(MCM_SETTING_IDS) do
        local ok, v = pcall(MCM.Get, id)
        if ok and v ~= nil then
            out[id] = v
            count = count + 1
        end
    end

    if count == 0 then return end

    ApplyServerConfig(out)

    local ok, data = pcall(Ext.Json.Stringify, out)
    if ok then
        Ext.IO.SaveFile("CinematicCombat_settings.json", data)
        Ext.Utils.Print("[BG3CinematicCombat] Pushed " .. count .. " MCM settings to native DLL")
    end
end

-- React to ingame MCM changes instantly
local function SubscribeToMcm()
    local ok = pcall(function()
        Ext.ModEvents.BG3MCM["MCM_Setting_Saved"]:Subscribe(function(payload)
            if not payload or payload.modUUID ~= ModuleUUID then return end
            PushSettingsToDll()
        end)
    end)
    if ok then
        Ext.Utils.Print("[BG3CinematicCombat] Subscribed to MCM setting changes")
    end
end

-- ===== Initialize =====

Ext.Events.SessionLoaded:Subscribe(function()
    sessionId = Ext.Utils.MonotonicTime()
    recentEvents = {}
    eventCounter = 0
    seenDead = {}
    aliveSeen = {}

    -- Accept events only after the load has settled (pre-placed corpses fire
    -- Died during loading), and overwrite any stale event file immediately
    sessionReadyAt = Ext.Utils.MonotonicTime() + 5000
    Flush()

    Ext.Utils.Print("[BG3CinematicCombat] Server EventDetector initialized (session " .. sessionId .. ")")

    if MCM ~= nil then
        Ext.Utils.Print("[BG3CinematicCombat] MCM detected - ingame configuration active")
        PushSettingsToDll()
        SubscribeToMcm()
        -- MCM may finish loading values slightly after us
        pcall(Ext.Timer.WaitFor, 2000, PushSettingsToDll)
    else
        Ext.Utils.Print("[BG3CinematicCombat] MCM not found - using TOML defaults")
    end
end)

return EventDetector
