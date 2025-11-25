--
--  upinfo.lua  --  Information about uptime and battery
--

local M = {}


local function divmod(x, y)
  if y then
    local d = x // y  -- since Lua 5.3; alternative: math.floor(x / y)
    return d, x-d*y
  else
    return nil, x
  end
end

local function file_first(p)
  local f = io.open(p)
  if not f then
    return
  end
  local u = f:read()
  f:close()
  return u:match("%S+")
end

local time_units = {
  { u = "s", m = 60 },
  { u = "m", m = 60 },
  { u = "h", m = 24 },
  { u = "d", m =  7 },
  { u = "w"         },
}


M.uptime = function(props)
  local t, v = math.floor(file_first("/proc/uptime"))
  local r = {}
  local hide = props and props.hide
  local show_zero = props and props.show_zero
  for i, e in ipairs(time_units) do
    t, v = divmod(t, e.m)
    if not hide and (show_zero or v ~= 0) then
      table.insert(r, 1, v..e.u)
    end
    if e.u == hide then
      hide = nil
    end
  end
  return table.concat(r)
end

local battery, batterystat, batterycapa

M.set_battery = function(bat)
  if bat then
    battery = "/sys/bus/acpi/drivers/battery/PNP0C0A:00/power_supply/"..bat
    batterystat, batterycapa = battery.."/status", battery.."/capacity"
  else
    battery, batterystat, batterycapa = nil, nil, nil
  end
end

M.battery = function()
  if not battery then
    return
  end
  local r = {}
  if file_first(batterystat) == "Charging" then
    table.insert(r, "^")
  end
  table.insert(r, file_first(batterycapa) or "?")
  table.insert(r, "%")
  return table.concat(r)
end


M.upinfo = function(props)
  local a = {}
  table.insert(a, M.uptime(props))
  local b = M.battery()
  if b then
    table.insert(a, b)
  end
  return table.concat(a, props and props.sep or "  ")
end


--[[
    -- Examples
    ui = require("upinfo")
    ui.set_battery(os.getenv("BAT") or "BAT")
    print(ui.upinfo())
    print(ui.upinfo({hide="s"}))
    print(ui.upinfo({hide="m"}))
    print(ui.upinfo({hide="d", show_zero=true}))
    print(ui.upinfo({show_zero=true}))
    print(ui.upinfo({sep="|", show_zero=true}))
--]]


return M

