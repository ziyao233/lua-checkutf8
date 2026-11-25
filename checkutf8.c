// SPDX-License-Identifier: MPL-2.0
/*
 * checkutf8 - Module for validating and repairing UTF-8 sequences.
 * Copyright (C) 2026 Yao Zi <me@ziyao.cc>
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <lua.h>
#include <lauxlib.h>

#ifndef __SSE2__
#define UTF8_VALIDATE utf8_validate_fast
#include "checkutf8-impl.h"

#undef UTF8_VALIDATE
#endif
#define BOUNDARY_CHECK
#define UTF8_VALIDATE utf8_validate_checked
#include "checkutf8-impl.h"

#ifdef __SSE2__
#include <emmintrin.h>

static inline __m128i
load16(const char *p)
{
	return _mm_loadu_si128((const __m128i_u *)p);
}

/* ASCII also compares above these negative signed-byte thresholds. */
static inline unsigned
signed_byte_mask_at_least(__m128i v, unsigned byte)
{
	__m128i threshold = _mm_set1_epi8((char)(byte - 1));
	return _mm_movemask_epi8(_mm_cmpgt_epi8(v, threshold));
}

/* Return the length of a validated prefix, ending on a character boundary. */
static size_t
utf8_validate_blocks(const char *str, size_t len)
{
	size_t i = 0;
	while (len - i >= 16) {
		__m128i v = load16(str + i);
		unsigned high = _mm_movemask_epi8(v);
		if (!high) {
			i += 16;
			while (len - i >= 64) {
				const char *p = str + i;
				__m128i a = _mm_or_si128(load16(p), load16(p + 16));
				__m128i b = _mm_or_si128(load16(p + 32), load16(p + 48));
				if (_mm_movemask_epi8(_mm_or_si128(a, b)))
					break;
				i += 64;
			}
			continue;
		}

		/* Each leading byte requires one, two or three trailers. */
		unsigned lead = high & signed_byte_mask_at_least(v, 0xc2);
		unsigned three = high & signed_byte_mask_at_least(v, 0xe0);
		unsigned four = high & signed_byte_mask_at_least(v, 0xf0);
		unsigned above = high & signed_byte_mask_at_least(v, 0xf5);
		unsigned bit6 = _mm_movemask_epi8(_mm_add_epi8(v, v));
		unsigned trailers = high & ~bit6;
		unsigned expected = (lead << 1) | (three << 2) | (four << 3);
		if (above || high != (trailers | lead) || (expected & 0xffff) != trailers)
			break;

		/* Check the four leads with a restricted second byte. */
		__m128i e0 = _mm_cmpeq_epi8(v, _mm_set1_epi8((char)0xe0));
		__m128i ed = _mm_cmpeq_epi8(v, _mm_set1_epi8((char)0xed));
		__m128i f0 = _mm_cmpeq_epi8(v, _mm_set1_epi8((char)0xf0));
		__m128i f4 = _mm_cmpeq_epi8(v, _mm_set1_epi8((char)0xf4));
		__m128i special3 = _mm_or_si128(e0, ed);
		__m128i special4 = _mm_or_si128(f0, f4);
		if (_mm_movemask_epi8(_mm_or_si128(special3, special4))) {
			__m128i second = _mm_srli_si128(v, 1);
			__m128i gea0 = _mm_cmpgt_epi8(second, _mm_set1_epi8((char)0x9f));
			__m128i ge90 = _mm_cmpgt_epi8(second, _mm_set1_epi8((char)0x8f));
			__m128i bad3 = _mm_or_si128(_mm_andnot_si128(gea0, e0), _mm_and_si128(gea0, ed));
			__m128i bad4 = _mm_or_si128(_mm_andnot_si128(ge90, f0), _mm_and_si128(ge90, f4));
			/* Lane 15 has no second byte in this block. */
			if (_mm_movemask_epi8(_mm_or_si128(bad3, bad4)) & 0x7fff)
				break;
		}

		/* Recheck a character straddling the block in the next pass. */
		unsigned step = 16;
		if (expected >> 16) {
			do {
				step--;
			} while (trailers & (1u << step));
		}
		i += step;
	}
	return i;
}
#endif

#if defined(__SSE2__) && (defined(__x86_64__) || defined(__i386__)) && \
    ((defined(__GNUC__) && __GNUC__ >= 8) || (defined(__clang_major__) && __clang_major__ >= 8))
#include <immintrin.h>
#define HAVE_WIDE_UTF8

/* Lookup algorithm by Keiser and Lemire (doi:10.1002/spe.2920).
 * Each nibble lookup keeps the rules that can apply at this position.
 * An intersection identifies an illegal pair, or a pair of trailers whose
 * validity depends on a lead two or three bytes earlier. */
enum {
	MISSING = 1, UNEXPECTED = 2, OVERLONG2 = 4, OVERLONG3 = 8,
	SURROGATE = 16, ABOVE_MAX = 32, LOW_FOUR = 64, TWO_TRAILERS = 128
};
#define COMMON (MISSING | UNEXPECTED | TWO_TRAILERS)
#define HIGH_F (COMMON | ABOVE_MAX | LOW_FOUR)
#define CONT (UNEXPECTED | OVERLONG2 | TWO_TRAILERS)
static const unsigned char prev_high[16] = {
	UNEXPECTED, UNEXPECTED, UNEXPECTED, UNEXPECTED,
	UNEXPECTED, UNEXPECTED, UNEXPECTED, UNEXPECTED,
	TWO_TRAILERS, TWO_TRAILERS, TWO_TRAILERS, TWO_TRAILERS,
	MISSING | OVERLONG2, MISSING, MISSING | OVERLONG3 | SURROGATE, MISSING | ABOVE_MAX | LOW_FOUR
};
static const unsigned char prev_low[16] = {
	COMMON | OVERLONG2 | OVERLONG3 | LOW_FOUR, COMMON | OVERLONG2, COMMON, COMMON,
	COMMON | ABOVE_MAX, HIGH_F, HIGH_F, HIGH_F,
	HIGH_F, HIGH_F, HIGH_F, HIGH_F,
	HIGH_F, HIGH_F | SURROGATE, HIGH_F, HIGH_F
};
static const unsigned char curr_high[16] = {
	MISSING, MISSING, MISSING, MISSING, MISSING, MISSING, MISSING, MISSING,
	CONT | OVERLONG3 | LOW_FOUR, CONT | OVERLONG3 | ABOVE_MAX,
	CONT | SURROGATE | ABOVE_MAX, CONT | SURROGATE | ABOVE_MAX,
	MISSING, MISSING, MISSING, MISSING
};
#undef HIGH_F
#undef COMMON
#undef CONT

#define VECTOR_BYTES 32
#include "checkutf8-wide.h"
#undef VECTOR_BYTES
#define VECTOR_BYTES 64
#include "checkutf8-wide.h"
#undef VECTOR_BYTES
#define VECTOR_BYTES 64
#define USE_VBMI
#include "checkutf8-wide.h"
#undef USE_VBMI
#undef VECTOR_BYTES
#endif

#if defined(HAVE_WIDE_UTF8) && !defined(__clang__)
/* Keep GCC's short-input loop together, outside the wide dispatcher. */
__attribute__((flatten, noinline))
#endif
static size_t
utf8_validate_narrow(const char *str, size_t len)
{
#ifdef __SSE2__
	size_t prefix = len >= 16 ? utf8_validate_blocks(str, len) : 0;
	return prefix + utf8_validate_checked(str + prefix, len - prefix);
#else
	size_t err = 0;

	/* Short inputs fit in one pass through the checked loop. */
	if (len >= 16) {
		err = utf8_validate_fast(str, len - 4);
		str += err;
		len -= err;
	}

	return err + utf8_validate_checked(str, len);
#endif
}

static size_t
utf8_validate(const char *str, size_t len)
{
#ifdef HAVE_WIDE_UTF8
	if (len) {
		unsigned lead = (unsigned char)str[0];
		if ((lead >= 0x80 && lead < 0xc2) || lead >= 0xf5)
			return 0;
	}
	if (len >= 5 && len < 512 && __builtin_cpu_supports("avx512vbmi") &&
	    __builtin_cpu_supports("avx512bw")) {
		size_t pos = short_valid_prefix(str, len);
		if (pos == len)
			return len;
		return pos + utf8_validate_checked(str + pos, len - pos);
	}
	if (len >= 80 && __builtin_cpu_supports("avx2")) {
		/* A checked prefix supplies lookbehind for the first wide block. */
		size_t prefix = utf8_validate_blocks(str, 16);
		if (!prefix)
			return utf8_validate_checked(str, len);
		if (len >= 512 && __builtin_cpu_supports("avx512vbmi") && __builtin_cpu_supports("avx512bw"))
			prefix = utf8_validate_vbmi(str, len, prefix);
		else if (len >= 512 && __builtin_cpu_supports("avx512bw"))
			prefix = utf8_validate_avx512(str, len, prefix);
		else
			prefix = utf8_validate_avx2(str, len, prefix);
		return prefix + utf8_validate_narrow(str + prefix, len - prefix);
	}
#endif
	return utf8_validate_narrow(str, len);
}

static inline size_t
validate_string(const char *str, size_t len)
{
	if (len <= 1)
		return len && (unsigned char)str[0] < 0x80;
	return utf8_validate(str, len);
}

static inline const char *
check_string(lua_State *l, size_t *len)
{
	const char *s = lua_tolstring(l, 1, len);
	return s ? s : luaL_checklstring(l, 1, len);
}

static int
checkutf8_validate(lua_State *l)
{
	size_t len;
	const char *s = check_string(l, &len);

	size_t err = validate_string(s, len);

	if (err == len) {
		lua_pushboolean(l, 1);
		return 1;
	}

	lua_pushboolean(l, 0);
	lua_pushinteger(l, (lua_Integer)err + 1);

	return 2;
}

/* Keep a Lua buffer reservation across nearby repairs. Commit its used
 * prefix before asking Lua for another reservation or appending the tail. */
struct repair_buffer {
	luaL_Buffer lua;
	char *end;
	size_t capacity, remaining;
};

static void
append_repaired(lua_State *l, struct repair_buffer *buff, const char *s, size_t valid,
                const char *replacement, size_t width, size_t count)
{
	if (count > 1 && width > (SIZE_MAX - valid) / count)
		luaL_error(l, "replacement string too large");
	size_t bytes = count * width;
	if (bytes > SIZE_MAX - valid)
		luaL_error(l, "replacement string too large");
	size_t needed = valid + bytes;
	if (needed > buff->remaining) {
		luaL_addsize(&buff->lua, buff->capacity - buff->remaining);
		buff->capacity = needed > LUAL_BUFFERSIZE ? needed : LUAL_BUFFERSIZE;
		buff->end = luaL_prepbuffsize(&buff->lua, buff->capacity) + buff->capacity;
		buff->remaining = buff->capacity;
	}
	char *out = buff->end - buff->remaining;
	memcpy(out, s, valid);
	out += valid;
	if (count == 1) {
		/* U+FFFD is three bytes; a fixed-size copy can be inlined. */
		if (width == 3)
			memcpy(out, replacement, 3);
		else
			memcpy(out, replacement, width);
	} else if (width == 1) {
		memset(out, (unsigned char)*replacement, bytes);
	} else if (width) {
		memcpy(out, replacement, width);
		for (size_t used = width; used < bytes; ) {
			size_t chunk = bytes - used;
			if (chunk > used)
				chunk = used;
			memcpy(out + used, out, chunk);
			used += chunk;
		}
	}
	buff->remaining -= needed;
}

static int
checkutf8_repair(lua_State *l)
{
	size_t len;
	const char *s = check_string(l, &len);

	const char *replacement = "\xef\xbf\xbd";
	size_t replacementLen = 3;
	int single = lua_gettop(l) == 1;
	if (!single)
		replacement = luaL_optlstring(l, 2, replacement, &replacementLen);

	size_t err = validate_string(s, len);
	if (err == len) {
		if (!single)
			lua_settop(l, 1);
		return 1;
	}

	/* One invalid byte produces exactly the replacement string. */
	if (len == 1) {
		if (!single && !lua_isnil(l, 2))
			lua_settop(l, 2);
		else
			lua_pushlstring(l, replacement, replacementLen);
		return 1;
	}

	struct repair_buffer buff;
	buff.capacity = LUAL_BUFFERSIZE;
	buff.remaining = buff.capacity;
	buff.end = luaL_buffinitsize(l, &buff.lua, buff.capacity) + buff.capacity;

	do {
		size_t count = 1;
		while (count < len - err) {
			unsigned char lead = (unsigned char)s[err + count];
			if (lead < 0x80 || (lead >= 0xc2 && lead <= 0xf4))
				break;
			count++;
		}
		append_repaired(l, &buff, s, err, replacement, replacementLen, count);
		s += err + count;
		len -= err + count;
		/* Nearby errors are common in damaged input. A short probe also
		 * avoids wide-vector setup for every replacement. The final three
		 * bytes of the probe can contain a truncated valid character. */
		if (len >= 16 && err < 16) {
			err = utf8_validate_checked(s, 16);
			if (err < 13)
				continue;
			err += utf8_validate(s + err, len - err);
		} else {
			err = utf8_validate(s, len);
		}
	} while (err < len);
	luaL_addsize(&buff.lua, buff.capacity - buff.remaining);
	luaL_addlstring(&buff.lua, s, len);

	luaL_pushresult(&buff.lua);

	return 1;
}

static const luaL_Reg checkutf8_funcs[] = {
	{ "validate", checkutf8_validate },
	{ "repair", checkutf8_repair },
	{ NULL, NULL },
};

int
luaopen_checkutf8(lua_State *l)
{
	luaL_newlib(l, checkutf8_funcs);

	return 1;
}
