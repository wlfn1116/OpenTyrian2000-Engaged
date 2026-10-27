#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "be_bytes.h"
#include "fnv1a.h"
#include "image_png.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	const char *name;
	const Uint8 *png;
	size_t size;
	int w, h;
	bool indexed;
	Uint32 rgba_hash;
} QaPngFixture;

#include "qa_png_fixtures.h"

enum { QA_PNG_HEADER_END = 8 + 12 + 13, QA_PNG_FUZZ_ROUNDS = 400 };

static Uint32 qa_png_rgba_hash(const SDL_Surface *s)
{
	const SDL_Palette *const pal = s->format->palette;
	Uint32 hash = FNV1A32_INIT;
	for (int y = 0; y < s->h; ++y)
	{
		const Uint8 *const row = (const Uint8 *)s->pixels + (size_t)y * s->pitch;
		for (int x = 0; x < s->w; ++x)
		{
			SDL_Color c;
			if (pal != NULL)
				c = pal->colors[row[x]];
			else
				c = (SDL_Color){ row[4 * x], row[4 * x + 1], row[4 * x + 2], row[4 * x + 3] };
			const Uint8 rgba[4] = { c.r, c.g, c.b, c.a };
			hash = fnv1a32_update(hash, rgba, sizeof(rgba));
		}
	}
	return hash;
}

static bool qa_png_decodes(const QaPngFixture *f, const Uint8 *png, size_t size)
{
	SDL_Surface *const s = image_png_load(png, size);
	const bool ok = s != NULL && s->w == f->w && s->h == f->h
	             && (s->format->format == SDL_PIXELFORMAT_INDEX8) == f->indexed
	             && qa_png_rgba_hash(s) == f->rgba_hash;
	SDL_FreeSurface(s);
	return ok;
}

static bool qa_png_rejects(const Uint8 *png, size_t size)
{
	SDL_Surface *const s = image_png_load(png, size);
	const bool rejected = s == NULL;
	SDL_FreeSurface(s);
	return rejected;
}

static size_t qa_png_chunk(const Uint8 *png, size_t size, const char *type)
{
	for (size_t pos = 8; pos + 12 <= size; pos += 12 + (size_t)be_read32(png + pos))
		if (memcmp(png + pos + 4, type, 4) == 0)
			return pos;
	return 0;
}

static void qa_png_seal(Uint8 *png, size_t chunk)
{
	const Uint32 length = be_read32(png + chunk);
	be_write32(image_png_crc32(png + chunk + 4, 4 + (size_t)length), png + chunk + 8 + length);
}

static bool qa_png_fuzz(const Uint8 *png, size_t size, int w, int h, Uint32 seed)
{
	const size_t idat = qa_png_chunk(png, size, "IDAT");
	const Uint32 length = (idat != 0) ? be_read32(png + idat) : 0;
	Uint8 *const copy = malloc(size);
	unsigned rejected = 0;
	bool sane = copy != NULL && length > 0;
	for (int round = 0; sane && round < QA_PNG_FUZZ_ROUNDS; ++round)
	{
		memcpy(copy, png, size);
		const unsigned edits = 1 + qa_prng(&seed) % 4;
		for (unsigned k = 0; k < edits; ++k)
			copy[idat + 8 + qa_prng(&seed) % length] = (Uint8)qa_prng(&seed);
		qa_png_seal(copy, idat);

		SDL_Surface *const s = image_png_load(copy, size);
		if (s == NULL)
			++rejected;
		else
			sane = s->w == w && s->h == h;
		SDL_FreeSurface(s);
	}
	free(copy);
	return sane && rejected > 0;
}

static size_t qa_png_insert(Uint8 *out, const QaPngFixture *f, const char *type)
{
	memcpy(out, f->png, QA_PNG_HEADER_END);
	be_write32(4, out + QA_PNG_HEADER_END);
	memcpy(out + QA_PNG_HEADER_END + 4, type, 4);
	memcpy(out + QA_PNG_HEADER_END + 8, "qa!!", 4);
	qa_png_seal(out, QA_PNG_HEADER_END);
	memcpy(out + QA_PNG_HEADER_END + 16, f->png + QA_PNG_HEADER_END, f->size - QA_PNG_HEADER_END);
	return f->size + 16;
}

static bool qa_png_size_rejected(const QaPngFixture *f, Uint8 *work, Uint32 w, Uint32 h)
{
	memcpy(work, f->png, f->size);
	be_write32(w, work + 16);
	be_write32(h, work + 20);
	qa_png_seal(work, 8);
	return qa_png_rejects(work, f->size);
}

static bool qa_png_same_indexed(const SDL_Surface *a, const SDL_Surface *b)
{
	if (b == NULL || b->w != a->w || b->h != a->h || b->format->format != SDL_PIXELFORMAT_INDEX8)
		return false;
	for (int i = 0; i < 256; ++i)
	{
		const SDL_Color ca = a->format->palette->colors[i], cb = b->format->palette->colors[i];
		if (ca.r != cb.r || ca.g != cb.g || ca.b != cb.b || ca.a != cb.a)
			return false;
	}
	for (int y = 0; y < a->h; ++y)
		if (memcmp((const Uint8 *)a->pixels + (size_t)y * a->pitch,
		           (const Uint8 *)b->pixels + (size_t)y * b->pitch, (size_t)a->w) != 0)
			return false;
	return true;
}

static SDL_Surface *qa_png_indexed_surface(int w, int h)
{
	SDL_Surface *const s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 8, SDL_PIXELFORMAT_INDEX8);
	if (s == NULL)
		return NULL;

	SDL_Color colors[256];
	for (int i = 0; i < 256; ++i)
		colors[i] = (SDL_Color){ (Uint8)i, (Uint8)(255 - i), (Uint8)(i * 7),
		                         (Uint8)((i == 0) ? 0 : (i == 3) ? 128 : 255) };
	SDL_SetPaletteColors(s->format->palette, colors, 0, 256);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			((Uint8 *)s->pixels)[(size_t)y * s->pitch + x] = (Uint8)(x * 5 + y * 11);
	return s;
}

static bool qa_png_round_trip(int w, int h, Uint8 *buf, size_t cap, size_t *size)
{
	SDL_Surface *const src = qa_png_indexed_surface(w, h);
	*size = (src != NULL) ? image_png_save(src, buf, cap) : 0;
	SDL_Surface *const back = (*size > 0) ? image_png_load(buf, *size) : NULL;
	const bool same = src != NULL && qa_png_same_indexed(src, back);
	SDL_FreeSurface(back);
	SDL_FreeSurface(src);
	return same;
}

void qa_test_png_codec(void)
{
	qa_check(image_png_crc32("123456789", 9) == 0xcbf43926u,
	         "the PNG chunk CRC matches the standard CRC-32 check value");

	char label[112];
	for (unsigned i = 0; i < COUNTOF(qa_png_fixtures); ++i)
	{
		const QaPngFixture *const f = &qa_png_fixtures[i];
		snprintf(label, sizeof(label), "PNG fixture %s decodes to the pixels Pillow and WIC read",
		         f->name);
		qa_check(qa_png_decodes(f, f->png, f->size), label);

		bool truncated = true;
		for (size_t size = 0; size < f->size && truncated; ++size)
			truncated = qa_png_rejects(f->png, size);
		snprintf(label, sizeof(label), "every truncation of PNG fixture %s is rejected", f->name);
		qa_check(truncated, label);

		snprintf(label, sizeof(label),
		         "corrupt deflate data in PNG fixture %s is rejected or decoded in bounds",
		         f->name);
		qa_check(qa_png_fuzz(f->png, f->size, f->w, f->h, 0x5eedu + i), label);
	}
	qa_check(qa_png_rejects(qa_png_pal8_out_of_range, sizeof(qa_png_pal8_out_of_range)),
	         "a PNG pixel indexing past its palette is rejected");

	const QaPngFixture *const base = &qa_png_fixtures[0];
	Uint8 *const work = malloc(base->size + 16);
	qa_check(work != NULL, "the PNG structure checks allocate their scratch copy");
	if (work != NULL)
	{
		memcpy(work, base->png, base->size);
		work[0] ^= 1;
		qa_check(qa_png_rejects(work, base->size), "a PNG with a damaged signature is rejected");

		memcpy(work, base->png, base->size);
		work[QA_PNG_HEADER_END] ^= 1;
		qa_check(qa_png_rejects(work, base->size), "a PNG chunk with a wrong CRC is rejected");

		qa_check(qa_png_size_rejected(base, work, 0, 8) &&
		         qa_png_size_rejected(base, work, 32, 0) &&
		         qa_png_size_rejected(base, work, (1u << 14) + 1, 1) &&
		         qa_png_size_rejected(base, work, 1, (1u << 14) + 1) &&
		         qa_png_size_rejected(base, work, 4096, 2048),
		         "PNG headers with empty or oversized dimensions are rejected");

		memcpy(work, base->png, base->size);
		const size_t idat = qa_png_chunk(work, base->size, "IDAT");
		work[idat + 7 + be_read32(work + idat)] ^= 1;
		qa_png_seal(work, idat);
		qa_check(qa_png_rejects(work, base->size),
		         "a PNG whose zlib checksum disagrees is rejected");

		const size_t critical = qa_png_insert(work, base, "Qapn");
		qa_check(qa_png_rejects(work, critical),
		         "a PNG with an unknown critical chunk is rejected");
		const size_t ancillary = qa_png_insert(work, base, "qapn");
		qa_check(qa_png_decodes(base, work, ancillary),
		         "a PNG with an unknown ancillary chunk still decodes");
		free(work);
	}

	enum { SMALL_CAP = 4096, LARGE_SIDE = 300, LARGE_CAP = 128 * 1024 };
	Uint8 *const buf = malloc(LARGE_CAP);
	size_t size = 0;
	qa_check(buf != NULL && qa_png_round_trip(37, 9, buf, SMALL_CAP, &size),
	         "an indexed PNG round-trips its pixels, palette and palette alpha");
	qa_check(buf != NULL && qa_png_fuzz(buf, size, 37, 9, 0xc0deu),
	         "corrupt stored blocks in a written PNG are rejected or decoded in bounds");

	SDL_Surface *const small = qa_png_indexed_surface(37, 9);
	qa_check(buf != NULL && small != NULL && image_png_save(small, buf, size - 1) == 0 &&
	         image_png_save(small, buf, size) == size,
	         "the PNG writer refuses a buffer one byte short and fills an exact one");
	SDL_FreeSurface(small);

	SDL_Surface *const rgba = SDL_CreateRGBSurfaceWithFormat(0, 4, 4, 32, SDL_PIXELFORMAT_RGBA32);
	qa_check(buf != NULL && rgba != NULL && image_png_save(rgba, buf, SMALL_CAP) == 0,
	         "the PNG writer only takes indexed surfaces");
	SDL_FreeSurface(rgba);

	qa_check(buf != NULL && qa_png_round_trip(LARGE_SIDE, LARGE_SIDE, buf, LARGE_CAP, &size) &&
	         size > 65535,
	         "an indexed PNG larger than one stored deflate block round-trips");
	free(buf);
}
