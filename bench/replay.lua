local api = require "checkutf8"
local mode = arg[1] or "check"
local dir = arg[2] or "bench/v1"
local filter = arg[3]

assert(mode == "check" or mode == "validate" or mode == "repair",
       "usage: lua bench/replay.lua check|validate|repair [corpus-dir [group]]")

local function fnv32(s)
	local value = 2166136261
	for i = 1, #s do
		value = ((value ~ s:byte(i)) * 16777619) & 0xffffffff
	end
	return ("%08x"):format(value)
end

local input = assert(io.open(dir .. "/inputs.bin", "rb"))
local index = assert(io.open(dir .. "/index.tsv", "r"))
assert(index:read("*l") == "group\toffset\tbytes\tfirst_error\trepair_fnv32")
local ordered, groups = {}, {}
local checked = 0
for row in index:lines() do
	local group, offset, size, first, digest =
		row:match("^([^\t]+)\t(%d+)\t(%d+)\t(%d+)\t(%x+)$")
	assert(group, "invalid index row: " .. row)
	offset, size, first = tonumber(offset), tonumber(size), tonumber(first)
	assert(offset == input:seek(), "noncontiguous index")
	local s = size == 0 and "" or assert(input:read(size))
	assert(#s == size, "truncated input")
	local label = group
	if group:match("^length%-") then label = group .. "/" .. size .. "B" end
	if not filter or filter == group or filter == label then
		local valid, pos = api.validate(s)
		assert(valid == (first == 0) and (pos or 0) == first, row)
		assert(fnv32(api.repair(s)) == digest, row)
		checked = checked + 1
		local g = groups[label]
		if not g then
			g = {name = label, samples = {}, bytes = 0, invalid = 0}
			groups[label] = g
			ordered[#ordered + 1] = g
		end
		g.samples[#g.samples + 1] = s
		g.bytes = g.bytes + size
		if first > 0 then g.invalid = g.invalid + 1 end
	end
end
assert(input:read(1) == nil, "unindexed bytes")
input:close()
index:close()
assert(checked > 0, "no matching records")
if mode == "check" then
	print(("verified %d calls"):format(checked))
	return
end

local function elapsed(f, samples, passes)
	local t = os.clock()
	for pass = 1, passes do
		for i = 1, #samples do f(samples[i]) end
	end
	return os.clock() - t
end

print("group\tcalls/pass\tbytes/pass\tinvalid/pass\tns/call\tMiB/s")
for _, g in ipairs(ordered) do
	local passes = 1
	while elapsed(api[mode], g.samples, passes) < 0.01 do passes = passes * 2 end
	local rounds = {}
	for i = 1, 5 do rounds[i] = elapsed(api[mode], g.samples, passes) end
	table.sort(rounds)
	local seconds = rounds[3]
	local rate = "-"
	if g.bytes > 0 and (mode == "repair" or g.invalid == 0) then
		rate = ("%.2f"):format(g.bytes * passes / seconds / 2^20)
	end
	print(("%s\t%d\t%d\t%d\t%.2f\t%s"):format(g.name, #g.samples, g.bytes, g.invalid,
		seconds * 1e9 / (#g.samples * passes), rate))
end
