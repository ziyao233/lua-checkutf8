// SPDX-License-Identifier: MPL-2.0
/* Include once per vector width. The compiler emits the targets in one
 * module without requiring AVX instructions in the baseline build. */
#if VECTOR_BYTES == 32
#define TARGET __attribute__((target("avx2")))
#define NAME(f) f##_avx2
#define V __m256i
#define OP(f) _mm256_##f
#define LOAD(p) _mm256_loadu_si256((const __m256i_u *)(p))
#define TABLE(p) _mm256_broadcastsi128_si256(load16((const char *)(p)))
#define HIGH(v) _mm256_movemask_epi8(v)
#define ANY(v) (!_mm256_testz_si256(v, v))
#else
#ifdef USE_VBMI
#define TARGET __attribute__((target("avx512f,avx512bw,avx512vbmi")))
#define NAME(f) f##_vbmi
#else
#define TARGET __attribute__((target("avx512f,avx512bw")))
#define NAME(f) f##_avx512
#endif
#define V __m512i
#define OP(f) _mm512_##f
#define LOAD(p) _mm512_loadu_si512(p)
#define TABLE(p) _mm512_broadcast_i32x4(load16((const char *)(p)))
#define HIGH(v) _mm512_movepi8_mask(v)
#define ANY(v) _mm512_test_epi64_mask(v, v)
#endif

TARGET static inline V
NAME(byte_errors)(V current, V prev1, V prev2, V prev3)
{
#ifdef USE_VBMI
	/* VPERMB indexes modulo 64. Repeating each 16-entry table four
	 * times makes the upper index bits irrelevant. */
	V pairs = _mm512_permutexvar_epi8(OP(srli_epi16)(prev1, 4), TABLE(prev_high))
	        & _mm512_permutexvar_epi8(prev1, TABLE(prev_low))
	        & _mm512_permutexvar_epi8(OP(srli_epi16)(current, 4), TABLE(curr_high));
#else
	V nibble = OP(set1_epi8)(15);
	V pairs = OP(shuffle_epi8)(TABLE(prev_high), OP(srli_epi16)(prev1, 4) & nibble)
	        & OP(shuffle_epi8)(TABLE(prev_low), prev1 & nibble)
	        & OP(shuffle_epi8)(TABLE(curr_high), OP(srli_epi16)(current, 4) & nibble);
#endif

	/* The sign bits indicate a three- or four-byte lead at p-2, or
	 * a four-byte lead at p-3. Both require two adjacent trailers. */
	V third = OP(subs_epu8)(prev2, OP(set1_epi8)(0x60));
	V fourth = OP(subs_epu8)(prev3, OP(set1_epi8)(0x70));
	return pairs ^ ((third | fourth) & OP(set1_epi8)((char)0x80));
}

TARGET static inline V
NAME(pair_errors)(const char *p, V current)
{
	return NAME(byte_errors)(current, LOAD(p - 1), LOAD(p - 2), LOAD(p - 3));
}


#ifdef USE_VBMI
/* Bytes before the first input byte act as ASCII. */
TARGET static inline V
first_byte_errors(V v)
{
	/* PALIGNR works within each 128-bit lane. Pair each lane with its
	 * predecessor and supply zeros for bytes before the input. */
	V previous = _mm512_alignr_epi32(v, _mm512_setzero_si512(), 12);
	V a = _mm512_alignr_epi8(v, previous, 15);
	V b = _mm512_alignr_epi8(v, previous, 14);
	V c = _mm512_alignr_epi8(v, previous, 13);
	return NAME(byte_errors)(v, a, b, c);
}

/* Return len on success, or a verified character boundary immediately
 * before the first failed pair check for scalar error reporting. */
TARGET static size_t
short_valid_prefix(const char *str, size_t len)
{
	size_t i = 0;
	V errors;
	if (len < 64) {
		V v = _mm512_maskz_loadu_epi8((UINT64_C(1) << len) - 1, str);
		if (!HIGH(v))
			return len;
		errors = first_byte_errors(v);
	} else {
		errors = first_byte_errors(LOAD(str));
		for (;;) {
			if (ANY(errors))
				goto failed_pair;
			i += 64;
			if (len - i < 64)
				break;
			errors = NAME(pair_errors)(str + i, LOAD(str + i));
		}

		/* Right-pad all four windows with zeros at the same end address.
		 * A missing continuation then fails against a virtual ASCII byte. */
		const char *p = str + i;
		uint64_t live = (UINT64_C(1) << (len - i)) - 1;
		V v = _mm512_maskz_loadu_epi8(live, p);
		V a = _mm512_maskz_loadu_epi8((live << 1) | 1, p - 1);
		V b = _mm512_maskz_loadu_epi8((live << 2) | 3, p - 2);
		V c = _mm512_maskz_loadu_epi8((live << 3) | 7, p - 3);
		errors = NAME(byte_errors)(v, a, b, c);
	}
	if (!ANY(errors))
		return len;

failed_pair:
	i += __builtin_ctzll(_mm512_test_epi8_mask(errors, errors));
	/* A bad successor can implicate its preceding lead. */
	if (i) {
		do {
			i--;
		} while (i && ((unsigned char)str[i] & 0xc0) == 0x80);
	}
	return i;
}

TARGET __attribute__((noinline)) static size_t
regular_three_byte_prefix(const char *s, size_t n, unsigned phase)
{
	/* Three vectors span 64 repetitions. Each expected byte
	 * carries the top two bits of a lead (11) or a trailer (10). */
#define LANES_0 UINT64_C(0x9249249249249249)
#define LANES_1 (LANES_0 >> 2)
#define LANES_2 (LANES_0 >> 1)
	static const uint64_t lead_lanes[3][3] = {
		{ LANES_0, LANES_2, LANES_1 },
		{ LANES_1, LANES_0, LANES_2 },
		{ LANES_2, LANES_1, LANES_0 },
	};
#undef LANES_0
#undef LANES_1
#undef LANES_2
	V trailer = OP(set1_epi8)((char)0x80);
	V phase0 = _mm512_mask_set1_epi8(trailer, lead_lanes[phase][0], (char)0xc0);
	V phase1 = _mm512_mask_set1_epi8(trailer, lead_lanes[phase][1], (char)0xc0);
	V phase2 = _mm512_mask_set1_epi8(trailer, lead_lanes[phase][2], (char)0xc0);
	/* Among bytes with prefix 11, accept E1..EC and EE..EF. */
	V invalid = _mm512_maskz_set1_epi8(UINT64_C(0xffff2001ffffffff), 0x40);
	size_t i = 0;
	while (n - i >= 192) {
		const char *p = s + i;
		V a = LOAD(p), b = LOAD(p + 64), c = LOAD(p + 128);
		/* (byte ^ phase) | (invalid_lead_bit & phase). */
		V ea = _mm512_ternarylogic_epi32(a, phase0, _mm512_permutexvar_epi8(a, invalid), 0xbc);
		V eb = _mm512_ternarylogic_epi32(b, phase1, _mm512_permutexvar_epi8(b, invalid), 0xbc);
		V ec = _mm512_ternarylogic_epi32(c, phase2, _mm512_permutexvar_epi8(c, invalid), 0xbc);
		V errors = _mm512_ternarylogic_epi32(ea, eb, ec, 0xfe);
		if (_mm512_test_epi64_mask(errors, OP(set1_epi8)((char)0xc0)))
			break;
		i += 192;
	}
	return i;
}
#endif

TARGET static size_t
NAME(utf8_validate)(const char *str, size_t len, size_t i)
{
	if (len - i >= 2048) {
		/* Certify a full vector before advancing to the next aligned
		 * address, even when it falls inside a character. */
		size_t skip = (-(uintptr_t)(str + i)) & (VECTOR_BYTES - 1);
		if (skip) {
			V errors = NAME(pair_errors)(str + i, LOAD(str + i));
			if (!ANY(errors))
				i += skip;
		}
	}
	while (len - i >= 4 * VECTOR_BYTES) {
		const char *p = str + i;
		V a = LOAD(p);
		V b = LOAD(p + VECTOR_BYTES);
		V c = LOAD(p + 2 * VECTOR_BYTES);
		V d = LOAD(p + 3 * VECTOR_BYTES);
		if (HIGH((a | b) | (c | d)) || (unsigned char)p[-1] >= 128) {
#ifdef USE_VBMI
			uint64_t sample;
			memcpy(&sample, p, sizeof(sample));
			/* Compare the top two bits of bytes three positions apart.
			 * This only predicts a run; all consumed bytes are checked. */
			if (HIGH(a & b & c) == UINT64_MAX &&
			    !((sample ^ (sample >> 24)) & UINT64_C(0xc0c0c0c0c0))) {
				unsigned phase = 0;
				if (!(sample & 0x40))
					phase = (sample & 0x4000) ? 1 : 2;
				/* Finish any character that started before this
				 * vector before scanning ahead for a long run. */
				V first = NAME(pair_errors)(p, a);
				if (ANY(first))
					break;
				size_t run = regular_three_byte_prefix(p, len - i, phase);
				if (run) {
					i += run;
					continue;
				}
			}
#endif
			V ab = NAME(pair_errors)(p, a) | NAME(pair_errors)(p + VECTOR_BYTES, b);
			V cd = NAME(pair_errors)(p + 2 * VECTOR_BYTES, c) | NAME(pair_errors)(p + 3 * VECTOR_BYTES, d);
			V errors = ab | cd;
			if (ANY(errors))
				break;  /* Locate the failing vector below. */
		}
		i += 4 * VECTOR_BYTES;
	}
	while (len - i >= VECTOR_BYTES) {
		V errors = NAME(pair_errors)(str + i, LOAD(str + i));
		if (ANY(errors))
			break;
		i += VECTOR_BYTES;
	}

	/* Recheck the last character, whose trailing bytes may be in the tail. */
	if ((unsigned char)str[i - 1] >= 128) {
		i--;
		while (((unsigned char)str[i] & 0xc0) == 0x80)
			i--;
	}
	return i;
}
#undef TARGET
#undef NAME
#undef V
#undef OP
#undef LOAD
#undef TABLE
#undef HIGH
#undef ANY
