#include "custom_weapon.h"

#include "episodes.h"
#include "shots.h"
#include "sprite.h"

#include "SDL.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	JE_word     sg;
	JE_byte     attack, del;
	JE_shortint sx, sy, bx, by;
} AutoScaleBullet;

enum
{
	AUTO_SCALE_TILES = 8,
	AUTO_SCALE_UNITS = 96,
	AUTO_SCALE_FRAMES = 16,
	AUTO_SCALE_VOLLEY = 8,
	AUTO_SCALE_VOLLEY_WIDE = 16,
	AUTO_SCALE_VOLLEY_CHAIN = 12,
	AUTO_SCALE_REACH_PX = 40,
	AUTO_SCALE_GAP_PX = 8,
	AUTO_SCALE_BEAM_GAP_PX = 10,
};

enum
{
	AUTO_SCALE_CORE,
	AUTO_SCALE_RING,
	AUTO_SCALE_SOLO,
	AUTO_SCALE_FLANK,
};

typedef struct
{
	AutoScaleBullet tile[AUTO_SCALE_TILES];
	int count, frame, kind, side, ring, grown, pair, origin;
	double weight;
} AutoScaleUnit;

typedef struct
{
	AutoScaleUnit unit[AUTO_SCALE_UNITS];
	int count, frames, cx2, csx, fwd, stride, axis2;
	double reach;
} AutoScaleShape;

typedef struct
{
	int left, right, frame, side, ring, stepX, stepY, room;
	double lean, turn;
} AutoScaleSeed;

enum
{
	AUTO_SCALE_HELD = 384,
	AUTO_SCALE_MATCH = 24,
	AUTO_SCALE_LADDER = 24,
};

typedef struct
{
	int origin, tile;
	AutoScaleBullet b;
} AutoScaleHeld;

static const double AUTO_SCALE_DEGREE = 0.017453292519943295;

static AutoScaleShape autoScaleAnchor, autoScaleStock, autoScaleLevel, autoScaleTrial;
static AutoScaleHeld autoScaleHeld[AUTO_SCALE_HELD];
static int autoScaleHeldCount;
static int autoScaleStyle = CUSTOM_SPREAD_ARROW;

int customWeaponSpread = CUSTOM_SPREAD_AUTO;
int customWeaponLastSpread = CUSTOM_SPREAD_AUTO;

static const char *const customSpreadNames[CUSTOM_SPREAD_COUNT] =
{
	"Auto", "Stock", "Arrow", "Fan", "Wall", "Crescent", "Converge", "Wave", "Twin", "Burst",
	"Nova", "Lob", "Spiral", "Weave"
};

const char *customWeaponSpreadName(int spread)
{
	return customSpreadNames[clampi(spread, 0, CUSTOM_SPREAD_COUNT - 1)];
}

static AutoScaleBullet autoScaleGet(const JE_WeaponType *w, int i)
{
	const AutoScaleBullet b = { w->sg[i], w->attack[i], w->del[i], w->sx[i], w->sy[i], w->bx[i], w->by[i] };
	return b;
}

static void autoScalePut(JE_WeaponType *w, int i, const AutoScaleBullet *b)
{
	w->sg[i] = b->sg;
	w->attack[i] = b->attack;
	w->del[i] = b->del;
	w->sx[i] = b->sx;
	w->sy[i] = b->sy;
	w->bx[i] = b->bx;
	w->by[i] = b->by;
}

static int autoScaleRound(double v)
{
	return (int)((v < 0.0) ? v - 0.5 : v + 0.5);
}

static bool autoScaleSteerable(const AutoScaleBullet *b)
{
	return abs(b->sx) <= 30 && abs(b->sy) <= 30 && (b->sx != 0 || b->sy != 0);
}

static bool autoScaleAimable(const AutoScaleBullet *b)
{
	return autoScaleSteerable(b) || (b->sx > 100 && b->sy != 0 && abs(b->sy) <= 30);
}

static int autoScaleDriftX(const AutoScaleBullet *b, int csx)
{
	return (b->sx > 100) ? csx : b->sx;
}

static int autoScaleTileDamage(JE_byte attack)
{
	if (attack >= 1 && attack <= 98)
		return attack;
	return (attack >= 251) ? attack - 250 : 0;
}

static JE_byte autoScaleWithDamage(JE_byte attack, int damage)
{
	if (attack >= 1 && attack <= 98)
		return (JE_byte)clampi(damage, 1, 98);
	if (attack >= 251)
		return (JE_byte)(250 + clampi(damage, 1, 5));
	return attack;
}

static int autoScaleChainDamage(JE_byte attack, int depth)
{
	if (attack < 101 || attack > 249)
		return 0;
	int dmg = 1;
	if (depth > 0 && attack - 100 <= WEAP_NUM)
	{
		const JE_WeaponType *const w = &weapons[attack - 100];
		for (int i = 0; i < clampi(w->multi, 1, CUSTOM_BULLETS_MAX); ++i)
			dmg +=
			    autoScaleTileDamage(w->attack[i]) + autoScaleChainDamage(w->attack[i], depth - 1);
	}
	return dmg;
}

static double autoScaleWeaponDps(const JE_WeaponType *w)
{
	const int multi = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	const int slots = MAX(multi, clampi(w->max, 1, CUSTOM_BULLETS_MAX));
	int dmg = 0;
	for (int i = 0; i < slots; ++i)
		dmg += autoScaleTileDamage(w->attack[i]) + autoScaleChainDamage(w->attack[i], 2);
	return (double)dmg * multi / slots * 35.0 / (w->shotrepeat + 1);
}

static bool autoScaleSpriteRef(JE_word sg, Sprite2_array *sheet, unsigned int *index,
                               JE_word *offset)
{
	if (sg == 0 || sg >= 60000)
		return false;
	const JE_word high = (sg > 1000) ? (JE_word)(sg - sg % 1000) : 0;
	const JE_word base = (JE_word)(sg - high);
	*sheet = *shot_sprite_sheet(base, index);
	*offset = (JE_word)(high + ((base > 500) ? 500 : 0));
	return *index >= 1;
}

static int autoScaleSpriteArea(Sprite2_array sheet, unsigned int index)
{
	int x0, y0, x1, y1;
	if (!sprite2_ink_bounds(sheet, index, &x0, &y0, &x1, &y1))
		return 0;
	return (x1 - x0 + 1) * (y1 - y0 + 1);
}

static int autoScaleProfile(Sprite2_array sheet, unsigned int index)
{
	int x0, y0, x1, y1;
	if (!sprite2_ink_bounds(sheet, index, &x0, &y0, &x1, &y1))
		return -1;
	return (y0 == 0) | (y1 >= 13) << 1 | (x0 == 0) << 2 | (x1 >= 11) << 3;
}

static JE_word autoScaleMirrorSprite(JE_word sg)
{
	static const int nearby[] = { 1, -1, 19, -19, 2, -2, 38, -38, 18, -18, 20, -20 };
	Sprite2_array sheet;
	unsigned int index;
	JE_word offset;
	if (!autoScaleSpriteRef(sg, &sheet, &index, &offset) ||
	    autoScaleSpriteArea(sheet, index) == 0 || sprite2_hflip_equal(sheet, index, index))
		return sg;

	for (size_t i = 0; i < COUNTOF(nearby); ++i)
	{
		const int candidate = (int)index + nearby[i];
		if (candidate >= 1 && sprite2_hflip_equal(sheet, index, (unsigned int)candidate))
			return (JE_word)(offset + candidate);
	}
	return sg;
}

static JE_word autoScaleFamilyStep(JE_word sg, int step)
{
	Sprite2_array sheet;
	unsigned int index;
	JE_word offset;
	if (step == 0 || !autoScaleSpriteRef(sg, &sheet, &index, &offset) ||
	    autoScaleSpriteArea(sheet, index) == 0 || autoScaleMirrorSprite(sg) != sg)
		return sg;

	const Uint8 bank = sprite2_dominant_bank(sheet, index);
	const int profile = autoScaleProfile(sheet, index);
	unsigned int lo = index, hi = index;
	while (lo > 1 && index - lo < 5 && autoScaleSpriteArea(sheet, lo - 1) > 0 &&
	       sprite2_dominant_bank(sheet, lo - 1) == bank)
		--lo;
	while (hi - index < 5 && autoScaleSpriteArea(sheet, hi + 1) > 0 &&
	       sprite2_dominant_bank(sheet, hi + 1) == bank)
		++hi;

	unsigned int pick = index;
	int pickArea = autoScaleSpriteArea(sheet, index);
	for (int s = 0; s < abs(step); ++s)
	{
		unsigned int next = pick;
		int nextArea = -1;
		for (unsigned int i = lo; i <= hi; ++i)
		{
			const int area = autoScaleSpriteArea(sheet, i);
			if ((step > 0) ? area <= pickArea : area >= pickArea)
				continue;
			if (nextArea >= 0 && ((step > 0) ? area >= nextArea : area <= nextArea))
				continue;
			if ((unsigned int)autoScaleMirrorSprite((JE_word)(offset + i)) != offset + i ||
			    autoScaleProfile(sheet, i) != profile)
				continue;
			next = i;
			nextArea = area;
		}
		if (nextArea < 0)
			break;
		pick = next;
		pickArea = nextArea;
	}
	return (JE_word)(offset + pick);
}

static int autoScaleOverhang(JE_word sg)
{
	if (sg <= 60000 || (unsigned int)(sg - 60001) >= COUNTOF(sprite_table[0].sprite))
		return 0;
	const int width = get_sprite_width(OPTION_SHAPES, sg - 60001);
	return (width > 0) ? width - 12 : 0;
}

static int autoScaleSway2(JE_byte circlesize)
{
	const int size = (circlesize > 19) ? circlesize % 20 : circlesize;
	return shot_circle_sway2(size >> 1, 1, size);
}

static int autoScaleUnitX2(const AutoScaleUnit *u)
{
	int lo = u->tile[0].bx, hi = lo + autoScaleOverhang(u->tile[0].sg);
	for (int i = 1; i < u->count; ++i)
	{
		lo = MIN(lo, u->tile[i].bx);
		hi = MAX(hi, u->tile[i].bx + autoScaleOverhang(u->tile[i].sg));
	}
	return lo + hi;
}

static int autoScaleUnitY2(const AutoScaleUnit *u)
{
	int lo = u->tile[0].by, hi = lo;
	for (int i = 1; i < u->count; ++i)
	{
		lo = MIN(lo, u->tile[i].by);
		hi = MAX(hi, u->tile[i].by);
	}
	return lo + hi;
}

static int autoScaleUnitWidth(const AutoScaleUnit *u)
{
	int lo = 127, hi = -128;
	for (int i = 0; i < u->count; ++i)
	{
		const AutoScaleBullet *const t = &u->tile[i];
		Sprite2_array sheet;
		unsigned int index;
		JE_word offset;
		int x0 = 0, y0, x1 = 11, y1;
		if (t->sg > 60000 && (unsigned int)(t->sg - 60001) < COUNTOF(sprite_table[0].sprite))
			x1 = MAX(get_sprite_width(OPTION_SHAPES, t->sg - 60001), 1) - 1;
		else if (autoScaleSpriteRef(t->sg, &sheet, &index, &offset) &&
		         !sprite2_ink_bounds(sheet, index, &x0, &y0, &x1, &y1))
		{
			x0 = 0;
			x1 = 11;
		}
		lo = MIN(lo, t->bx + x0);
		hi = MAX(hi, t->bx + x1);
	}
	return hi - lo + 1;
}

static void autoScaleUnitShift(AutoScaleUnit *u, int dx, int dy)
{
	for (int i = 0; i < u->count; ++i)
	{
		u->tile[i].bx = (JE_shortint)clampi(u->tile[i].bx + dx, -128, 127);
		u->tile[i].by = (JE_shortint)clampi(u->tile[i].by + dy, -128, 127);
	}
}

static bool autoScaleUnitWide(const AutoScaleUnit *u)
{
	const int x2 = autoScaleUnitX2(u);
	bool wide = false;
	for (int i = 0; i < u->count; ++i)
	{
		const int rel = 2 * u->tile[i].bx + autoScaleOverhang(u->tile[i].sg) - x2;
		bool mirrored = false;
		for (int j = 0; j < u->count && !mirrored; ++j)
			mirrored = 2 * u->tile[j].bx + autoScaleOverhang(u->tile[j].sg) - x2 == -rel &&
			           u->tile[j].by == u->tile[i].by;
		if (!mirrored)
			return false;
		wide |= rel != 0;
	}
	return wide;
}

static bool autoScaleUnitPlain(const AutoScaleUnit *u)
{
	if (autoScaleUnitWide(u))
		return true;
	for (int i = 0; i < u->count; ++i)
		if (autoScaleMirrorSprite(u->tile[i].sg) != u->tile[i].sg)
			return false;
	return true;
}

static bool autoScaleUnitColumn(const AutoScaleUnit *u)
{
	if (u->count < 2)
		return false;
	for (int i = 0; i < u->count; ++i)
		if (abs(u->tile[i].bx - u->tile[0].bx) > 1 || u->tile[i].sg == 0 || u->tile[i].sg >= 60000)
			return false;
	return true;
}

static void autoScaleUnitMirror(const AutoScaleUnit *u, int cx2, int csx, AutoScaleUnit *out)
{
	*out = *u;
	bool translate = autoScaleUnitWide(u);
	for (int i = 0; i < u->count && !translate; ++i)
		translate = u->tile[i].sg > 60000;
	const int shift = cx2 - autoScaleUnitX2(u);
	for (int i = 0; i < out->count; ++i)
	{
		AutoScaleBullet *const t = &out->tile[i];
		if (translate)
		{
			t->bx = (JE_shortint)clampi(t->bx + shift, -128, 127);
		}
		else
		{
			t->bx = (JE_shortint)clampi(cx2 - t->bx - autoScaleOverhang(t->sg), -128, 127);
			t->sg = autoScaleMirrorSprite(t->sg);
		}
		if (abs(t->sx) <= 100)
			t->sx = (JE_shortint)clampi(2 * csx - t->sx, -100, 100);
	}
	out->side = -u->side;
}

static void autoScaleTileOrder(const AutoScaleUnit *u, int *order)
{
	for (int i = 0; i < u->count; ++i)
	{
		int j = i;
		for (; j > 0; --j)
		{
			const AutoScaleBullet *const prev = &u->tile[order[j - 1]];
			if (prev->by < u->tile[i].by ||
			    (prev->by == u->tile[i].by && prev->bx <= u->tile[i].bx))
				break;
			order[j] = order[j - 1];
		}
		order[j] = i;
	}
}

static void autoScaleAdoptSprites(AutoScaleUnit *mirrored, const AutoScaleUnit *actual)
{
	int mine[AUTO_SCALE_TILES], theirs[AUTO_SCALE_TILES];
	autoScaleTileOrder(mirrored, mine);
	autoScaleTileOrder(actual, theirs);
	for (int i = 0; i < mirrored->count && i < actual->count; ++i)
		mirrored->tile[mine[i]].sg = actual->tile[theirs[i]].sg;
}

static double autoScaleLean(const AutoScaleUnit *u, int side, int csx, int fwd)
{
	const AutoScaleBullet *const t = &u->tile[0];
	if (!autoScaleAimable(t))
		return 0.0;
	const int sx = autoScaleDriftX(t, csx);
	return atan2((double)((side < 0) ? csx - sx : sx - csx), (double)(fwd * t->sy));
}

static void autoScaleTurn(AutoScaleUnit *u, int side, int csx, int fwd, double lean)
{
	if (!autoScaleAimable(&u->tile[0]))
		return;
	for (int i = 0; i < u->count; ++i)
	{
		AutoScaleBullet *const t = &u->tile[i];
		const double vx = autoScaleDriftX(t, csx) - csx, vy = t->sy;
		const double speed = sqrt(vx * vx + vy * vy);
		const int sx = clampi(autoScaleRound(csx + side * speed * sin(lean)), -30, 30);
		const int sy = clampi(autoScaleRound(fwd * speed * cos(lean)), -30, 30);
		if (sx == 0 && sy == 0)
			continue;
		t->sx = (JE_shortint)sx;
		t->sy = (JE_shortint)sy;
	}
}

static void autoScaleSpin(AutoScaleUnit *u, double angle)
{
	if (!autoScaleAimable(&u->tile[0]))
		return;
	for (int i = 0; i < u->count; ++i)
	{
		AutoScaleBullet *const t = &u->tile[i];
		const int drift = autoScaleDriftX(t, 0);
		const double speed = sqrt((double)drift * drift + (double)t->sy * t->sy);
		const double heading = atan2((double)drift, (double)t->sy) + angle;
		const int sx = clampi(autoScaleRound(speed * sin(heading)), -30, 30);
		const int sy = clampi(autoScaleRound(speed * cos(heading)), -30, 30);
		if (sx == 0 && sy == 0)
			continue;
		t->sx = (JE_shortint)sx;
		t->sy = (JE_shortint)sy;
	}
}

static bool autoScaleAbuts(JE_word left, JE_word right)
{
	Sprite2_array sheet;
	unsigned int index;
	JE_word offset;
	if (!autoScaleSpriteRef(left, &sheet, &index, &offset))
		return false;
	const int leftEdges = autoScaleProfile(sheet, index);
	if (!autoScaleSpriteRef(right, &sheet, &index, &offset))
		return false;
	const int rightEdges = autoScaleProfile(sheet, index);
	return leftEdges >= 0 && rightEdges >= 0 && (leftEdges & 8) != 0 && (rightEdges & 4) != 0;
}

static bool autoScaleTilesJoin(const AutoScaleBullet *a, const AutoScaleBullet *b, int stride)
{
	if (a->sx > 100 && a->sx == b->sx && abs(a->sy - b->sy) == 1 && a->del == b->del &&
	    a->sg != 0 && b->sg != 0 && a->sg < 60000 && b->sg < 60000 && abs(a->bx - b->bx) <= 1 &&
	    (a->by == b->by || abs(a->by - b->by) == 14))
		return true;
	if (a->sx != b->sx || a->sy != b->sy || a->del != b->del || a->sg == 0 || b->sg == 0)
		return false;
	const bool optionShape = a->sg >= 60000;
	if (optionShape != (b->sg >= 60000) || (!optionShape && a->sg / 500 != b->sg / 500))
		return false;

	const AutoScaleBullet *const top = (a->by <= b->by) ? a : b, *const low = (top == a) ? b : a;
	const AutoScaleBullet *const left = (a->bx <= b->bx) ? a : b;
	const AutoScaleBullet *const right = (left == a) ? b : a;
	const int dx = right->bx - left->bx, dy = low->by - top->by;
	if (optionShape)
		return dx <= 1 && dy >= 8 && dy <= 32 && low->sg == top->sg + 1;
	if (dx <= 1 && dy == 14)
	{
		const int ds = (int)low->sg - (int)top->sg;
		return ds == 0 || (ds >= 1 && ds <= 2 * stride) || ds == 19 || ds == 38;
	}
	if (dx == 12 && dy <= 1)
	{
		const int ds = (int)right->sg - (int)left->sg;
		return ((ds >= 1 && ds <= 2 * stride) || ds == 19) && autoScaleAbuts(left->sg, right->sg);
	}
	return false;
}

static int autoScaleGcd(int a, int b)
{
	while (b != 0)
	{
		const int t = a % b;
		a = b;
		b = t;
	}
	return a;
}

static int autoScaleRoot(const int *parent, int i)
{
	while (parent[i] != i)
		i = parent[i];
	return i;
}

static int autoScaleUnitLateral(const AutoScaleUnit *u, int cx2, int csx)
{
	const int sx = u->tile[0].sx;
	return abs(autoScaleUnitX2(u) - cx2) + ((abs(sx) <= 100) ? 8 * abs(sx - csx) : 0);
}

static int autoScalePairCost(const AutoScaleUnit *l, const AutoScaleUnit *r, int cx2, int csx)
{
	const AutoScaleBullet *const a = &l->tile[0], *const b = &r->tile[0];
	const int sxWant = (abs(a->sx) <= 100) ? 2 * csx - a->sx : a->sx;
	return abs(autoScaleUnitX2(r) - (2 * cx2 - autoScaleUnitX2(l))) +
	       abs(autoScaleUnitY2(r) - autoScaleUnitY2(l)) + 8 * abs(b->sx - sxWant) +
	       8 * abs(b->sy - a->sy);
}

static int autoScaleFrameTiles(const AutoScaleShape *s, int f)
{
	int n = 0;
	for (int i = 0; i < s->count; ++i)
		if (s->unit[i].frame == f)
			n += s->unit[i].count;
	return n;
}

static int autoScaleFrameRings(const AutoScaleShape *s, int f)
{
	int r = 0;
	for (int i = 0; i < s->count; ++i)
		if (s->unit[i].frame == f && s->unit[i].kind == AUTO_SCALE_RING)
			r = MAX(r, s->unit[i].ring);
	return r;
}

static int autoScaleFindRing(const AutoScaleShape *s, int f, int ring, int side)
{
	for (int i = 0; i < s->count; ++i)
	{
		const AutoScaleUnit *const u = &s->unit[i];
		if (u->frame == f && u->kind == AUTO_SCALE_RING && u->ring == ring && u->side == side)
			return i;
	}
	return -1;
}

static int autoScaleMate(const AutoScaleShape *s, int i)
{
	for (int j = 0; j < s->count; ++j)
		if (j != i && s->unit[j].pair == s->unit[i].pair && s->unit[i].pair >= 0)
			return j;
	return -1;
}

static int autoScaleCoreUnit(const AutoScaleShape *s, int f)
{
	int best = -1;
	for (int i = 0; i < s->count; ++i)
	{
		const AutoScaleUnit *const u = &s->unit[i];
		if (u->frame != f || u->kind != AUTO_SCALE_CORE)
			continue;
		if (best < 0 || u->count > s->unit[best].count ||
		    (u->count == s->unit[best].count &&
		     abs(autoScaleUnitY2(u)) < abs(autoScaleUnitY2(&s->unit[best]))))
			best = i;
	}
	return best;
}

static int autoScaleShapeSide(const AutoScaleShape *s)
{
	int side = 0;
	for (int i = 0; i < s->count; ++i)
	{
		const int x2 = autoScaleUnitX2(&s->unit[i]);
		const int clear = MAX(16, autoScaleUnitWidth(&s->unit[i]));
		const int spawn = (x2 <= -clear) ? -1 : (x2 >= clear) ? 1 : 0;
		const int seen = (x2 - s->axis2 <= -clear) ? -1 : (x2 - s->axis2 >= clear) ? 1 : 0;
		if (spawn == 0 || seen != spawn || (side != 0 && spawn != side))
			return 0;
		side = spawn;
	}
	return side;
}

static void autoScaleCentre(AutoScaleShape *s)
{
	const int dx = (s->cx2 - s->axis2) / 2;
	if (dx == 0 || autoScaleShapeSide(s) != 0)
		return;
	for (int i = 0; i < s->count; ++i)
		autoScaleUnitShift(&s->unit[i], -dx, 0);
	s->cx2 -= 2 * dx;
}

static bool autoScaleAddUnit(AutoScaleShape *s, const AutoScaleUnit *u)
{
	if (s->count == AUTO_SCALE_UNITS)
		return false;
	s->unit[s->count++] = *u;
	return true;
}

static bool autoScaleDecompose(const JE_WeaponType *w, AutoScaleShape *shape)
{
	static AutoScaleBullet bullet[CUSTOM_BULLETS_MAX];
	static AutoScaleUnit pool[AUTO_SCALE_UNITS];
	static int parent[CUSTOM_BULLETS_MAX];
	int lateral[AUTO_SCALE_UNITS], order[AUTO_SCALE_UNITS], partner[AUTO_SCALE_UNITS];
	bool taken[AUTO_SCALE_UNITS];
	const int multi = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	const int max = clampi(w->max, 1, CUSTOM_BULLETS_MAX);
	const int period = (max > multi) ? max : multi;
	const int cycle = (max > multi) ? multi / autoScaleGcd(multi, max) * max : multi;
	if (cycle > CUSTOM_BULLETS_MAX || cycle / multi > AUTO_SCALE_FRAMES)
		return false;

	shape->frames = cycle / multi;
	shape->stride = clampi(w->weapani + 1, 1, 8);
	shape->count = 0;
	int units = 0;
	for (int f = 0; f < shape->frames; ++f)
	{
		for (int i = 0; i < multi; ++i)
		{
			bullet[i] = autoScaleGet(w, (f * multi + i) % period);
			parent[i] = i;
		}
		for (int i = 0; i < multi; ++i)
			for (int j = i + 1; j < multi; ++j)
				if (autoScaleTilesJoin(&bullet[i], &bullet[j], shape->stride))
				{
					const int ri = autoScaleRoot(parent, i), rj = autoScaleRoot(parent, j);
					parent[MAX(ri, rj)] = MIN(ri, rj);
				}
		for (int i = 0; i < multi; ++i)
		{
			if (parent[i] != i)
				continue;
			int members = 0;
			for (int j = i; j < multi; ++j)
				members += autoScaleRoot(parent, j) == i;
			bool opened = false;
			for (int j = i; j < multi; ++j)
			{
				if (autoScaleRoot(parent, j) != i)
					continue;
				if (!opened || members > AUTO_SCALE_TILES)
				{
					if (units == AUTO_SCALE_UNITS)
						return false;
					const AutoScaleUnit fresh = {.frame = f, .ring = 0, .pair = -1, .weight = 1.0};
					pool[units++] = fresh;
					opened = true;
				}
				AutoScaleUnit *const u = &pool[units - 1];
				u->tile[u->count++] = bullet[j];
			}
		}
	}

	int x2Sum = 0, sxSum = 0, sxCount = 0, sySum = 0;
	for (int u = 0; u < units; ++u)
	{
		const AutoScaleBullet *const t = &pool[u].tile[0];
		x2Sum += autoScaleUnitX2(&pool[u]);
		if (abs(t->sx) <= 100)
		{
			sxSum += t->sx;
			++sxCount;
		}
		if (abs(t->sy) <= 30)
			sySum += t->sy;
	}
	shape->cx2 = autoScaleRound((double)x2Sum / units);
	shape->axis2 = -autoScaleSway2(w->circlesize);
	shape->csx = (sxCount > 0) ? autoScaleRound((double)sxSum / sxCount) : 0;
	shape->fwd = (sySum < 0) ? -1 : 1;
	shape->reach = 0.0;

	for (int u = 0; u < units; ++u)
	{
		const AutoScaleBullet *const t = &pool[u].tile[0];
		const int dx2 = autoScaleUnitX2(&pool[u]) - shape->cx2;
		const int dv = (abs(t->sx) <= 100) ? t->sx - shape->csx : 0;
		lateral[u] = dx2 + 8 * dv;
		if (lateral[u] == 0)
			lateral[u] = (dv != 0) ? dv : ((dx2 < 0) ? -1 : 1);
		taken[u] = abs(dx2) <= 2 && dv == 0;
		partner[u] = -1;
		if (taken[u])
		{
			pool[u].kind = AUTO_SCALE_CORE;
			pool[u].side = 0;
			if (!autoScaleAddUnit(shape, &pool[u]))
				return false;
		}
	}

	int pairs = 0;
	for (int u = 0; u < units; ++u)
	{
		if (taken[u] || lateral[u] > 0)
			continue;
		int best = -1, bestCost = 25;
		for (int v = 0; v < units; ++v)
		{
			if (taken[v] || lateral[v] < 0 || pool[v].frame != pool[u].frame ||
			    pool[v].count != pool[u].count)
				continue;
			const int cost = autoScalePairCost(&pool[u], &pool[v], shape->cx2, shape->csx);
			if (cost < bestCost)
			{
				best = v;
				bestCost = cost;
			}
		}
		if (best >= 0)
		{
			taken[u] = taken[best] = true;
			partner[u] = best;
			order[pairs++] = u;
		}
	}
	for (int u = 0; u < units; ++u)
		if (!taken[u] && shape->frames == 1)
		{
			taken[u] = true;
			order[pairs++] = u;
		}

	for (int k = 1; k < pairs; ++k)
	{
		const int key = order[k];
		int j = k;
		for (; j > 0 && (pool[order[j - 1]].frame > pool[key].frame ||
		                 (pool[order[j - 1]].frame == pool[key].frame &&
		                  abs(lateral[order[j - 1]]) > abs(lateral[key])));
		     --j)
			order[j] = order[j - 1];
		order[j] = key;
	}

	for (int k = 0; k < pairs; ++k)
	{
		const int u = order[k];
		const int f = pool[u].frame;
		AutoScaleUnit left = pool[u], right;
		if (lateral[u] > 0)
		{
			right = pool[u];
			right.side = 1;
			autoScaleUnitMirror(&right, shape->cx2, shape->csx, &left);
		}
		else
		{
			left.side = -1;
			autoScaleUnitMirror(&left, shape->cx2, shape->csx, &right);
			if (partner[u] >= 0)
				autoScaleAdoptSprites(&right, &pool[partner[u]]);
		}
		left.kind = right.kind = AUTO_SCALE_RING;
		left.side = -1;
		right.side = 1;
		left.ring = right.ring = autoScaleFrameRings(shape, f) + 1;
		left.pair = right.pair = k;
		if (!autoScaleAddUnit(shape, &left) || !autoScaleAddUnit(shape, &right))
			return false;
		shape->reach = MAX(shape->reach, autoScaleLean(&left, -1, shape->csx, shape->fwd));
	}

	for (int u = 0; u < units; ++u)
	{
		if (taken[u])
			continue;
		AutoScaleUnit solo = pool[u];
		solo.kind = AUTO_SCALE_SOLO;
		solo.side = (lateral[u] < 0) ? -1 : 1;
		if (!autoScaleAddUnit(shape, &solo))
			return false;
		shape->reach = MAX(shape->reach, autoScaleLean(&solo, solo.side, shape->csx, shape->fwd));
	}

	autoScaleCentre(shape);
	int tiles = 0;
	for (int f = 0; f < shape->frames; ++f)
		tiles += autoScaleFrameTiles(shape, f);
	for (int i = 0; i < shape->count; ++i)
		shape->unit[i].origin = i;
	return tiles <= CUSTOM_BULLETS_MAX;
}

static double autoScaleFrameLateral(const AutoScaleShape *s, int f)
{
	int best = -1;
	for (int i = 0; i < s->count; ++i)
	{
		const AutoScaleUnit *const u = &s->unit[i];
		if (u->frame != f)
			continue;
		const int lat = (u->kind == AUTO_SCALE_CORE) ? 0 : autoScaleUnitLateral(u, s->cx2, s->csx);
		if (best < 0 || lat < best)
			best = lat;
	}
	return MAX(best, 0);
}

static bool autoScaleCollapse(AutoScaleUnit *u, const AutoScaleShape *s)
{
	if (!autoScaleUnitPlain(u))
		return false;
	for (int i = 0; i < u->count; ++i)
		if (abs(u->tile[i].sx) <= 100 && u->tile[i].sx != s->csx &&
		    !autoScaleSteerable(&u->tile[i]))
			return false;

	autoScaleUnitShift(u, (s->cx2 - autoScaleUnitX2(u)) / 2, 0);
	if (autoScaleSteerable(&u->tile[0]))
	{
		const double vx = u->tile[0].sx - s->csx, vy = u->tile[0].sy;
		const int sy = clampi(autoScaleRound(sqrt(vx * vx + vy * vy)), 1, 30) * s->fwd;
		for (int i = 0; i < u->count; ++i)
		{
			u->tile[i].sx = (JE_shortint)s->csx;
			u->tile[i].sy = (JE_shortint)sy;
		}
	}
	u->kind = AUTO_SCALE_CORE;
	u->side = 0;
	u->ring = 0;
	u->pair = -1;
	return true;
}

static bool autoScaleFramesEven(const AutoScaleShape *s)
{
	if (s->frames < 1)
		return false;
	const int tiles = autoScaleFrameTiles(s, 0);
	for (int f = 1; f < s->frames; ++f)
		if (autoScaleFrameTiles(s, f) != tiles)
			return false;
	return tiles >= 1;
}

static bool autoScaleFramesSame(const AutoScaleShape *s, int f, int g)
{
	static AutoScaleBullet a[CUSTOM_BULLETS_MAX], b[CUSTOM_BULLETS_MAX];
	int na = 0, nb = 0;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
		{
			if (s->unit[i].frame == f && na < CUSTOM_BULLETS_MAX)
				a[na++] = s->unit[i].tile[k];
			else if (s->unit[i].frame == g && nb < CUSTOM_BULLETS_MAX)
				b[nb++] = s->unit[i].tile[k];
		}
	if (na != nb)
		return false;
	for (int i = 0; i < na; ++i)
	{
		bool found = false;
		for (int j = 0; j < nb && !found; ++j)
			found = memcmp(&a[i], &b[j], sizeof(a[i])) == 0;
		if (!found)
			return false;
	}
	return true;
}

static bool autoScaleShrink(const AutoScaleShape *src, double share, AutoScaleShape *out)
{
	double frameLat[AUTO_SCALE_FRAMES], levels[AUTO_SCALE_FRAMES];
	int levelCount = 0;
	for (int f = 0; f < src->frames; ++f)
	{
		frameLat[f] = autoScaleFrameLateral(src, f);
		int j = levelCount;
		bool seen = false;
		for (int k = 0; k < levelCount; ++k)
			seen |= fabs(levels[k] - frameLat[f]) < 0.5;
		if (seen)
			continue;
		for (; j > 0 && levels[j - 1] > frameLat[f]; --j)
			levels[j] = levels[j - 1];
		levels[j] = frameLat[f];
		++levelCount;
	}
	const double cut = levels[(int)((levelCount - 1) * share + 0.5)];

	*out = *src;
	out->count = 0;
	out->frames = 0;
	for (int f = 0; f < src->frames; ++f)
	{
		if (frameLat[f] > cut + 0.5)
			continue;
		const int nf = out->frames++;
		const int rings = autoScaleFrameRings(src, f);
		const int keep = (int)(rings * share + 0.5);
		bool anchored = false;
		for (int i = 0; i < src->count; ++i)
		{
			AutoScaleUnit u = src->unit[i];
			if (u.frame != f || (u.kind == AUTO_SCALE_RING && u.ring > keep))
				continue;
			anchored |= u.kind != AUTO_SCALE_RING || keep > 0;
			u.frame = nf;
			if (!autoScaleAddUnit(out, &u))
				return false;
		}
		if (anchored || rings == 0)
			continue;

		AutoScaleUnit core = src->unit[autoScaleFindRing(src, f, 1, -1)];
		if (autoScaleCollapse(&core, src))
		{
			core.frame = nf;
			if (!autoScaleAddUnit(out, &core))
				return false;
			continue;
		}
		for (int side = -1; side <= 1; side += 2)
		{
			AutoScaleUnit u = src->unit[autoScaleFindRing(src, f, 1, side)];
			u.frame = nf;
			if (!autoScaleAddUnit(out, &u))
				return false;
		}
	}

	if (share <= 0.0 && cut > 0.5)
	{
		for (int f = 0; f < out->frames; ++f)
		{
			int only = -1, units = 0;
			for (int i = 0; i < out->count; ++i)
				if (out->unit[i].frame == f)
				{
					only = i;
					++units;
				}
			if (units == 1 && out->unit[only].kind == AUTO_SCALE_SOLO)
				autoScaleCollapse(&out->unit[only], out);
		}
	}

	bool same = out->frames > 1;
	for (int f = 1; f < out->frames && same; ++f)
		same = autoScaleFramesSame(out, 0, f);
	if (same)
	{
		int n = 0;
		for (int i = 0; i < out->count; ++i)
			if (out->unit[i].frame == 0)
				out->unit[n++] = out->unit[i];
		out->count = n;
		out->frames = 1;
	}
	return autoScaleFramesEven(out);
}

static bool autoScaleCoincides(const AutoScaleShape *s, const AutoScaleUnit *u)
{
	for (int i = 0; i < s->count; ++i)
	{
		const AutoScaleBullet *const a = &s->unit[i].tile[0], *const b = &u->tile[0];
		if (s->unit[i].frame == u->frame && a->bx == b->bx && a->by == b->by && a->sx == b->sx &&
		    a->sy == b->sy)
			return true;
	}
	return false;
}

static int autoScaleGap(const AutoScaleUnit *u)
{
	const int gap = (u->tile[0].sx > 100) ? AUTO_SCALE_BEAM_GAP_PX : AUTO_SCALE_GAP_PX;
	return MAX(autoScaleUnitWidth(u) + 3, gap);
}

static void autoScaleStyleSeed(AutoScaleSeed *s, const AutoScaleUnit *t, int fwd)
{
	const int gap = autoScaleGap(t);
	const bool steer = autoScaleAimable(&t->tile[0]);
	switch (autoScaleStyle)
	{
	case CUSTOM_SPREAD_FAN:
		s->stepX = gap;
		s->stepY = 0;
		s->turn = steer ? MAX(s->turn, 9.0 * AUTO_SCALE_DEGREE) : 0.0;
		break;
	case CUSTOM_SPREAD_WALL:
		s->stepX = MIN(MAX(s->stepX, gap + 4), MAX(gap, s->room / 2));
		s->stepY = 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_CRESCENT:
		s->stepX = MAX(s->stepX, gap);
		s->stepY = steer ? -3 * fwd : 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_CONVERGE:
		s->stepX = MIN(MAX(s->stepX, gap + 3), MAX(gap, s->room / 2));
		s->stepY = steer ? 2 * fwd : 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_WAVE:
		s->stepX = MAX(s->stepX, gap);
		s->stepY = steer ? 5 * fwd : 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_TWIN:
		s->stepX = MAX(s->stepX, gap);
		s->stepY = steer ? 2 * fwd : 0;
		s->turn = 0.0;
		break;
	case CUSTOM_SPREAD_BURST:
		s->stepX = 0;
		s->stepY = 0;
		s->turn = steer ? 14.0 * AUTO_SCALE_DEGREE : 0.0;
		break;
	case CUSTOM_SPREAD_NOVA:
		s->stepX = 0;
		s->stepY = 0;
		s->turn = steer ? 24.0 * AUTO_SCALE_DEGREE : 0.0;
		break;
	case CUSTOM_SPREAD_LOB:
		s->stepX = MAX(s->stepX, gap);
		s->stepY = 0;
		s->turn = 0.0;
		break;
	default:
		if (s->turn < 3.0 * AUTO_SCALE_DEGREE)
		{
			s->stepX = MAX(s->stepX, gap);
			if (s->stepY == 0 && steer)
				s->stepY = 3 * fwd;
		}
		break;
	}
}

static int autoScaleSeeds(const AutoScaleShape *src, AutoScaleSeed *seed)
{
	int n = 0;
	for (int f = 0; f < src->frames; ++f)
	{
		const int rings = autoScaleFrameRings(src, f);
		const int core = autoScaleCoreUnit(src, f);
		if (rings > 0)
		{
			AutoScaleSeed *const s = &seed[n++];
			s->frame = f;
			s->side = 0;
			s->ring = rings;
			s->left = autoScaleFindRing(src, f, rings, -1);
			s->right = autoScaleFindRing(src, f, rings, 1);
			const AutoScaleUnit *const t = &src->unit[s->left];
			const int prevIdx = (rings > 1) ? autoScaleFindRing(src, f, rings - 1, -1) : core;
			const AutoScaleUnit *const prev = (prevIdx >= 0) ? &src->unit[prevIdx] : NULL;
			const bool prevRing = prev != NULL && prev->kind == AUTO_SCALE_RING;
			const int prevX2 = prevRing ? autoScaleUnitX2(prev) : src->cx2;
			const int prevY2 = (prev != NULL) ? autoScaleUnitY2(prev) : autoScaleUnitY2(t);
			s->lean = autoScaleLean(t, -1, src->csx, src->fwd);
			const double fan =
			    s->lean - (prevRing ? autoScaleLean(prev, -1, src->csx, src->fwd) : 0.0);
			s->turn = (fan > 0.0) ? MIN(fan, 20.0 * AUTO_SCALE_DEGREE) : 0.0;
			s->stepX = clampi((prevX2 - autoScaleUnitX2(t)) / 2, 0, 24);
			s->stepY = clampi((autoScaleUnitY2(t) - prevY2) / 2, -8, 8);
			s->room = AUTO_SCALE_REACH_PX - abs(autoScaleUnitX2(t) - src->cx2) / 2;
			autoScaleStyleSeed(s, t, src->fwd);
		}
		else if (core >= 0)
		{
			AutoScaleSeed *const s = &seed[n++];
			s->frame = f;
			s->side = 0;
			s->ring = 0;
			s->left = core;
			s->right = -1;
			s->lean = 0.0;
			s->turn = autoScaleAimable(&src->unit[core].tile[0]) ? 7.0 * AUTO_SCALE_DEGREE : 0.0;
			s->stepX = autoScaleGap(&src->unit[core]);
			s->stepY = 3 * src->fwd;
			s->room = AUTO_SCALE_REACH_PX;
			autoScaleStyleSeed(s, &src->unit[core], src->fwd);
		}
		for (int i = 0; i < src->count; ++i)
		{
			const AutoScaleUnit *const u = &src->unit[i];
			if (u->frame != f || u->kind != AUTO_SCALE_SOLO)
				continue;
			AutoScaleSeed *const s = &seed[n++];
			s->frame = f;
			s->side = u->side;
			s->ring = 0;
			s->left = i;
			s->right = -1;
			s->lean = autoScaleLean(u, u->side, src->csx, src->fwd);
			s->turn = autoScaleAimable(&u->tile[0]) ? 7.0 * AUTO_SCALE_DEGREE : 0.0;
			s->stepX = autoScaleGap(u);
			s->stepY = 3 * src->fwd;
			s->room = AUTO_SCALE_REACH_PX - abs(autoScaleUnitX2(u) - src->cx2) / 2;
			autoScaleStyleSeed(s, u, src->fwd);
		}
	}
	return n;
}

static void autoScaleRetime(AutoScaleUnit *u, double fraction)
{
	if (!autoScaleAimable(&u->tile[0]) || u->tile[0].sy == 0)
		return;
	for (int i = 0; i < u->count; ++i)
	{
		const int speed = abs(u->tile[i].sy);
		if (speed == 0)
			continue;
		const int retimed = clampi(speed - autoScaleRound(speed * fraction), (speed + 1) / 2, 30);
		u->tile[i].sy = (JE_shortint)((u->tile[i].sy > 0) ? retimed : -retimed);
	}
}

static void autoScaleRingOffset(const AutoScaleSeed *s, double at, int *dx, int *dy)
{
	double x = at * s->stepX, y = at * s->stepY;
	switch (autoScaleStyle)
	{
	case CUSTOM_SPREAD_ARROW:
	case CUSTOM_SPREAD_CRESCENT:
	case CUSTOM_SPREAD_CONVERGE:
	case CUSTOM_SPREAD_SPIRAL:
	case CUSTOM_SPREAD_WEAVE:
		x += 2.0 * MAX(at - 1.0, 0.0);
		break;
	case CUSTOM_SPREAD_WAVE:
		x += 2.0 * MAX(at - 1.0, 0.0);
		y = ((int)ceil(at) % 2 == 1) ? s->stepY : 0.0;
		break;
	case CUSTOM_SPREAD_TWIN:
	{
		const double first =
		    clampi(autoScaleRound(2.5 * s->stepX), s->stepX, MAX(s->room - s->stepX, s->stepX));
		x = (at <= 1.0) ? at * first : first + (at - 1.0) * s->stepX;
		break;
	}
	default:
		break;
	}
	*dx = autoScaleRound(x);
	*dy = autoScaleRound(y);
}

static void autoScaleLob(AutoScaleUnit *u, double at, int side)
{
	if (!autoScaleAimable(&u->tile[0]) || u->tile[0].sy == 0)
		return;
	const int lead = abs(u->tile[0].sy);
	const int back = clampi(autoScaleRound(lead * (0.3 + 0.1 * at)), 2, 12);
	const int push = side * clampi(autoScaleRound(1.0 + at), 1, 6);
	const bool ahead = u->tile[0].sy > 0;
	for (int i = 0; i < u->count; ++i)
	{
		AutoScaleBullet *const t = &u->tile[i];
		const int lag = clampi(back + lead - abs(t->sy), 2, 12);
		t->sx = (JE_shortint)clampi(autoScaleDriftX(t, 0) + push, -30, 30);
		t->sy = (JE_shortint)(ahead ? -lag : lag);
	}
}

static void autoScaleRingMotion(AutoScaleUnit *u, double at, int side, int fwd)
{
	const int outward = side * fwd;
	switch (autoScaleStyle)
	{
	case CUSTOM_SPREAD_ARROW:
	case CUSTOM_SPREAD_WEAVE:
		autoScaleRetime(u, 0.05 * at);
		if (at >= 3.0)
			autoScaleSpin(u, outward * (at - 2.0) * 6.0 * AUTO_SCALE_DEGREE);
		break;
	case CUSTOM_SPREAD_LOB:
		autoScaleLob(u, at, side);
		break;
	case CUSTOM_SPREAD_CRESCENT:
		autoScaleRetime(u, -0.05 * at);
		break;
	case CUSTOM_SPREAD_CONVERGE:
		autoScaleSpin(u, -outward * at * 4.0 * AUTO_SCALE_DEGREE);
		break;
	default:
		break;
	}
}

static bool autoScaleGrowRing(const AutoScaleShape *src, const AutoScaleSeed *s, int j, bool half,
                              int cap, double leanCap, int pairId, int origin, AutoScaleShape *out)
{
	const double at = half ? j - 0.5 : j;
	const AutoScaleUnit *const t = &src->unit[s->left];
	if (autoScaleFrameTiles(out, s->frame) + 2 * t->count > cap)
		return false;
	if (s->side != 0)
	{
		AutoScaleUnit wing[2];
		for (int w = 0; w < 2; ++w)
		{
			const int side = (w == 0) ? -1 : 1;
			int dx, dy;
			autoScaleRingOffset(s, j, &dx, &dy);
			wing[w] = *t;
			autoScaleUnitShift(&wing[w], side * dx, dy);
			if (abs(autoScaleUnitX2(&wing[w]) - src->cx2) / 2 > AUTO_SCALE_REACH_PX)
				return false;
			if (s->turn > 0.0)
				autoScaleSpin(&wing[w], side * src->fwd * j * s->turn);
			else
				autoScaleRingMotion(&wing[w], j, side, src->fwd);
			wing[w].weight = t->weight * pow(0.75, j);
			wing[w].grown = j;
			wing[w].ring = j;
			wing[w].side = side;
			wing[w].kind = AUTO_SCALE_FLANK;
			wing[w].pair = pairId;
			wing[w].origin = origin + w;
			if (autoScaleCoincides(out, &wing[w]))
				return false;
		}
		return autoScaleAddUnit(out, &wing[0]) && autoScaleAddUnit(out, &wing[1]);
	}

	int dx, dy;
	autoScaleRingOffset(s, at, &dx, &dy);
	AutoScaleUnit u = *t;
	autoScaleUnitShift(&u, -dx, dy);
	if (abs(autoScaleUnitX2(&u) - src->cx2) / 2 > AUTO_SCALE_REACH_PX)
		return false;
	if (s->turn > 0.0)
		autoScaleTurn(&u, -1, src->csx, src->fwd, MIN(s->lean + at * s->turn, leanCap));
	else
		autoScaleRingMotion(&u, at, -1, src->fwd);
	u.weight = t->weight * pow(0.75, half ? j - 1 : j);
	u.grown = half ? j - 1 : j;
	u.ring = s->ring + j;
	u.side = -1;
	u.kind = AUTO_SCALE_RING;
	u.pair = pairId;
	u.origin = origin;
	if (autoScaleCoincides(out, &u) || !autoScaleAddUnit(out, &u))
		return false;

	AutoScaleUnit r;
	autoScaleUnitMirror(&u, src->cx2, src->csx, &r);
	if (s->right >= 0)
		autoScaleAdoptSprites(&r, &src->unit[s->right]);
	r.side = 1;
	r.origin = origin + 1;
	return !autoScaleCoincides(out, &r) && autoScaleAddUnit(out, &r);
}

static bool autoScaleFlank(const AutoScaleShape *src, AutoScaleShape *out)
{
	const int side = autoScaleShapeSide(src);
	if (side == 0)
		return false;

	for (int i = 0; i < src->count; ++i)
	{
		AutoScaleUnit u;
		autoScaleUnitMirror(&src->unit[i], src->axis2, 0, &u);
		u.kind = AUTO_SCALE_SOLO;
		u.side = -side;
		u.ring = 0;
		u.grown = 0;
		u.pair = -1;
		u.origin = 1000 + i;
		if (autoScaleCoincides(out, &u) || !autoScaleAddUnit(out, &u))
			return false;
	}
	return autoScaleFramesEven(out);
}

static bool autoScaleLattice(const AutoScaleShape *s)
{
	for (int i = 0; i < s->count; ++i)
		if (s->unit[i].kind != AUTO_SCALE_CORE)
			return false;
	return s->count > 0;
}

static bool autoScaleGrow(const AutoScaleShape *src, int extra, AutoScaleShape *out)
{
	static AutoScaleSeed seed[AUTO_SCALE_UNITS];
	*out = *src;
	if (extra > 0 && autoScaleFlank(src, out))
		return true;
	*out = *src;
	const bool half = autoScaleLattice(src) && extra % 2 == 1;
	if (autoScaleLattice(src))
		extra = (extra + 1) / 2;
	const int seeds = autoScaleSeeds(src, seed);
	const double leanCap = (autoScaleStyle == CUSTOM_SPREAD_NOVA)
	                     ? 120.0 * AUTO_SCALE_DEGREE
	                     : MIN(MAX(60.0 * AUTO_SCALE_DEGREE, src->reach + 30.0 * AUTO_SCALE_DEGREE),
	                           150.0 * AUTO_SCALE_DEGREE);
	int wide = 0, cap = AUTO_SCALE_VOLLEY;
	for (int i = 0; i < src->count; ++i)
	{
		wide |= src->unit[i].count > 1;
		for (int k = 0; k < src->unit[i].count; ++k)
			if (src->unit[i].tile[k].attack >= 101 && src->unit[i].tile[k].attack <= 249)
				cap = MAX(cap, AUTO_SCALE_VOLLEY_CHAIN);
	}
	if (wide)
		cap = MAX(cap, AUTO_SCALE_VOLLEY_WIDE);
	cap = MAX(cap, autoScaleFrameTiles(src, 0));

	int pairId = 1000;
	for (int j = 1; j <= extra; ++j)
	{
		const int before = out->count;
		bool fits = seeds > 0;
		for (int k = 0; k < seeds && fits; ++k)
			fits = autoScaleGrowRing(src, &seed[k], j, half, cap, leanCap, pairId++,
			                         100 + 8 * k + 2 * (j - 1), out);
		if (fits)
			fits = autoScaleFramesEven(out);
		if (!fits)
		{
			out->count = before;
			break;
		}
	}
	if (half)
	{
		int n = 0;
		for (int i = 0; i < out->count; ++i)
			if (out->unit[i].kind != AUTO_SCALE_CORE)
				out->unit[n++] = out->unit[i];
		if (n == 0)
			return false;
		out->count = n;
	}
	return autoScaleFramesEven(out);
}

static int autoScaleShiftArea(const AutoScaleUnit *u, int d)
{
	int total = 0;
	for (int i = 0; i < u->count; ++i)
	{
		Sprite2_array sheet;
		unsigned int index;
		JE_word offset;
		if (!autoScaleSpriteRef(u->tile[i].sg, &sheet, &index, &offset) || (int)index + d < 1)
			return -1;
		const unsigned int moved = (unsigned int)((int)index + d);
		const int area = autoScaleSpriteArea(sheet, moved);
		if (area == 0 ||
		    sprite2_dominant_bank(sheet, moved) != sprite2_dominant_bank(sheet, index) ||
		    autoScaleProfile(sheet, moved) != autoScaleProfile(sheet, index))
			return -1;
		total += area;
	}
	return total;
}

static void autoScaleColumnStep(AutoScaleUnit *u, int step)
{
	if (step == 0 || !autoScaleUnitColumn(u))
		return;

	int areaAt[13];
	for (int d = -6; d <= 6; ++d)
		areaAt[d + 6] = autoScaleShiftArea(u, d);

	int pick = 0;
	for (int s = 0; s < abs(step); ++s)
	{
		int next = pick;
		for (int d = -6; d <= 6; ++d)
		{
			const int area = areaAt[d + 6];
			if (area < 0 || ((step > 0) ? area <= areaAt[pick + 6] : area >= areaAt[pick + 6]))
				continue;
			if (next != pick && ((step > 0) ? area >= areaAt[next + 6] : area <= areaAt[next + 6]))
				continue;
			next = d;
		}
		if (next == pick)
			break;
		pick = next;
	}
	for (int i = 0; i < u->count; ++i)
		u->tile[i].sg = (JE_word)(u->tile[i].sg + pick);
}

static JE_word autoScaleBase(JE_word sg)
{
	return (sg >= 60000) ? sg : (JE_word)(sg % 1000);
}

static bool autoScaleChain(JE_byte attack)
{
	return attack >= 101 && attack <= 249;
}

static int autoScaleSpriteMass(JE_word sg)
{
	Sprite2_array sheet;
	unsigned int index;
	JE_word offset;
	if (autoScaleSpriteRef(sg, &sheet, &index, &offset))
		return autoScaleSpriteArea(sheet, index);
	if (sg > 60000 && (unsigned int)(sg - 60001) < COUNTOF(sprite_table[0].sprite))
		return get_sprite_width(OPTION_SHAPES, sg - 60001) *
		       get_sprite_height(OPTION_SHAPES, sg - 60001);
	return 0;
}

static int autoScaleSlots(const JE_WeaponType *w)
{
	return MAX(clampi(w->multi, 1, CUSTOM_BULLETS_MAX), clampi(w->max, 1, CUSTOM_BULLETS_MAX));
}

static AutoScaleHeld *autoScaleHeldFind(int origin, int tile)
{
	for (int i = 0; i < autoScaleHeldCount; ++i)
		if (autoScaleHeld[i].origin == origin && autoScaleHeld[i].tile == tile)
			return &autoScaleHeld[i];
	return NULL;
}

static int autoScaleUnitMass(const AutoScaleUnit *u)
{
	int mass = 0;
	for (int k = 0; k < u->count; ++k)
		mass += autoScaleSpriteMass(u->tile[k].sg);
	return mass;
}

static int autoScaleUnitPower(const AutoScaleUnit *u)
{
	int burst = 0;
	for (int k = 0; k < u->count; ++k)
		burst += autoScaleChainDamage(u->tile[k].attack, 2);
	return burst;
}

static void autoScaleHold(const AutoScaleShape *s)
{
	for (int i = 0; i < s->count; ++i)
		for (int h = 0; h < autoScaleHeldCount; ++h)
			if (autoScaleHeld[h].origin == s->unit[i].origin &&
			    autoScaleHeld[h].tile >= s->unit[i].count)
				autoScaleHeld[h].origin = -1;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
		{
			AutoScaleHeld *h = autoScaleHeldFind(s->unit[i].origin, k);
			if (h == NULL)
			{
				if (autoScaleHeldCount == AUTO_SCALE_HELD)
					continue;
				h = &autoScaleHeld[autoScaleHeldCount++];
				h->origin = s->unit[i].origin;
				h->tile = k;
			}
			h->b = s->unit[i].tile[k];
		}
}

static JE_shortint autoScaleStretch(int lead, int speed, int base)
{
	if (lead == 0 || base == 0 || abs(lead) > 30 || abs(speed) > 30 || abs(base) > 30)
		return (JE_shortint)lead;
	const int value = lead + (((lead > 0) == (base > 0)) ? speed - base : base - speed);
	return (JE_shortint)((lead > 0) ? clampi(value, 1, 30) : clampi(value, -30, -1));
}

static void autoScaleRatchet(AutoScaleUnit *u, bool up)
{
	AutoScaleUnit held = *u;
	held.count = 0;
	for (;;)
	{
		const AutoScaleHeld *const h =
		    (held.count < AUTO_SCALE_TILES) ? autoScaleHeldFind(u->origin, held.count) : NULL;
		if (h == NULL)
			break;
		held.tile[held.count++] = h->b;
	}
	if (held.count == 0)
		return;

	const int now = autoScaleUnitMass(u), was = autoScaleUnitMass(&held);
	const int burst = autoScaleUnitPower(u), had = autoScaleUnitPower(&held);
	if (!(up ? now < was : now > was) && !(up ? burst < had : burst > had))
		return;

	const int dx = (autoScaleUnitX2(u) - autoScaleUnitX2(&held)) / 2,
	          dy = (autoScaleUnitY2(u) - autoScaleUnitY2(&held)) / 2;
	const AutoScaleBullet lead = u->tile[0];
	const int heldLead = held.tile[0].sy;
	for (int k = 0; k < held.count; ++k)
	{
		AutoScaleBullet *const t = &held.tile[k];
		t->bx = (JE_shortint)clampi(t->bx + dx, -128, 127);
		t->by = (JE_shortint)clampi(t->by + dy, -128, 127);
		t->sx = lead.sx;
		t->sy = autoScaleStretch(lead.sy, t->sy, heldLead);
		t->del = lead.del;
	}
	*u = held;
}

static int autoScaleUnitCost(const AutoScaleUnit *u, const AutoScaleUnit *s, int cx2)
{
	const AutoScaleBullet *const t = &u->tile[0], *const r = &s->tile[0];
	const int ux2 = autoScaleUnitX2(u), sx2 = autoScaleUnitX2(s);
	if (r->sg == 0 || (t->sg >= 60000) != (r->sg >= 60000) ||
	    (abs(ux2 - cx2) > 12 && abs(sx2) <= 4))
		return -1;
	const int vx = (abs(t->sx) <= 100 && abs(r->sx) <= 100) ? abs(t->sx - r->sx)
	             : (t->sx == r->sx) ? 0 : 20;
	const int vy = (abs(t->sy) <= 100 && abs(r->sy) <= 100) ? abs(t->sy - r->sy)
	             : (t->sy == r->sy) ? 0 : 20;
	return abs(ux2 - sx2) + abs(autoScaleUnitY2(u) - autoScaleUnitY2(s)) + 8 * (vx + vy);
}

static bool autoScaleLineTiles(AutoScaleUnit *u, const AutoScaleShape *stock, int cx2)
{
	int mine[AUTO_SCALE_TILES], theirs[AUTO_SCALE_TILES];
	int best = -1, bestCost = AUTO_SCALE_MATCH + 1;
	if (stock == NULL)
		return false;
	for (int i = 0; i < stock->count; ++i)
	{
		const int cost =
		    (stock->unit[i].count == u->count) ? autoScaleUnitCost(u, &stock->unit[i], cx2) : -1;
		if (cost >= 0 && cost < bestCost)
		{
			best = i;
			bestCost = cost;
		}
	}
	if (best < 0)
		return false;

	const AutoScaleUnit *const s = &stock->unit[best];
	autoScaleTileOrder(u, mine);
	autoScaleTileOrder(s, theirs);
	for (int k = 0; k < u->count; ++k)
	{
		AutoScaleBullet *const t = &u->tile[mine[k]];
		const AutoScaleBullet *const r = &s->tile[theirs[k]];
		const JE_word base = autoScaleBase(r->sg);
		t->sg = (t->sg < 60000 && base < 60000) ? (JE_word)(t->sg - t->sg % 1000 + base) : base;
		if (autoScaleChain(t->attack) && autoScaleChain(r->attack))
			t->attack = r->attack;
	}
	return true;
}

static bool autoScaleLineUses(const JE_word *line, JE_byte code)
{
	for (int l = 0; l < CUSTOM_POWER_LEVELS; ++l)
	{
		const int wn = line[l];
		if (wn <= 0 || wn > WEAP_NUM)
			continue;
		for (int i = 0; i < autoScaleSlots(&weapons[wn]); ++i)
			if (weapons[wn].attack[i] == code)
				return true;
	}
	return false;
}

static bool autoScaleSharedPort(int port)
{
	int users = 0;
	for (int o = 1; o <= OPTION_NUM; ++o)
		users += options[o].wport == port;
	return users >= 3;
}

static bool autoScaleStockPort(int port)
{
	return !customWeaponPortIsCustom((JE_word)port) && weaponPort[port].name[0] != '\0' &&
	       SDL_strcasecmp(weaponPort[port].name, "Test") != 0 && !autoScaleSharedPort(port);
}

static JE_byte autoScaleChainStep(JE_byte code, int step)
{
	JE_byte family[AUTO_SCALE_LADDER];
	int count = 0;
	if (step == 0 || !autoScaleChain(code))
		return code;
	for (int p = 1; p <= PORT_NUM; ++p)
	{
		if (!autoScaleStockPort(p))
			continue;
		for (int m = 0; m < clampi(weaponPort[p].opnum, 1, CUSTOM_WEAPON_MODES); ++m)
		{
			if (!autoScaleLineUses(weaponPort[p].op[m], code))
				continue;
			for (int l = 0; l < CUSTOM_POWER_LEVELS; ++l)
			{
				const int wn = weaponPort[p].op[m][l];
				for (int i = 0; wn > 0 && wn <= WEAP_NUM && i < autoScaleSlots(&weapons[wn]); ++i)
				{
					const JE_byte c = weapons[wn].attack[i];
					bool known = !autoScaleChain(c);
					for (int k = 0; k < count && !known; ++k)
						known = family[k] == c;
					if (!known && count < AUTO_SCALE_LADDER)
						family[count++] = c;
				}
			}
		}
	}

	int rank = 0;
	for (int i = 0; i < count; ++i)
		rank += autoScaleChainDamage(family[i], 2) < autoScaleChainDamage(code, 2);
	int pick = -1, pickRank = 0;
	for (int i = 0; i < count; ++i)
	{
		int r = 0;
		for (int k = 0; k < count; ++k)
			r += autoScaleChainDamage(family[k], 2) < autoScaleChainDamage(family[i], 2);
		const int want = clampi(rank + step, 0, count - 1);
		if (pick < 0 || abs(r - want) < abs(pickRank - want))
		{
			pick = i;
			pickRank = r;
		}
	}
	return (pick >= 0) ? family[pick] : code;
}

static void autoScaleSwapCore(AutoScaleUnit *u, const AutoScaleUnit *s)
{
	const int dx = (autoScaleUnitX2(u) - autoScaleUnitX2(s)) / 2,
	          dy = (autoScaleUnitY2(u) - autoScaleUnitY2(s)) / 2;
	const AutoScaleBullet lead = u->tile[0];
	const int stockLead = s->tile[0].sy;
	u->count = s->count;
	for (int k = 0; k < s->count; ++k)
	{
		AutoScaleBullet *const t = &u->tile[k];
		*t = s->tile[k];
		t->bx = (JE_shortint)clampi(s->tile[k].bx + dx, -128, 127);
		t->by = (JE_shortint)clampi(s->tile[k].by + dy, -128, 127);
		t->sx = lead.sx;
		t->sy = autoScaleStretch(lead.sy, t->sy, stockLead);
		t->del = lead.del;
		t->sg = autoScaleBase(s->tile[k].sg);
		if (lead.sg < 60000 && t->sg < 60000)
			t->sg = (JE_word)(t->sg + lead.sg - lead.sg % 1000);
		t->attack = (autoScaleChain(lead.attack) && autoScaleChain(s->tile[k].attack))
		          ? s->tile[k].attack : lead.attack;
	}
}

static void autoScaleRestyle(AutoScaleShape *out, const AutoScaleShape *stock, int step,
                             bool animated, bool up)
{
	int order[AUTO_SCALE_TILES], twinOrder[AUTO_SCALE_TILES];
	for (int i = 0; i < out->count; ++i)
	{
		AutoScaleUnit *const u = &out->unit[i];
		const bool paired = u->kind == AUTO_SCALE_RING || u->kind == AUTO_SCALE_FLANK;
		const int mate = paired ? autoScaleMate(out, i) : -1;
		if (u->kind == AUTO_SCALE_FLANK && u->side > 0 && mate >= 0)
		{
			for (int k = 0; k < u->count && k < out->unit[mate].count; ++k)
				u->tile[k].sg = out->unit[mate].tile[k].sg;
			continue;
		}

		const int core = (stock != NULL && out->frames == 1 && u->kind == AUTO_SCALE_CORE &&
		                  u->grown == 0 && !autoScaleLattice(out))
		               ? autoScaleCoreUnit(stock, 0) : -1;
		if (core >= 0 && stock->unit[core].count != u->count)
		{
			autoScaleSwapCore(u, &stock->unit[core]);
			autoScaleRatchet(u, up);
			continue;
		}
		if (autoScaleLineTiles(u, stock, out->cx2))
		{
			autoScaleRatchet(u, up);
			continue;
		}

		if (u->kind == AUTO_SCALE_RING && u->side > 0 && mate >= 0)
		{
			AutoScaleUnit r;
			autoScaleUnitMirror(&out->unit[mate], out->cx2, out->csx, &r);
			autoScaleTileOrder(&r, order);
			autoScaleTileOrder(u, twinOrder);
			for (int k = 0; k < r.count && k < u->count; ++k)
				u->tile[twinOrder[k]].sg = r.tile[order[k]].sg;
			continue;
		}

		const bool small = u->grown > 0 && (u->kind != AUTO_SCALE_RING || u->ring == u->grown);
		const int unitStep = small ? step - MIN(u->grown, 2) : step;
		if (u->count > 1 && !animated)
			autoScaleColumnStep(u, unitStep);
		else if (!animated && u->tile[0].sg != 0 && u->tile[0].sg < 60000)
			u->tile[0].sg = (JE_word)(u->tile[0].sg - u->tile[0].sg % 1000 +
			                          autoScaleFamilyStep(autoScaleBase(u->tile[0].sg), unitStep));
		for (int k = 0; k < u->count; ++k)
			u->tile[k].attack = autoScaleChainStep(u->tile[k].attack, unitStep);
		autoScaleRatchet(u, up);
	}
}

static double autoScaleShapeWeight(const AutoScaleShape *s)
{
	double sum = 0.0;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
			sum += autoScaleTileDamage(s->unit[i].tile[k].attack) * s->unit[i].weight;
	return sum;
}

static int autoScaleShapeChain(const AutoScaleShape *s)
{
	int sum = 0;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
			sum += autoScaleChainDamage(s->unit[i].tile[k].attack, 2);
	return sum;
}

static void autoScaleSettle(AutoScaleShape *s, double total)
{
	const double have = autoScaleShapeWeight(s);
	if (have <= 0.0)
		return;
	const double scale = MAX(total - autoScaleShapeChain(s), 0.0) / have;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
		{
			AutoScaleBullet *const t = &s->unit[i].tile[k];
			const int dmg = autoScaleTileDamage(t->attack);
			if (dmg > 0)
				t->attack =
				    autoScaleWithDamage(t->attack, autoScaleRound(dmg * s->unit[i].weight * scale));
		}
}

static double autoScaleShapeDps(const AutoScaleShape *s, int period)
{
	int dmg = autoScaleShapeChain(s);
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
			dmg += autoScaleTileDamage(s->unit[i].tile[k].attack);
	return (double)dmg / MAX(s->frames, 1) * 35.0 / period;
}

static bool autoScaleNudgeTile(AutoScaleBullet *t, int dir)
{
	const int dmg = autoScaleTileDamage(t->attack);
	if (dmg == 0 || autoScaleWithDamage(t->attack, dmg + dir) == t->attack)
		return false;
	t->attack = autoScaleWithDamage(t->attack, dmg + dir);
	return true;
}

static bool autoScaleNudge(AutoScaleShape *s, int dir)
{
	AutoScaleBullet *pick = NULL;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count && s->unit[i].kind == AUTO_SCALE_CORE; ++k)
		{
			AutoScaleBullet *const t = &s->unit[i].tile[k];
			const int dmg = autoScaleTileDamage(t->attack);
			if (dmg == 0 || autoScaleWithDamage(t->attack, dmg + dir) == t->attack)
				continue;
			if (pick == NULL || ((dir < 0) ? dmg > autoScaleTileDamage(pick->attack)
			                               : dmg < autoScaleTileDamage(pick->attack)))
				pick = t;
		}
	if (pick != NULL)
		return autoScaleNudgeTile(pick, dir);

	for (int i = 0; i < s->count; ++i)
	{
		AutoScaleUnit *const u = &s->unit[i];
		if (u->kind == AUTO_SCALE_CORE || u->side > 0)
			continue;
		const int mate = (u->kind == AUTO_SCALE_RING) ? autoScaleMate(s, i) : -1;
		for (int k = 0; k < u->count; ++k)
			if (autoScaleNudgeTile(&u->tile[k], dir))
			{
				if (mate >= 0 && k < s->unit[mate].count)
					autoScaleNudgeTile(&s->unit[mate].tile[k], dir);
				return true;
			}
	}
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count; ++k)
			if (autoScaleNudgeTile(&s->unit[i].tile[k], dir))
				return true;
	return false;
}

static void autoScaleMonotonic(AutoScaleShape *s, int *period, int fastest, int slowest,
                               double limit, bool above)
{
	for (int guard = 0; guard < 400; ++guard)
	{
		const double dps = autoScaleShapeDps(s, *period);
		if (above ? dps >= limit : dps <= limit)
			return;
		if (autoScaleNudge(s, above ? 1 : -1))
			continue;
		if (above ? *period <= fastest : *period >= slowest)
			return;
		*period += above ? -1 : 1;
	}
}

static bool autoScaleClear(AutoScaleShape *s, int period, double limit, bool above)
{
	for (int guard = 0; guard < 64; ++guard)
	{
		const double dps = autoScaleShapeDps(s, period);
		if (above ? dps >= limit : dps <= limit)
			return true;
		if (!autoScaleNudge(s, above ? 1 : -1))
			return false;
	}
	return false;
}

static void autoScaleWrite(const AutoScaleShape *s, const JE_WeaponType *anchor, int period,
                           JE_WeaponType *w)
{
	static AutoScaleBullet tiles[CUSTOM_BULLETS_MAX];
	int n = 0;
	for (int f = 0; f < s->frames; ++f)
	{
		const int first = n;
		for (int i = 0; i < s->count; ++i)
			for (int k = 0; k < s->unit[i].count && s->unit[i].frame == f && n < CUSTOM_BULLETS_MAX;
			     ++k)
				tiles[n++] = s->unit[i].tile[k];
		for (int i = first + 1; i < n; ++i)
		{
			const AutoScaleBullet t = tiles[i];
			int j = i;
			for (; j > first &&
			       (tiles[j - 1].bx > t.bx || (tiles[j - 1].bx == t.bx && tiles[j - 1].by > t.by));
			     --j)
				tiles[j] = tiles[j - 1];
			tiles[j] = t;
		}
	}

	const int clearTo = MAX(clampi(anchor->multi, 1, CUSTOM_BULLETS_MAX),
	                        clampi(anchor->max, 1, CUSTOM_BULLETS_MAX));
	*w = *anchor;
	w->shotrepeat = (JE_byte)(period - 1);
	for (int i = 0; i < n; ++i)
		autoScalePut(w, i, &tiles[i]);
	for (int i = n; i < clearTo; ++i)
	{
		const AutoScaleBullet empty = { 0, 0, 0, 0, 0, 0, 0 };
		autoScalePut(w, i, &empty);
	}
	w->multi = (JE_byte)(n / MAX(s->frames, 1));
	w->max = (JE_byte)n;
}

static int autoScaleLevelStep(int p, int a)
{
	const int step = (int)(abs(p - a) * 0.4 + 0.5);
	return clampi((p < a) ? -step : step, -3, 3);
}

static void autoScaleRigid(const JE_WeaponType *anchor, int p, int a, double scale, int period,
                           JE_WeaponType *w)
{
	*w = *anchor;
	w->shotrepeat = (JE_byte)(period - 1);
	const int slots = MAX(clampi(anchor->multi, 1, CUSTOM_BULLETS_MAX),
	                      clampi(anchor->max, 1, CUSTOM_BULLETS_MAX));
	const int step = (anchor->weapani > 0) ? 0 : autoScaleLevelStep(p, a);
	for (int i = 0; i < slots; ++i)
	{
		const int dmg = autoScaleTileDamage(w->attack[i]);
		if (dmg > 0)
			w->attack[i] = autoScaleWithDamage(w->attack[i], autoScaleRound(dmg * scale));
		if (w->sg[i] != 0 && w->sg[i] < 60000)
		{
			const JE_word spark = (JE_word)(w->sg[i] - w->sg[i] % 1000);
			w->sg[i] = (JE_word)(spark + autoScaleFamilyStep((JE_word)(w->sg[i] % 1000), step));
		}
	}
}

static int autoScaleLineWeapon(int line, int level)
{
	if (line > 0)
	{
		const int port = line / CUSTOM_WEAPON_MODES, mode = line % CUSTOM_WEAPON_MODES;
		for (int d = 0; d < CUSTOM_POWER_LEVELS; ++d)
			for (int k = -1; k <= 1; k += 2)
			{
				const int l = level + k * d;
				if (l < 0 || l >= CUSTOM_POWER_LEVELS)
					continue;
				const int wn = weaponPort[port].op[mode][l];
				if (wn > 0 && wn <= WEAP_NUM && weapons[wn].sg[0] != 0)
					return wn;
			}
		return 0;
	}
	const int option = -line;
	const int stages = clampi(options[option].pwr + 1, 1, CUSTOM_POWER_LEVELS);
	const int wn = options[option].wpnum + clampi(level, 0, stages - 1);
	return (wn > 0 && wn <= WEAP_NUM) ? wn : 0;
}

static int autoScaleLikeness(const JE_WeaponType *a, const JE_WeaponType *w)
{
	const int na = autoScaleSlots(a), nw = autoScaleSlots(w);
	bool exact = a->multi == w->multi && a->max == w->max;
	int shared = 0;
	for (int i = 0; i < na; ++i)
	{
		bool found = false;
		for (int j = 0; j < nw && !found; ++j)
			found = autoScaleBase(a->sg[i]) == autoScaleBase(w->sg[j]);
		shared += found;
		exact = exact && i < nw && a->bx[i] == w->bx[i] && a->by[i] == w->by[i] &&
		        a->sx[i] == w->sx[i] && a->sy[i] == w->sy[i] &&
		        autoScaleBase(a->sg[i]) == autoScaleBase(w->sg[i]);
	}
	return shared * 100 / na + (exact ? 500 : 0);
}

static void autoScaleConsider(const JE_WeaponType *anchor, int anchorLevel, int line, int level,
                              int *best, int *bestLevel, int *bestScore)
{
	const int wn = autoScaleLineWeapon(line, level);
	if (wn <= 0)
		return;
	const int score = autoScaleLikeness(anchor, &weapons[wn]) * 16 - abs(level - anchorLevel);
	if (score > *bestScore)
	{
		*best = line;
		*bestLevel = level;
		*bestScore = score;
	}
}

static int autoScaleLineage(const JE_WeaponType *anchor, int anchorLevel, int *lineLevel)
{
	int best = 0, bestScore = 50 * 16 - CUSTOM_POWER_LEVELS;
	for (int p = 1; p <= PORT_NUM; ++p)
	{
		if (!autoScaleStockPort(p))
			continue;
		for (int m = 0; m < clampi(weaponPort[p].opnum, 1, CUSTOM_WEAPON_MODES); ++m)
			for (int l = 0; l < CUSTOM_POWER_LEVELS; ++l)
				if (weaponPort[p].op[m][l] > 0)
					autoScaleConsider(anchor, anchorLevel, p * CUSTOM_WEAPON_MODES + m, l, &best,
					                  lineLevel, &bestScore);
	}
	for (int o = 1; o <= OPTION_NUM; ++o)
		if (!customSidekickSlotIsCustom(o) && options[o].pwr > 0)
			for (int l = 0; l <= MIN(options[o].pwr, CUSTOM_POWER_LEVELS - 1); ++l)
				autoScaleConsider(anchor, anchorLevel, -o, l, &best, lineLevel, &bestScore);
	return best;
}

static int autoScaleVolleyArea(const AutoScaleShape *s)
{
	int area = 0;
	for (int i = 0; i < s->count; ++i)
		for (int k = 0; k < s->unit[i].count && s->unit[i].frame == 0; ++k)
		{
			const JE_word sg = s->unit[i].tile[k].sg;
			Sprite2_array sheet;
			unsigned int index;
			JE_word offset;
			if (autoScaleSpriteRef(sg, &sheet, &index, &offset))
				area += autoScaleSpriteArea(sheet, index);
			else if (sg > 60000 && (unsigned int)(sg - 60001) < COUNTOF(sprite_table[0].sprite))
				area += get_sprite_width(OPTION_SHAPES, sg - 60001) *
				        get_sprite_height(OPTION_SHAPES, sg - 60001);
		}
	return area;
}

static int autoScaleRingCap(int period)
{
	return (period <= 2) ? 1 : (period <= 4) ? 2 : 3;
}

static bool autoScaleWeave(AutoScaleShape *s)
{
	bool sided = false;
	for (int i = 0; i < s->count; ++i)
		sided |= (s->unit[i].kind == AUTO_SCALE_RING || s->unit[i].kind == AUTO_SCALE_FLANK) &&
		         s->unit[i].side != 0;
	if (s->frames != 1 || !sided)
		return true;

	const int count = s->count;
	for (int i = 0; i < count; ++i)
	{
		AutoScaleUnit *const u = &s->unit[i];
		if ((u->kind == AUTO_SCALE_RING || u->kind == AUTO_SCALE_FLANK) && u->side != 0)
		{
			u->frame = (u->side > 0) ? 1 : 0;
			continue;
		}
		AutoScaleUnit twin = *u;
		twin.frame = 1;
		if (!autoScaleAddUnit(s, &twin))
			return false;
	}
	s->frames = 2;
	return autoScaleFramesEven(s) && 2 * autoScaleFrameTiles(s, 0) <= CUSTOM_BULLETS_MAX;
}

static bool autoScaleForm(const AutoScaleShape *src, int p, int a, int plan,
                          const AutoScaleShape *stock, bool animated, AutoScaleShape *out)
{
	const bool ok =
	    (p < a) ? autoScaleShrink(src, (double)plan / a, out) : autoScaleGrow(src, plan, out);
	if (ok)
		autoScaleRestyle(out, stock, autoScaleLevelStep(p, a), animated, p > a);
	return ok && (autoScaleStyle != CUSTOM_SPREAD_WEAVE || autoScaleWeave(out));
}

static int autoScaleShapeTiles(const AutoScaleShape *s)
{
	int tiles = 0;
	for (int i = 0; i < s->count; ++i)
		tiles += s->unit[i].count;
	return tiles;
}

static void autoScaleFlair(const AutoScaleShape *s, const JE_WeaponType *anchor, int p,
                           JE_WeaponType *w)
{
	bool grown = false;
	for (int i = 0; i < s->count; ++i)
		grown |= s->unit[i].grown > 0;
	if (autoScaleStyle == CUSTOM_SPREAD_LOB && anchor->acceleration == 0 && grown)
		w->acceleration = (JE_shortint)s->fwd;
	if (autoScaleStyle == CUSTOM_SPREAD_SPIRAL && anchor->circlesize == 0)
	{
		w->circlesize = (JE_byte)(2 + p / 3);
		const int dx = autoScaleSway2(w->circlesize) / 2;
		for (int i = 0; i < autoScaleSlots(w); ++i)
			w->bx[i] = (JE_shortint)clampi(w->bx[i] - dx, -128, 127);
	}
}

static bool autoScaleSameLook(const JE_WeaponType *x, const JE_WeaponType *y)
{
	const int n = autoScaleSlots(x);
	if (x->multi != y->multi || x->max != y->max)
		return false;
	for (int i = 0; i < n; ++i)
	{
		bool found = false;
		for (int j = 0; j < n && !found; ++j)
			found = autoScaleBase(x->sg[i]) == autoScaleBase(y->sg[j]) && x->bx[i] == y->bx[j] &&
			        x->by[i] == y->by[j] && x->sx[i] == y->sx[j] && x->sy[i] == y->sy[j];
		if (!found)
			return false;
	}
	return true;
}

static bool autoScaleLineFits(int line, int lineLevel, int a)
{
	const int top = (line > 0) ? CUSTOM_POWER_LEVELS - 1
	              : clampi(options[-line].pwr, 0, CUSTOM_POWER_LEVELS - 1);
	int looks = 0, clamped = 0;
	for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
	{
		const int at = lineLevel + p - a;
		clamped += at > top;
		const int wn = autoScaleLineWeapon(line, clampi(at, 0, top));
		bool seen = wn <= 0;
		for (int k = 0; k < p && !seen; ++k)
		{
			const int other = autoScaleLineWeapon(line, clampi(lineLevel + k - a, 0, top));
			seen = other > 0 && autoScaleSameLook(&weapons[other], &weapons[wn]);
		}
		looks += !seen;
	}
	return looks >= 5 && clamped <= 1;
}

static int autoScaleLooks(const JE_WeaponType *levels)
{
	int looks = 0;
	for (int p = 0; p < CUSTOM_POWER_LEVELS; ++p)
	{
		bool seen = false;
		for (int k = 0; k < p && !seen; ++k)
			seen = autoScaleSameLook(&levels[k], &levels[p]);
		looks += !seen;
	}
	return looks;
}

static bool autoScaleSameSlot(const JE_WeaponType *w, int i, int j, bool mirror)
{
	const JE_word sg = mirror ? autoScaleMirrorSprite(w->sg[j]) : w->sg[j];
	const int sx = (mirror && abs(w->sx[j]) <= 100) ? -w->sx[j] : w->sx[j];
	const int bx = mirror ? -w->bx[j] - autoScaleOverhang(w->sg[j]) : w->bx[j];
	return w->bx[i] == bx && w->by[i] == w->by[j] && w->sx[i] == sx && w->sy[i] == w->sy[j] &&
	       w->sg[i] == sg && w->attack[i] == w->attack[j] && w->del[i] == w->del[j];
}

static void autoScaleUnweave(JE_WeaponType *w)
{
	const int multi = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	if (w->max != 2 * multi)
		return;
	bool echo[CUSTOM_BULLETS_MAX] = { false };
	for (int i = multi; i < 2 * multi; ++i)
	{
		bool mirrored = false;
		for (int j = 0; j < multi && !mirrored && !echo[i]; ++j)
		{
			echo[i] = autoScaleSameSlot(w, i, j, false);
			mirrored = autoScaleSameSlot(w, i, j, true);
		}
		if (!mirrored && !echo[i])
			return;
	}
	int n = multi;
	for (int i = multi; i < 2 * multi; ++i)
		if (!echo[i])
		{
			w->sg[n] = w->sg[i];
			w->attack[n] = w->attack[i];
			w->del[n] = w->del[i];
			w->sx[n] = w->sx[i];
			w->sy[n] = w->sy[i];
			w->bx[n] = w->bx[i];
			w->by[n] = w->by[i];
			++n;
		}
	w->multi = w->max = (JE_byte)n;
}

static int autoScaleUsableStyle(int style, bool steer)
{
	if (!steer && (style == CUSTOM_SPREAD_FAN || style == CUSTOM_SPREAD_BURST ||
	               style == CUSTOM_SPREAD_LOB || style == CUSTOM_SPREAD_SPIRAL))
		return CUSTOM_SPREAD_ARROW;
	if (!steer && (style == CUSTOM_SPREAD_CONVERGE || style == CUSTOM_SPREAD_NOVA))
		return CUSTOM_SPREAD_WALL;
	return style;
}

static int autoScaleRolled = CUSTOM_SPREAD_AUTO;

static int autoScalePickStyle(const JE_WeaponType *anchor, bool chosen)
{
	bool steer = false;
	for (int i = 0; i < autoScaleSlots(anchor); ++i)
	{
		const AutoScaleBullet b = autoScaleGet(anchor, i);
		steer |= b.sg != 0 && autoScaleAimable(&b);
	}
	if (chosen)
	{
		autoScaleRolled = autoScaleUsableStyle(customWeaponSpread, steer);
		return autoScaleRolled;
	}

	int pool[CUSTOM_SPREAD_COUNT], n = 0;
	for (int style = CUSTOM_SPREAD_ARROW; style < CUSTOM_SPREAD_COUNT; ++style)
	{
		const int usable = autoScaleUsableStyle(style, steer);
		bool seen = usable == autoScaleRolled;
		for (int k = 0; k < n && !seen; ++k)
			seen = pool[k] == usable;
		if (!seen)
			pool[n++] = usable;
	}
	autoScaleRolled = pool[((Uint32)customRand(65536) + SDL_GetTicks()) % (Uint32)n];
	return autoScaleRolled;
}

static const int autoScaleCurve[CUSTOM_POWER_LEVELS] = { 100, 140, 180, 220, 260, 300, 350, 400, 460, 530, 600 };

static void autoScaleRawScale(JE_WeaponType *w, const JE_WeaponType *stock, double target)
{
	const int slots = autoScaleSlots(w), multi = clampi(w->multi, 1, CUSTOM_BULLETS_MAX);
	int scalable = 0, fixed = 0;
	for (int i = 0; i < slots; ++i)
	{
		scalable += autoScaleTileDamage(stock->attack[i]);
		fixed += autoScaleChainDamage(stock->attack[i], 2);
	}
	if (scalable == 0)
		return;
	const double want = target * (w->shotrepeat + 1) * slots / (35.0 * multi) - fixed;
	const double scale = MAX(want, 0.0) / scalable;
	for (int i = 0; i < slots; ++i)
	{
		const int dmg = autoScaleTileDamage(stock->attack[i]);
		if (dmg > 0)
			w->attack[i] = autoScaleWithDamage(stock->attack[i], autoScaleRound(dmg * scale));
	}
}

static bool autoScaleRawNudge(JE_WeaponType *w, int dir)
{
	int pick = -1;
	for (int i = 0; i < autoScaleSlots(w); ++i)
	{
		const int dmg = autoScaleTileDamage(w->attack[i]);
		if (dmg == 0 || autoScaleWithDamage(w->attack[i], dmg + dir) == w->attack[i])
			continue;
		if (pick < 0 || ((dir > 0) ? dmg < autoScaleTileDamage(w->attack[pick])
		                           : dmg > autoScaleTileDamage(w->attack[pick])))
			pick = i;
	}
	if (pick < 0)
		return false;
	w->attack[pick] =
	    autoScaleWithDamage(w->attack[pick], autoScaleTileDamage(w->attack[pick]) + dir);
	return true;
}

static double autoScaleRawMiss(double dps, double target)
{
	return (dps > 0.0 && target > 0.0) ? fabs(log(dps / target)) : 99.0;
}

static bool autoScaleRawBreaks(double dps, double limit, bool up)
{
	return limit > 0.0 && (up ? dps < limit - 1e-6 : dps > limit + 1e-6);
}

static double autoScaleRawTry(JE_WeaponType *w, const JE_WeaponType *stock, int period,
                              double target, double limit, bool up)
{
	w->shotrepeat = (JE_byte)(period - 1);
	autoScaleRawScale(w, stock, target);
	double dps = autoScaleWeaponDps(w);
	for (int guard = 0; guard < 64; ++guard)
	{
		JE_WeaponType next = *w;
		if (!autoScaleRawNudge(&next, (dps < target) ? 1 : -1))
			break;
		const double moved = autoScaleWeaponDps(&next);
		if (autoScaleRawMiss(moved, target) >= autoScaleRawMiss(dps, target))
			break;
		*w = next;
		dps = moved;
	}
	for (int guard = 0; guard < 64 && autoScaleRawBreaks(dps, limit, up); ++guard)
	{
		if (!autoScaleRawNudge(w, up ? 1 : -1))
			break;
		dps = autoScaleWeaponDps(w);
	}
	return dps;
}

static void autoScaleRawFit(JE_WeaponType *w, const JE_WeaponType *stock, int base, double target,
                            double limit, bool up)
{
	int scalable = 0;
	for (int i = 0; i < autoScaleSlots(stock); ++i)
		scalable += autoScaleTileDamage(stock->attack[i]);
	const JE_WeaponType from = *w;
	const double dps = autoScaleRawTry(w, stock, base, target, limit, up);
	if (base <= 1 || (!autoScaleRawBreaks(dps, limit, up) &&
	                  (scalable == 0 || autoScaleRawMiss(dps, target) <= 0.2)))
		return;
	double bestScore = 5.0 * autoScaleRawBreaks(dps, limit, up) +
	                   ((scalable > 0) ? autoScaleRawMiss(dps, target) : 0.0);
	for (int period = MAX(autoScaleRound(base * 0.5), 1); period <= MIN(base * 2, 256); ++period)
	{
		JE_WeaponType t = from;
		const double moved = autoScaleRawTry(&t, stock, period, target, limit, up);
		const double score = 5.0 * autoScaleRawBreaks(moved, limit, up) +
		                     ((scalable > 0) ? autoScaleRawMiss(moved, target) : 0.0) +
		                     0.5 * fabs(log((double)period / base));
		if (score < bestScore)
		{
			*w = t;
			bestScore = score;
		}
	}
}

static double autoScaleLineHeft(int line, int level)
{
	double heft = 0.0;
	for (int q = 0; q <= level; ++q)
	{
		const int wn = autoScaleLineWeapon(line, q);
		if (wn > 0)
			heft = MAX(heft, autoScaleWeaponDps(&weapons[wn]) * pow(1.08, level - q));
	}
	return heft;
}

static double autoScaleLineRatio(int line, int lineLevel, int a, int p)
{
	const int at = lineLevel + p - a, end = clampi(at, 0, CUSTOM_POWER_LEVELS - 1);
	const double from = autoScaleWeaponDps(&weapons[autoScaleLineWeapon(line, lineLevel)]);
	const double reach = autoScaleLineHeft(line, end);
	if (from <= 0.0 || reach <= 0.0)
		return (double)autoScaleCurve[p] / autoScaleCurve[a];
	return reach / from * autoScaleCurve[p] / autoScaleCurve[p - (at - end)];
}

static void autoScaleFollowLine(const JE_WeaponType *anchor, int m, int a, int line, int lineLevel)
{
	const JE_WeaponType *const x = &weapons[autoScaleLineWeapon(line, lineLevel)];
	const double pace = (double)(anchor->shotrepeat + 1) / (x->shotrepeat + 1);
	const double anchorDps = autoScaleWeaponDps(anchor);
	bool beam = false;
	for (int i = 0; i < autoScaleSlots(anchor); ++i)
		beam |= anchor->shotrepeat == 0 && x->shotrepeat == 0 && abs(anchor->sx[i]) > 100 &&
		        anchor->sg[i] != 0;
	for (int dir = -1; dir <= 1; dir += 2)
	{
		double limit = anchorDps;
		for (int p = a + dir; p >= 0 && p < CUSTOM_POWER_LEVELS; p += dir)
		{
			const int wn =
			    autoScaleLineWeapon(line, clampi(lineLevel + p - a, 0, CUSTOM_POWER_LEVELS - 1));
			if (wn <= 0)
				continue;
			JE_WeaponType w = weapons[wn];
			const int base = beam ? 1 : clampi(autoScaleRound((w.shotrepeat + 1) * pace), 1, 256);
			w.shotrepeat = (JE_byte)(base - 1);
			if (anchor->sound != x->sound)
				w.sound = anchor->sound;
			if (anchor->aim != x->aim)
				w.aim = anchor->aim;
			if (anchor->acceleration != x->acceleration)
				w.acceleration = anchor->acceleration;
			if (anchor->accelerationx != x->accelerationx)
				w.accelerationx = anchor->accelerationx;
			if (anchor->circlesize != x->circlesize)
				w.circlesize = anchor->circlesize;
			if (anchor->trail != x->trail)
				w.trail = anchor->trail;
			if (anchor->shipblastfilter != x->shipblastfilter)
				w.shipblastfilter = anchor->shipblastfilter;
			const double target = anchorDps * autoScaleLineRatio(line, lineLevel, a, p);
			if (anchorDps > 0.0)
				autoScaleRawFit(&w, &weapons[wn], base, target, limit, dir > 0);
			customWeaponRaw[m][p] = w;
			limit = autoScaleWeaponDps(&w);
		}
	}
}

void customWeaponAutoScaleLevels(void)
{
	const int m = clampi(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int a = clampi(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	const JE_WeaponType anchor = customWeaponRaw[m][a];
	const int basePeriod = anchor.shotrepeat + 1;
	JE_WeaponType source = anchor;
	autoScaleUnweave(&source);
	const bool shaped = autoScaleDecompose(&source, &autoScaleAnchor);
	const bool animated = anchor.weapani > 0;

	int lineLevel = 0;
	const int line = shaped ? autoScaleLineage(&anchor, a, &lineLevel) : 0;
	static JE_WeaponType followed[CUSTOM_POWER_LEVELS];
	const int lineWn = (line != 0) ? autoScaleLineWeapon(line, lineLevel) : 0;
	const bool stockLine = lineWn > 0 && autoScaleLikeness(&anchor, &weapons[lineWn]) >= 500;
	const bool stockFits = stockLine && autoScaleLineFits(line, lineLevel, a);
	const bool chosen =
	    customWeaponSpread >= (stockLine ? CUSTOM_SPREAD_STOCK : CUSTOM_SPREAD_ARROW) &&
	    customWeaponSpread < CUSTOM_SPREAD_COUNT;
	autoScaleStyle = autoScalePickStyle(&anchor, chosen);
	const bool follow = autoScaleStyle == CUSTOM_SPREAD_STOCK;
	if (follow)
	{
		autoScaleFollowLine(&anchor, m, a, line, lineLevel);
		customWeaponLastSpread = CUSTOM_SPREAD_STOCK;
		if (stockFits)
			return;
		memcpy(followed, customWeaponRaw[m], sizeof(followed));
		autoScaleStyle = CUSTOM_SPREAD_ARROW;
	}
	customWeaponLastSpread = autoScaleStyle;
	const bool lattice = shaped && autoScaleLattice(&autoScaleAnchor);
	const double anchorDps = autoScaleWeaponDps(&anchor);
	const bool damaging = anchorDps > 0.0;
	const int fastest = (basePeriod <= 2) ? 1 : MAX(autoScaleRound(basePeriod * 0.4), 2);
	bool beam = false;
	for (int i = 0; i < autoScaleSlots(&anchor); ++i)
		beam |= basePeriod <= 1 && abs(anchor.sx[i]) > 100 && anchor.sg[i] != 0;

	for (int dir = -1; dir <= 1; dir += 2)
	{
		double limit = anchorDps;
		int trend = basePeriod;
		int volley = clampi(anchor.multi, 1, CUSTOM_BULLETS_MAX);
		if (shaped)
			volley = autoScaleShapeTiles(&autoScaleAnchor);
		autoScaleHeldCount = 0;
		if (shaped)
			autoScaleHold(&autoScaleAnchor);
		int heft = shaped ? autoScaleVolleyArea(&autoScaleAnchor) : 0;
		for (int p = a + dir; p >= 0 && p < CUSTOM_POWER_LEVELS; p += dir)
		{
			const double ratio = (double)autoScaleCurve[p] / autoScaleCurve[a];
			const double target = anchorDps * ratio;
			const int planned =
			    beam ? 1 : clampi(autoScaleRound(basePeriod / pow(ratio, 0.25)), fastest, 256);
			if (!shaped)
			{
				const int period = (dir < 0) ? MAX(planned, trend) : MIN(planned, trend);
				autoScaleRigid(&anchor, p, a, ratio * period / basePeriod, period,
				               &customWeaponRaw[m][p]);
				trend = period;
				continue;
			}

			const int lineTo = (line != 0) ? autoScaleLineWeapon(line, lineLevel + p - a) : 0;
			const JE_WeaponType *const stock =
			    (lineTo > 0 && (!animated || weapons[lineTo].weapani == anchor.weapani))
			        ? &weapons[lineTo]
			        : NULL;
			const AutoScaleShape *const stockShape =
			    (stock != NULL && autoScaleDecompose(stock, &autoScaleStock)) ? &autoScaleStock
			                                                                  : NULL;

			const int growth = lattice ? 2 : 1;
			const int highestPlan = (p < a) ? p : growth * autoScaleRingCap(basePeriod);
			const double stride =
			    MIN(0.25 * growth, (double)highestPlan / MAX(CUSTOM_POWER_LEVELS - 1 - a, 1));
			const int plannedPlan = (p < a) ? p : MIN((int)((p - a) * stride + 0.5), highestPlan);
			int bestPlan = -1, bestPush = 0;
			int bestPeriod = (dir < 0) ? MAX(planned, trend) : MIN(planned, trend);
			double bestScore = 0.0;
			for (int plan = highestPlan; plan >= 0; --plan)
			{
				if (!autoScaleForm(&autoScaleAnchor, p, a, plan, stockShape, animated,
				                   &autoScaleLevel))
					continue;
				const int tiles = autoScaleShapeTiles(&autoScaleLevel);
				if ((dir > 0) ? tiles < volley : tiles > volley)
					continue;
				const int area = autoScaleVolleyArea(&autoScaleLevel);
				const bool heavier = tiles > volley || area * 5 >= heft * 6;
				const bool lighter = tiles < volley || area * 6 <= heft * 5;
				const int lowPeriod = beam        ? 1
				                      : (dir < 0) ? MAX(lighter ? trend / 2 : trend - 1,
				                                        MAX(fastest, MIN(basePeriod, 2)))
				                                  : clampi(MIN(trend, planned) - 4, fastest, 256);
				const int highPeriod =
				    beam        ? 1
				    : (dir < 0) ? MIN(MAX(trend, planned) + 4, 256)
				                : MIN(heavier ? MIN(trend * 2, trend + 12) : trend + 1, 256);
				for (int trial = 0; trial < 2 * (highPeriod - lowPeriod + 1); ++trial)
				{
					const int period = lowPeriod + trial / 2, push = trial % 2;
					autoScaleTrial = autoScaleLevel;
					autoScaleSettle(&autoScaleTrial,
					                target * period * autoScaleTrial.frames / 35.0);
					if (damaging && !autoScaleClear(&autoScaleTrial, period, limit, dir > 0))
						continue;
					if (push && (!damaging || !autoScaleNudge(&autoScaleTrial, dir)))
						continue;
					const int against = (dir > 0) ? period - trend : trend - period;
					const double slip = ((dir > 0) ? heavier : lighter) ? 0.03 : 0.1;
					const double dps = autoScaleShapeDps(&autoScaleTrial, period);
					const double miss = damaging ? fabs(log(dps / target)) : 0.0;
					const double stall = (damaging && fabs(dps - limit) < 0.001) ? 0.25 : 0.0;
					const double score = 0.3 * abs(plannedPlan - plan) +
					                     0.02 * abs(period - planned) + slip * MAX(against, 0) +
					                     0.5 * miss + ((miss > 0.7) ? 2.0 + miss : 0.0) + stall;
					if (bestPlan < 0 || score < bestScore)
					{
						bestPlan = plan;
						bestPeriod = period;
						bestPush = push;
						bestScore = score;
					}
				}
			}

			int period = bestPeriod;
			int plan = (bestPlan >= 0) ? bestPlan : plannedPlan;
			while (!autoScaleForm(&autoScaleAnchor, p, a, plan, stockShape, animated,
			                      &autoScaleLevel))
			{
				if (plan == 0)
				{
					autoScaleLevel = autoScaleAnchor;
					break;
				}
				--plan;
			}
			autoScaleSettle(&autoScaleLevel, target * period * autoScaleLevel.frames / 35.0);
			if (damaging && !autoScaleClear(&autoScaleLevel, period, limit, dir > 0))
				autoScaleMonotonic(&autoScaleLevel, &period, fastest, beam ? 1 : 256, limit,
				                   dir > 0);
			if (bestPush)
				autoScaleNudge(&autoScaleLevel, dir);
			autoScaleWrite(&autoScaleLevel, &anchor, period, &customWeaponRaw[m][p]);
			autoScaleFlair(&autoScaleLevel, &anchor, p, &customWeaponRaw[m][p]);
			autoScaleHold(&autoScaleLevel);
			limit = autoScaleShapeDps(&autoScaleLevel, period);
			trend = period;
			volley = autoScaleShapeTiles(&autoScaleLevel);
			heft = autoScaleVolleyArea(&autoScaleLevel);
		}
	}
	if (follow && autoScaleLooks(followed) > autoScaleLooks(customWeaponRaw[m]))
	{
		memcpy(customWeaponRaw[m], followed, sizeof(followed));
		customWeaponLastSpread = CUSTOM_SPREAD_STOCK;
	}
}
