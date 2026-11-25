-- Exercise UTF-8 sequences on both sides of the vector block boundaries.
local checkutf8 = require "checkutf8"

local function check(input)
	local count, first = utf8.len(input)
	local valid, offset = checkutf8.validate(input)
	assert(valid == (count ~= nil) and offset == first)

	local parts, start = {}, 1
	while first do
		parts[#parts + 1] = input:sub(start, first - 1) .. "?"
		start = first + 1
		count, first = utf8.len(input, start)
	end
	assert(checkutf8.repair(input, "?") == table.concat(parts) .. input:sub(start))
end

for prefix = 0, 80 do
	local pad = ("a"):rep(prefix)
	for _, lead in ipairs({0x80, 0xc0, 0xc1, 0xc2, 0xdf, 0xe0, 0xe1, 0xed,
	                       0xef, 0xf0, 0xf1, 0xf4, 0xf5, 0xff}) do
		for _, second in ipairs({0, 0x7f, 0x80, 0x8f, 0x90, 0x9f, 0xa0, 0xbf, 0xc0}) do
			local seq = string.char(lead, second, 0x80, 0xbf)
			for n = 1, #seq do
				local s = pad .. seq:sub(1, n)
				check(s)
				check(s .. "中🙂" .. pad)
			end
		end
	end
end

-- Move a sequence through every lane of a four-vector group. Check both
-- a truncated final character and an error followed by another full group.
for offset = 0, 255 do
	local pad = ("a中"):rep(128) .. ("x"):rep(offset)
	for _, seq in ipairs({
		"\xc0\x80", "\xc1\xbf", "\xc2\x80", "\xe0\x9f\xbf", "\xe0\xa0\x80",
		"\xed\x9f\xbf", "\xed\xa0\x80", "\xf0\x8f\xbf\xbf", "\xf0\x90\x80\x80",
		"\xf4\x8f\xbf\xbf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff",
	}) do
		for n = 1, #seq do
			local s = pad .. seq:sub(1, n)
			check(s)
			check(s .. ("a"):rep(256) .. "\xff" .. pad)
		end
	end
end

-- Exercise long regular triples, including the character crossing a vector
-- boundary, and transitions to ASCII, restricted leads and four-byte text.
local run = ("中文。"):rep(256)
for prefix = 0, 63 do
	local pad = ("x"):rep(prefix)
	for _, middle in ipairs({
		"\0", "🙂", "\xe0\xa0\x80", "\xed\x9f\xbf",
		"\xe0\x9f\xbf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xff",
	}) do
		check(pad .. run .. middle .. run)
	end
	for tail = 1, 3 do
		check(pad .. run:sub(1, #run - tail))
	end
end

-- Exercise all short-block tail masks, including an incomplete character
-- in the last active lane and a bad second byte after a full vector.
for len = 0, 513 do
	local ch = "中🙂"
	local s = ch:rep(len // #ch) .. ("a"):rep(len % #ch)
	check(s)
	for _, ending in ipairs({"\xc2", "\xe0\xa0", "\xf4\x8f\xbf", "\xed\xa0\x80"}) do
		if len >= #ending then check(s:sub(1, len - #ending) .. ending) end
	end
end

-- Consecutive invalid bytes may grow the output across Lua buffer boundaries.
local repair = checkutf8.repair
for _, count in ipairs({1, 2, 3, 31, 1025, 8193}) do
	local input = "中\0" .. ("\x80"):rep(count) .. "🙂\xffend"
	for _, replacement in ipairs({"", "?", "\0", "x\0y", "a\0中", ("ab"):rep(128)}) do
		assert(repair(input, replacement) ==
		       "中\0" .. replacement:rep(count) .. "🙂" .. replacement .. "end")
	end
	assert(repair(input) == repair(input, "�"))
end
assert(repair(123) == "123")
assert(repair("\xff", 123) == "123")
assert(repair("ok", nil, "extra") == "ok")
assert(not pcall(repair, "ok", {}))

-- Refill the repair buffer across many separate errors, then append a long
-- valid span and a multibyte tail. Also exercise an oversized single write.
local span = ("文"):rep(4000)
for _, n in ipairs({255, 1024, 8191, 8192, 8193}) do
	local input = ("a\xff"):rep(n) .. span .. "\xff终"
	for _, repl in ipairs({"", "?", "x\0y"}) do
		assert(repair(input, repl) == ("a" .. repl):rep(n) .. span .. repl .. "终")
	end
end
local large = ("\0a"):rep(8193)
assert(repair("\xff好\xff", large) == large .. "好" .. large)

-- A standalone non-ASCII byte always produces exactly one replacement.
local custom = ("A\0文"):rep(512)
for byte = 0, 255 do
	local s = string.char(byte)
	local valid, pos = checkutf8.validate(s)
	assert(valid == (byte < 128))
	assert(pos == (byte >= 128 and 1 or nil))
	assert(repair(s) == (valid and s or "�"))
	assert(repair(s, nil, false) == (valid and s or "�"))
	assert(repair(s, "", false) == (valid and s or ""))
	assert(repair(s, custom, false) == (valid and s or custom))
	assert(repair(s, 42, false) == (valid and s or "42"))
	assert(not pcall(repair, s, {}))
end
