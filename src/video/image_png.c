/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "image_png.h"

#include "be_bytes.h"
#include "opentyr.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum
{
	PNG_SIGNATURE_BYTES = 8,
	PNG_CHUNK_BYTES = 12,
	PNG_HEADER_BYTES = 13,
	PNG_STORED_MAX_BYTES = 65535,
	PNG_MAX_SIDE = 1 << 14,
	PNG_MAX_PIXELS = 1 << 22,
};

enum { PNG_GRAY = 0, PNG_RGB = 2, PNG_INDEXED = 3, PNG_GRAY_ALPHA = 4, PNG_RGBA = 6 };

enum { PNG_LITERALS = 288, PNG_DISTANCES = 30, PNG_CODE_LENGTHS = 19 };

static const Uint8 png_signature[PNG_SIGNATURE_BYTES] = {
	0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
};

static const Uint16 png_length_base[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
	163, 195, 227, 258,
};
static const Uint8 png_length_extra[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
static const Uint16 png_distance_base[PNG_DISTANCES] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049,
	3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
static const Uint8 png_distance_extra[PNG_DISTANCES] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
	13, 13,
};

typedef struct
{
	Uint32 width, height;
	Uint8 depth, color, channels;
	bool interlaced;
	SDL_Color palette[256];
	unsigned palette_size;
	bool keyed;
	Uint16 key[3];
	Uint8 *idat;
	size_t idat_size;
} PngImage;

typedef struct
{
	Uint32 x0, y0, dx, dy, w, h;
} PngPass;

typedef struct
{
	Uint16 count[16];
	Uint16 symbol[PNG_LITERALS];
} PngHuffman;

typedef struct
{
	const Uint8 *in;
	size_t in_size, in_pos;
	Uint32 bits;
	unsigned bit_count;
	Uint8 *out;
	size_t out_size, out_pos;
	bool bad;
} PngInflate;

Uint32 image_png_crc32(const void *data, size_t size)
{
	const Uint8 *const bytes = (const Uint8 *)data;
	Uint32 crc = 0xffffffffu;
	for (size_t i = 0; i < size; ++i)
	{
		crc ^= bytes[i];
		for (int bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

static Uint32 png_adler32(const Uint8 *data, size_t size)
{
	enum { ADLER_BASE = 65521, ADLER_RUN = 5552 };

	Uint32 a = 1, b = 0;
	while (size > 0)
	{
		const size_t run = (size < ADLER_RUN) ? size : ADLER_RUN;
		for (size_t i = 0; i < run; ++i)
		{
			a += data[i];
			b += a;
		}
		a %= ADLER_BASE;
		b %= ADLER_BASE;
		data += run;
		size -= run;
	}
	return (b << 16) | a;
}

static unsigned png_bits(PngInflate *z, unsigned count)
{
	while (z->bit_count < count)
	{
		if (z->in_pos == z->in_size)
		{
			z->bad = true;
			return 0;
		}
		z->bits |= (Uint32)z->in[z->in_pos++] << z->bit_count;
		z->bit_count += 8;
	}
	const unsigned value = z->bits & ((1u << count) - 1);
	z->bits >>= count;
	z->bit_count -= count;
	return value;
}

static int png_huffman_build(PngHuffman *h, const Uint8 *lengths, unsigned n)
{
	memset(h->count, 0, sizeof(h->count));
	for (unsigned i = 0; i < n; ++i)
		++h->count[lengths[i]];
	if (h->count[0] == n)
		return 0;

	int left = 1;
	for (int len = 1; len < 16; ++len)
	{
		left = 2 * left - h->count[len];
		if (left < 0)
			return left;
	}

	Uint16 offsets[16];
	offsets[1] = 0;
	for (int len = 1; len < 15; ++len)
		offsets[len + 1] = (Uint16)(offsets[len] + h->count[len]);
	for (unsigned i = 0; i < n; ++i)
		if (lengths[i] != 0)
			h->symbol[offsets[lengths[i]]++] = (Uint16)i;
	return left;
}

static bool png_huffman_complete(const PngHuffman *h, int left, unsigned n)
{
	return left == 0 || (left > 0 && (unsigned)(h->count[0] + h->count[1]) == n);
}

static int png_huffman_decode(PngInflate *z, const PngHuffman *h)
{
	int code = 0, first = 0, index = 0;
	for (int len = 1; len < 16; ++len)
	{
		code |= (int)png_bits(z, 1);
		const int count = h->count[len];
		if (code - first < count)
			return h->symbol[index + code - first];
		index += count;
		first = (first + count) << 1;
		code <<= 1;
	}
	z->bad = true;
	return -1;
}

static bool png_inflate_codes(PngInflate *z, const PngHuffman *literals,
                              const PngHuffman *distances)
{
	for (;;)
	{
		const int symbol = png_huffman_decode(z, literals);
		if (z->bad)
			return false;
		if (symbol < 256)
		{
			if (z->out_pos == z->out_size)
				return false;
			z->out[z->out_pos++] = (Uint8)symbol;
			continue;
		}
		if (symbol == 256)
			return true;

		const int length_code = symbol - 257;
		if (length_code >= 29)
			return false;
		const size_t length = png_length_base[length_code]
		                    + png_bits(z, png_length_extra[length_code]);
		const int distance_code = png_huffman_decode(z, distances);
		if (z->bad || distance_code >= PNG_DISTANCES)
			return false;
		const size_t distance = png_distance_base[distance_code]
		                      + png_bits(z, png_distance_extra[distance_code]);
		if (z->bad || distance > z->out_pos || length > z->out_size - z->out_pos)
			return false;
		for (size_t i = 0; i < length; ++i, ++z->out_pos)
			z->out[z->out_pos] = z->out[z->out_pos - distance];
	}
}

static bool png_inflate_stored(PngInflate *z)
{
	z->bits = 0;
	z->bit_count = 0;
	if (z->in_size - z->in_pos < 4)
		return false;

	const Uint8 *const header = z->in + z->in_pos;
	const unsigned length = header[0] | (unsigned)header[1] << 8;
	const unsigned check = header[2] | (unsigned)header[3] << 8;
	z->in_pos += 4;
	if (length != (~check & 0xffffu) || length > z->in_size - z->in_pos
	    || length > z->out_size - z->out_pos)
		return false;

	memcpy(z->out + z->out_pos, z->in + z->in_pos, length);
	z->in_pos += length;
	z->out_pos += length;
	return true;
}

static bool png_inflate_fixed(PngInflate *z)
{
	Uint8 lengths[PNG_LITERALS + PNG_DISTANCES];
	memset(lengths, 8, 144);
	memset(lengths + 144, 9, 112);
	memset(lengths + 256, 7, 24);
	memset(lengths + 280, 8, 8);
	memset(lengths + PNG_LITERALS, 5, PNG_DISTANCES);

	PngHuffman literals, distances;
	png_huffman_build(&literals, lengths, PNG_LITERALS);
	png_huffman_build(&distances, lengths + PNG_LITERALS, PNG_DISTANCES);
	return png_inflate_codes(z, &literals, &distances);
}

static bool png_inflate_dynamic(PngInflate *z)
{
	static const Uint8 order[PNG_CODE_LENGTHS] = {
		16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
	};

	const unsigned literal_count = png_bits(z, 5) + 257;
	const unsigned distance_count = png_bits(z, 5) + 1;
	const unsigned code_count = png_bits(z, 4) + 4;
	if (z->bad || literal_count > 286 || distance_count > PNG_DISTANCES)
		return false;

	Uint8 lengths[286 + PNG_DISTANCES];
	for (unsigned i = 0; i < PNG_CODE_LENGTHS; ++i)
		lengths[order[i]] = (i < code_count) ? (Uint8)png_bits(z, 3) : 0;
	PngHuffman literals, distances;
	if (z->bad || png_huffman_build(&literals, lengths, PNG_CODE_LENGTHS) != 0)
		return false;

	const unsigned total = literal_count + distance_count;
	for (unsigned i = 0; i < total; )
	{
		const int symbol = png_huffman_decode(z, &literals);
		if (z->bad)
			return false;
		if (symbol < 16)
		{
			lengths[i++] = (Uint8)symbol;
			continue;
		}

		Uint8 repeated = 0;
		unsigned run;
		if (symbol == 16)
		{
			if (i == 0)
				return false;
			repeated = lengths[i - 1];
			run = 3 + png_bits(z, 2);
		}
		else
		{
			run = (symbol == 17) ? 3 + png_bits(z, 3) : 11 + png_bits(z, 7);
		}
		if (z->bad || run > total - i)
			return false;
		memset(lengths + i, repeated, run);
		i += run;
	}
	if (lengths[256] == 0)
		return false;

	const int literal_left = png_huffman_build(&literals, lengths, literal_count);
	const int distance_left = png_huffman_build(&distances, lengths + literal_count,
	                                            distance_count);
	return png_huffman_complete(&literals, literal_left, literal_count)
	    && png_huffman_complete(&distances, distance_left, distance_count)
	    && png_inflate_codes(z, &literals, &distances);
}

static bool png_inflate(const Uint8 *in, size_t in_size, Uint8 *out, size_t out_size)
{
	if (in_size < 6 || (in[0] & 0x0f) != 8 || (in[0] >> 4) > 7 || (in[1] & 0x20) != 0
	    || ((unsigned)in[0] << 8 | in[1]) % 31 != 0)
		return false;

	PngInflate z = { .in = in, .in_size = in_size, .in_pos = 2, .out = out, .out_size = out_size };
	for (bool last = false; !last; )
	{
		last = png_bits(&z, 1) != 0;
		const unsigned type = png_bits(&z, 2);
		if (z.bad)
			return false;

		bool decoded;
		switch (type)
		{
		case 0:
			decoded = png_inflate_stored(&z);
			break;
		case 1:
			decoded = png_inflate_fixed(&z);
			break;
		case 2:
			decoded = png_inflate_dynamic(&z);
			break;
		default:
			decoded = false;
			break;
		}
		if (!decoded)
			return false;
	}

	z.bits = 0;
	z.bit_count = 0;
	return z.out_pos == out_size && z.in_size - z.in_pos >= 4
	    && be_read32(z.in + z.in_pos) == png_adler32(out, out_size);
}

static bool png_read_header(PngImage *img, const Uint8 *data, Uint32 length)
{
	static const struct { Uint8 color, channels; Uint32 depths; } formats[] = {
		{ PNG_GRAY,       1, 1u << 1 | 1u << 2 | 1u << 4 | 1u << 8 | 1u << 16 },
		{ PNG_RGB,        3, 1u << 8 | 1u << 16 },
		{ PNG_INDEXED,    1, 1u << 1 | 1u << 2 | 1u << 4 | 1u << 8 },
		{ PNG_GRAY_ALPHA, 2, 1u << 8 | 1u << 16 },
		{ PNG_RGBA,       4, 1u << 8 | 1u << 16 },
	};

	if (length != PNG_HEADER_BYTES)
		return false;
	img->width = be_read32(data);
	img->height = be_read32(data + 4);
	img->depth = data[8];
	img->color = data[9];
	img->interlaced = data[12] == 1;
	if (img->width == 0 || img->height == 0
	    || img->width > PNG_MAX_SIDE || img->height > PNG_MAX_SIDE
	    || img->width * img->height > PNG_MAX_PIXELS || img->depth > 16
	    || data[10] != 0 || data[11] != 0 || data[12] > 1)
		return false;

	for (unsigned i = 0; i < COUNTOF(formats); ++i)
	{
		if (formats[i].color == img->color)
		{
			img->channels = formats[i].channels;
			return ((formats[i].depths >> img->depth) & 1u) != 0;
		}
	}
	return false;
}

static bool png_read_palette(PngImage *img, const Uint8 *data, Uint32 length)
{
	if (img->color != PNG_INDEXED)
		return true;
	if (img->palette_size != 0 || length == 0 || length % 3 != 0
	    || length / 3 > COUNTOF(img->palette))
		return false;

	img->palette_size = length / 3;
	for (unsigned i = 0; i < img->palette_size; ++i)
	{
		const Uint8 *const rgb = data + 3 * i;
		img->palette[i] = (SDL_Color){ rgb[0], rgb[1], rgb[2], SDL_ALPHA_OPAQUE };
	}
	return true;
}

static void png_read_transparency(PngImage *img, const Uint8 *data, Uint32 length)
{
	if (img->color == PNG_INDEXED)
	{
		if (length <= img->palette_size)
			for (Uint32 i = 0; i < length; ++i)
				img->palette[i].a = data[i];
	}
	else if ((img->color == PNG_GRAY || img->color == PNG_RGB) && length == 2u * img->channels)
	{
		img->keyed = true;
		for (unsigned c = 0; c < img->channels; ++c)
			img->key[c] = be_read16(data + 2 * c);
	}
}

static bool png_read_chunks(PngImage *img, const Uint8 *file, size_t size)
{
	if (size < PNG_SIGNATURE_BYTES || memcmp(file, png_signature, PNG_SIGNATURE_BYTES) != 0)
		return false;
	img->idat = malloc(size);
	if (img->idat == NULL)
		return false;

	for (size_t pos = PNG_SIGNATURE_BYTES; ; )
	{
		if (size - pos < PNG_CHUNK_BYTES)
			return false;
		const Uint32 length = be_read32(file + pos);
		const Uint8 *const type = file + pos + 4, *const data = type + 4;
		if (length > size - pos - PNG_CHUNK_BYTES
		    || image_png_crc32(type, 4 + (size_t)length) != be_read32(data + length))
			return false;

		const bool header = memcmp(type, "IHDR", 4) == 0;
		if (header != (pos == PNG_SIGNATURE_BYTES))
			return false;
		pos += PNG_CHUNK_BYTES + (size_t)length;

		if (header)
		{
			if (!png_read_header(img, data, length))
				return false;
		}
		else if (memcmp(type, "PLTE", 4) == 0)
		{
			if (!png_read_palette(img, data, length))
				return false;
		}
		else if (memcmp(type, "tRNS", 4) == 0)
		{
			png_read_transparency(img, data, length);
		}
		else if (memcmp(type, "IDAT", 4) == 0)
		{
			memcpy(img->idat + img->idat_size, data, length);
			img->idat_size += length;
		}
		else if (memcmp(type, "IEND", 4) == 0)
		{
			return img->idat_size > 0 && (img->color != PNG_INDEXED || img->palette_size > 0);
		}
		else if ((type[0] & 0x20) == 0)
		{
			return false;
		}
	}
}

static unsigned png_passes(const PngImage *img, PngPass *passes)
{
	static const Uint8 adam7[7][4] = {
		{ 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 },
		{ 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 },
	};

	if (!img->interlaced)
	{
		passes[0] = (PngPass){ 0, 0, 1, 1, img->width, img->height };
		return 1;
	}
	for (unsigned i = 0; i < COUNTOF(adam7); ++i)
	{
		const Uint32 x0 = adam7[i][0], y0 = adam7[i][1], dx = adam7[i][2], dy = adam7[i][3];
		passes[i] = (PngPass){
			x0, y0, dx, dy,
			(img->width > x0) ? (img->width - x0 + dx - 1) / dx : 0,
			(img->height > y0) ? (img->height - y0 + dy - 1) / dy : 0,
		};
	}
	return COUNTOF(adam7);
}

static size_t png_row_bytes(const PngImage *img, Uint32 width)
{
	return ((size_t)width * img->channels * img->depth + 7) / 8;
}

static size_t png_raw_size(const PngImage *img)
{
	PngPass passes[7];
	const unsigned count = png_passes(img, passes);
	size_t size = 0;
	for (unsigned i = 0; i < count; ++i)
		if (passes[i].w > 0)
			size += passes[i].h * (1 + png_row_bytes(img, passes[i].w));
	return size;
}

static int png_predict(Uint8 filter, int left, int up, int corner)
{
	switch (filter)
	{
	case 1:
		return left;
	case 2:
		return up;
	case 3:
		return (left + up) / 2;
	case 4:
	{
		const int estimate = left + up - corner;
		const int to_left = abs(estimate - left);
		const int to_up = abs(estimate - up);
		const int to_corner = abs(estimate - corner);
		if (to_left <= to_up && to_left <= to_corner)
			return left;
		return (to_up <= to_corner) ? up : corner;
	}
	default:
		return 0;
	}
}

static bool png_unfilter(Uint8 filter, Uint8 *row, const Uint8 *prior, size_t size, size_t stride)
{
	if (filter > 4)
		return false;

	for (size_t i = 0; i < size; ++i)
	{
		const int left = (i >= stride) ? row[i - stride] : 0;
		const int up = (prior != NULL) ? prior[i] : 0;
		const int corner = (i >= stride && prior != NULL) ? prior[i - stride] : 0;
		row[i] = (Uint8)(row[i] + png_predict(filter, left, up, corner));
	}
	return true;
}

static unsigned png_sample(const Uint8 *row, size_t index, unsigned depth)
{
	if (depth == 16)
		return be_read16(row + 2 * index);
	if (depth == 8)
		return row[index];

	const size_t bit = index * depth;
	return (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
}

static Uint8 png_scale(unsigned sample, unsigned depth)
{
	return (Uint8)((depth == 16) ? sample >> 8 : sample * 255 / ((1u << depth) - 1));
}

static bool png_matches_key(const PngImage *img, const unsigned *sample)
{
	if (!img->keyed)
		return false;
	for (unsigned c = 0; c < img->channels; ++c)
		if (sample[c] != img->key[c])
			return false;
	return true;
}

static bool png_put_row(const PngImage *img, SDL_Surface *surface, const PngPass *pass, Uint32 y,
                        const Uint8 *row)
{
	const bool gray = img->color == PNG_GRAY || img->color == PNG_GRAY_ALPHA;
	const bool alpha = img->color == PNG_GRAY_ALPHA || img->color == PNG_RGBA;
	const size_t line_y = pass->y0 + y * pass->dy;
	Uint8 *const line = (Uint8 *)surface->pixels + line_y * (size_t)surface->pitch;

	for (Uint32 x = 0; x < pass->w; ++x)
	{
		const size_t at = pass->x0 + x * pass->dx, first = (size_t)x * img->channels;
		if (img->color == PNG_INDEXED)
		{
			const unsigned index = png_sample(row, first, img->depth);
			if (index >= img->palette_size)
				return false;
			line[at] = (Uint8)index;
			continue;
		}

		unsigned sample[4] = { 0 };
		for (unsigned c = 0; c < img->channels; ++c)
			sample[c] = png_sample(row, first + c, img->depth);
		Uint8 *const rgba = line + 4 * at;
		rgba[0] = png_scale(sample[0], img->depth);
		rgba[1] = png_scale(sample[gray ? 0 : 1], img->depth);
		rgba[2] = png_scale(sample[gray ? 0 : 2], img->depth);
		if (alpha)
			rgba[3] = png_scale(sample[img->channels - 1], img->depth);
		else
			rgba[3] = png_matches_key(img, sample) ? SDL_ALPHA_TRANSPARENT : SDL_ALPHA_OPAQUE;
	}
	return true;
}

static SDL_Surface *png_decode(const PngImage *img, Uint8 *raw)
{
	const bool indexed = img->color == PNG_INDEXED;
	const Uint32 format = indexed ? SDL_PIXELFORMAT_INDEX8 : SDL_PIXELFORMAT_RGBA32;
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, (int)img->width, (int)img->height,
	                                                      SDL_BITSPERPIXEL(format), format);
	bool ok = surface != NULL;
	if (ok && indexed)
		ok = SDL_SetPaletteColors(surface->format->palette, img->palette, 0,
		                          (int)img->palette_size) == 0;

	PngPass passes[7];
	const unsigned count = png_passes(img, passes);
	const size_t stride = png_row_bytes(img, 1);
	for (unsigned p = 0; ok && p < count; ++p)
	{
		if (passes[p].w == 0)
			continue;

		const size_t row_bytes = png_row_bytes(img, passes[p].w);
		const Uint8 *prior = NULL;
		for (Uint32 y = 0; ok && y < passes[p].h; ++y)
		{
			Uint8 *const row = raw + 1;
			ok = png_unfilter(raw[0], row, prior, row_bytes, stride)
			  && png_put_row(img, surface, &passes[p], y, row);
			prior = row;
			raw += 1 + row_bytes;
		}
	}

	if (!ok)
	{
		SDL_FreeSurface(surface);
		return NULL;
	}
	return surface;
}

SDL_Surface *image_png_load(const void *data, size_t size)
{
	PngImage img;
	memset(&img, 0, sizeof(img));

	SDL_Surface *surface = NULL;
	if (png_read_chunks(&img, (const Uint8 *)data, size))
	{
		const size_t raw_size = png_raw_size(&img);
		Uint8 *const raw = malloc(raw_size);
		if (raw != NULL && png_inflate(img.idat, img.idat_size, raw, raw_size))
			surface = png_decode(&img, raw);
		free(raw);
	}
	free(img.idat);
	return surface;
}

static Uint8 *png_chunk_begin(Uint8 *out, const char *type, Uint32 length)
{
	be_write32(length, out);
	memcpy(out + 4, type, 4);
	return out + 8;
}

static Uint8 *png_chunk_end(const Uint8 *chunk, Uint8 *end)
{
	be_write32(image_png_crc32(chunk + 4, (size_t)(end - chunk) - 4), end);
	return end + 4;
}

size_t image_png_save(const SDL_Surface *surface, void *out, size_t capacity)
{
	const SDL_Palette *const palette = (surface != NULL) ? surface->format->palette : NULL;
	if (palette == NULL || palette->ncolors <= 0
	    || surface->format->format != SDL_PIXELFORMAT_INDEX8
	    || surface->w <= 0 || surface->h <= 0
	    || surface->w > PNG_MAX_SIDE || surface->h > PNG_MAX_SIDE
	    || surface->w * surface->h > PNG_MAX_PIXELS)
		return 0;

	const unsigned colors = (unsigned)SDL_min(palette->ncolors, 256);
	unsigned transparent = 0;
	for (unsigned i = 0; i < colors; ++i)
		if (palette->colors[i].a != SDL_ALPHA_OPAQUE)
			transparent = i + 1;

	const size_t width = (size_t)surface->w, height = (size_t)surface->h;
	const size_t raw_size = height * (1 + width);
	const size_t blocks = (raw_size + PNG_STORED_MAX_BYTES - 1) / PNG_STORED_MAX_BYTES;
	const size_t zlib_size = 2 + raw_size + 5 * blocks + 4;
	const size_t total = PNG_SIGNATURE_BYTES + 4 * PNG_CHUNK_BYTES + PNG_HEADER_BYTES + 3 * colors
	                   + zlib_size + ((transparent > 0) ? PNG_CHUNK_BYTES + transparent : 0);
	Uint8 *const raw = (total <= capacity) ? malloc(raw_size) : NULL;
	if (raw == NULL)
		return 0;

	for (size_t y = 0; y < height; ++y)
	{
		Uint8 *const row = raw + y * (1 + width);
		row[0] = 0;
		memcpy(row + 1, (const Uint8 *)surface->pixels + y * (size_t)surface->pitch, width);
	}

	Uint8 *p = out;
	memcpy(p, png_signature, PNG_SIGNATURE_BYTES);
	p += PNG_SIGNATURE_BYTES;

	Uint8 *chunk = p;
	p = png_chunk_begin(p, "IHDR", PNG_HEADER_BYTES);
	be_write32((Uint32)width, p);
	be_write32((Uint32)height, p + 4);
	p[8] = 8;
	p[9] = PNG_INDEXED;
	memset(p + 10, 0, 3);
	p = png_chunk_end(chunk, p + PNG_HEADER_BYTES);

	chunk = p;
	p = png_chunk_begin(p, "PLTE", 3 * colors);
	for (unsigned i = 0; i < colors; ++i, p += 3)
	{
		p[0] = palette->colors[i].r;
		p[1] = palette->colors[i].g;
		p[2] = palette->colors[i].b;
	}
	p = png_chunk_end(chunk, p);

	if (transparent > 0)
	{
		chunk = p;
		p = png_chunk_begin(p, "tRNS", transparent);
		for (unsigned i = 0; i < transparent; ++i)
			*p++ = palette->colors[i].a;
		p = png_chunk_end(chunk, p);
	}

	chunk = p;
	p = png_chunk_begin(p, "IDAT", (Uint32)zlib_size);
	*p++ = 0x78;
	*p++ = 0x01;
	for (size_t at = 0; at < raw_size; )
	{
		const size_t length = SDL_min(raw_size - at, (size_t)PNG_STORED_MAX_BYTES);
		*p++ = (at + length == raw_size) ? 1 : 0;
		*p++ = (Uint8)length;
		*p++ = (Uint8)(length >> 8);
		*p++ = (Uint8)~length;
		*p++ = (Uint8)(~length >> 8);
		memcpy(p, raw + at, length);
		p += length;
		at += length;
	}
	be_write32(png_adler32(raw, raw_size), p);
	p = png_chunk_end(chunk, p + 4);

	chunk = p;
	p = png_chunk_end(chunk, png_chunk_begin(p, "IEND", 0));

	free(raw);
	return (size_t)(p - (Uint8 *)out);
}
