#define ENDLESS_INTERNAL_ACCESS
#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "debug_flags.h"
#include "demo.h"
#include "endless.h"
#include "episodes.h"
#include "endless_internal.h"
#include "game_state.h"
#include "player.h"
#include "player_ship.h"
#include "specials.h"
#include "video.h"

#include "SDL.h"

#include <stdlib.h>
#include <string.h>

// Fire one twiddle through the real path, with the clocks and durations that gate it cleared.
static void qa_fire_twiddle(JE_byte pwr, uint *armor, uint *shield)
{
	special[SPECIAL_NUM].pwr = pwr;
	SFExecuted[0] = SPECIAL_NUM;
	shotRepeat[SHOT_SPECIAL] = 0;
	specialWait = 0;
	flareDuration = 0;
	flareStart = false;
	zinglonDuration = 0;
	astralDuration = 0;
	JE_resetTwiddleClocks();
	JE_doSpecialShot(1, armor, shield);
}

/* What a twiddle charges. */
void qa_test_twiddle_charges(void)
{
	if (VGAScreenSeg == NULL || game_screen == NULL)
		return;  // the charge path repaints the real shield and armor gauges

	const JE_boolean savedEndless = endlessMode;
	const JE_boolean savedMods = endlessCampaignMods;
	const JE_boolean savedAuto = autoFireSpecial;
	const JE_boolean savedDbgAuto = debugAutofireTwiddle;
	const JE_boolean savedTrigger = debugTwiddleTrigger;
	const JE_boolean savedFlareStart = flareStart;
	const JE_byte savedDbgTwiddle = debugTwiddleSpecial;
	const JE_byte savedSF = SFExecuted[0];
	const JE_byte savedStype = special[SPECIAL_NUM].stype;
	const JE_byte savedPwr = special[SPECIAL_NUM].pwr;
	const JE_byte savedTemp = temp;
	const JE_byte savedTemp2 = temp2;
	const JE_byte savedWait = specialWait;
	const JE_byte savedRepeat = shotRepeat[SHOT_SPECIAL];
	const JE_byte savedZing = zinglonDuration;
	const JE_byte savedAstral = astralDuration;
	const JE_word savedFlare = flareDuration;
	const Player saved0 = player[0];
	SDL_Surface *const savedVGA = VGAScreen;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	// The scratch slot takes an inert type: the effect switch has no case 0, so only the charge
	// runs. Clearing the equipped special keeps the other fire gates shut.
	special[SPECIAL_NUM].stype = 0;
	player[0].items.special = 0;
	autoFireSpecial = false;
	debugAutofireTwiddle = false;
	debugTwiddleTrigger = false;
	debugTwiddleSpecial = 0;
	endlessMode = false;
	endlessCampaignMods = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	// Each charge kind: the bars it starts with, the bars vanilla leaves behind, and the magnitude
	// JE_specialComplete has to be handed either way.
	static const struct {
		JE_byte pwr;
		uint startShield, startArmor;
		uint leftShield, leftArmor;
		JE_byte magnitude;
	} charges[] = {
		{  20, 50, 30, 30, 30, 20 },  // a fixed shield charge
		{  98, 41, 30,  0, 30, 41 },  // the whole bar, which is also the effect's size
		{  99, 41, 30, 20, 30, 20 },  // half of it, rounded as vanilla rounds an odd bar
		{ 104, 50, 30, 50, 26,  4 },  // a fixed armor charge
	};

	bool stock = true, cheaper = true, sameEffect = true;
	for (unsigned c = 0; c < COUNTOF(charges); ++c)
	{
		uint shield = charges[c].startShield, armor = charges[c].startArmor;
		qa_fire_twiddle(charges[c].pwr, &armor, &shield);
		stock &= shield == charges[c].leftShield && armor == charges[c].leftArmor
		      && temp2 == charges[c].magnitude;
	}
	qa_check(stock, "a twiddle charges its stock shield or armor without Kinetic Converter");

	endlessMode = true;
	endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)endlessPerkMaxStack(PERK_KINETIC);
	endlessPerkRederive();

	for (unsigned c = 0; c < COUNTOF(charges); ++c)
	{
		uint shield = charges[c].startShield, armor = charges[c].startArmor;
		qa_fire_twiddle(charges[c].pwr, &armor, &shield);
		const uint spent[2] = { charges[c].startShield - shield, charges[c].startArmor - armor };
		const uint list[2] = { charges[c].startShield - charges[c].leftShield,
		                       charges[c].startArmor - charges[c].leftArmor };
		for (unsigned b = 0; b < COUNTOF(spent); ++b)
			cheaper &= (list[b] == 0) ? spent[b] == 0 : (spent[b] > 0 && spent[b] < list[b]);
		sameEffect &= temp2 == charges[c].magnitude;
	}
	qa_check(cheaper, "Kinetic Converter charges less for every twiddle, proportional ones too");
	qa_check(sameEffect, "...and the special still reads the magnitude its list price bought");

	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	special[SPECIAL_NUM].stype = savedStype;
	special[SPECIAL_NUM].pwr = savedPwr;
	SFExecuted[0] = savedSF;
	temp = savedTemp;
	temp2 = savedTemp2;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	specialWait = savedWait;
	flareDuration = savedFlare;
	flareStart = savedFlareStart;
	zinglonDuration = savedZing;
	astralDuration = savedAstral;
	autoFireSpecial = savedAuto;
	debugAutofireTwiddle = savedDbgAuto;
	debugTwiddleTrigger = savedTrigger;
	debugTwiddleSpecial = savedDbgTwiddle;
	endlessMode = savedEndless;
	endlessCampaignMods = savedMods;
	player[0] = saved0;
	VGAScreen = savedVGA;
}

/* One twiddle keystroke through the real detector. The codes are the keyboardCombos alphabet:
 * 1..4 are UP/DOWN/LEFT/RIGHT, 5..8 the same four with fire held, 9 everything released. */
static void qa_twiddle_step(JE_byte playerNum, JE_byte code)
{
	enum { PX = 100, PY = 100 };
	const bool withFire = (code >= 5 && code <= 8);
	const JE_byte dir = withFire ? (JE_byte)(code - 4) : code;
	int mx = PX, my = PY;

	button[0] = withFire;

	if (dir == 1)
		my = PY + 1;  // up
	else if (dir == 2)
		my = PY - 1;  // down
	else if (dir == 3)
		mx = PX + 1;  // left
	else if (dir == 4)
		mx = PX - 1;  // right
	// any other code is 9, which holds nothing at all

	JE_SFCodes(playerNum, PX, PY, mx, my);
}

// Nothing entered on either seat.
static void qa_twiddle_clear(void)
{
	memset(SFCurrentCode, 0, sizeof(SFCurrentCode));
	memset(SFExecuted, 0, sizeof(SFExecuted));
}

// Perform one whole combo row and report the special it executed (0 if the ship refused it).
static JE_byte qa_perform_twiddle(JE_byte playerNum, JE_byte comboRow)
{
	qa_twiddle_clear();

	for (unsigned k = 0; k < COUNTOF(keyboardCombos[0]); ++k)
	{
		const JE_byte code = keyboardCombos[comboRow][k];
		if (code == 0 || code > 9)  // 0 pads the row, >100 is the terminator the detector consumes
			break;
		qa_twiddle_step(playerNum, code);
	}

	return SFExecuted[playerNum - 1];
}

/* Every ship reaches the twiddles its combo row lists, for either seat, and a ship with no row of
 * its own reaches none without reading past the table. */
void qa_test_twiddle_ships(void)
{
	const JE_boolean savedSuper = superTyrian;
	const JE_boolean savedTwo = twoPlayerMode;
	const JE_boolean savedCoop = coopCampaignMode;
	const JE_boolean savedCoopEndless = coopEndlessMode;
	const JE_boolean savedSep = arcadeSeparateMode;
	const Player saved0 = player[0], saved1 = player[1];
	JE_byte savedCode[2][21];
	JE_byte savedExec[2];
	bool savedButton[4];
	memcpy(savedCode, SFCurrentCode, sizeof(savedCode));
	memcpy(savedExec, SFExecuted, sizeof(savedExec));
	memcpy(savedButton, button, sizeof(savedButton));

	superTyrian = false;
	twoPlayerMode = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	arcadeSeparateMode = false;

	bool everyRow = true, everySpecial = true;
	unsigned rowsChecked = 0;
	for (uint ship = 1; ship <= SHIP_NUM; ++ship)
	{
		player[0].items.ship = (Uint8)ship;
		for (unsigned slot = 0; slot < COUNTOF(shipCombos[0]); ++slot)
		{
			const JE_byte row = shipCombos[ship][slot];
			if (row == 0)
				continue;  // that slot is empty for this ship

			++rowsChecked;
			const JE_byte fired = qa_perform_twiddle(1, (JE_byte)(row - 1));
			everyRow &= fired != 0;
			everySpecial &= fired <= SPECIAL_NUM;
		}
	}
	// The floor guards against a loop that visits nothing; the table lists 34 rows today.
	qa_check(rowsChecked >= 30 && everyRow && everySpecial,
	         "every ship performs each twiddle its combo row lists");

	// The last two rows the table carries, checked by name so a bound that shortens names itself.
	player[0].items.ship = 15;  // Red Dragon
	const JE_byte dragon = qa_perform_twiddle(1, (JE_byte)(shipCombos[15][0] - 1));
	player[0].items.ship = 16;  // Gencore II
	const JE_byte gencore = qa_perform_twiddle(1, (JE_byte)(shipCombos[16][0] - 1));
	qa_check(dragon != 0 && gencore != 0,
	         "the last two ships in the table twiddle like the rest");

	// A shipedit "extra" ship has no row of its own, so it reaches no twiddle at all.
	player[0].items.ship = 91;
	qa_check(qa_perform_twiddle(1, 0) == 0 && qa_perform_twiddle(1, 25) == 0,
	         "a ship outside the combo table has no twiddles");

	// The Endless-sold Dragonwing collapses to the shared "2nd Player ship" row on any seat.
	player[0].items.ship = SHIP_DRAGONWING;
	const JE_byte boughtWing = qa_perform_twiddle(1, (JE_byte)(shipCombos[0][0] - 1));
	qa_check(boughtWing != 0, "a bought Dragonwing twiddles off the shared row");

	/* Seat two. The linked pair's rear half has no ship of its own and twiddles off row 0; every
	 * mode where it flies its own ship uses that ship's row. */
	twoPlayerMode = true;
	player[1].items.ship = 12;  // Nort Ship: seeker bombs, protron field, post-it
	const JE_byte linked = qa_perform_twiddle(2, (JE_byte)(shipCombos[0][0] - 1));
	const JE_byte linkedOwn = qa_perform_twiddle(2, (JE_byte)(shipCombos[12][0] - 1));
	coopCampaignMode = true;
	const JE_byte coopOwn = qa_perform_twiddle(2, (JE_byte)(shipCombos[12][0] - 1));
	qa_check(linked != 0 && linkedOwn == 0 && coopOwn != 0,
	         "player two twiddles off the shared row when linked and off its own ship in co-op");

	memcpy(button, savedButton, sizeof(button));
	memcpy(SFExecuted, savedExec, sizeof(SFExecuted));
	memcpy(SFCurrentCode, savedCode, sizeof(savedCode));
	player[0] = saved0;
	player[1] = saved1;
	arcadeSeparateMode = savedSep;
	coopEndlessMode = savedCoopEndless;
	coopCampaignMode = savedCoop;
	twoPlayerMode = savedTwo;
	superTyrian = savedSuper;
}

/* Flicks inside the 2:1 cone collapse to one axis; shallower diagonals remain neutral. */
void qa_test_twiddle_diagonals(void)
{
	enum { PX = 100, PY = 100 };

	// The detector mirrors the horizontal half on an upside-down screen, so pin the flag off for
	// every case that expects an unmirrored target.
	const JE_boolean savedInvert = smoothies[9-1];
	smoothies[9-1] = false;

	// One entry per shape a flick can take, with the target the cone has to produce.
	static const struct {
		int dx, dy;
		int wantX, wantY;
	} flicks[] = {
		{  0,  0, PX,     PY     },  // standing still
		{  3,  0, PX - 1, PY     },  // right
		{ -3,  0, PX + 1, PY     },  // left
		{  0,  3, PX,     PY - 1 },  // down
		{  0, -3, PX,     PY + 1 },  // up
		{  3,  1, PX - 1, PY     },  // mostly right
		{  1,  3, PX,     PY - 1 },  // mostly down
		{  5, -2, PX - 1, PY     },  // mostly right, drifting up
		{ -1, -4, PX,     PY + 1 },  // mostly up, drifting left
		{  3,  2, PX - 1, PY - 1 },  // inside the cone on neither axis: both survive
		{  2, -3, PX - 1, PY + 1 },
		{  2,  2, PX - 1, PY - 1 },  // an exact diagonal
		{ -2, -2, PX + 1, PY + 1 },
	};

	bool resolved = true;
	for (unsigned f = 0; f < COUNTOF(flicks); ++f)
	{
		int tx = 0, ty = 0;
		SF_twiddleTarget(PX, PY, flicks[f].dx, flicks[f].dy, &tx, &ty);
		resolved &= tx == flicks[f].wantX && ty == flicks[f].wantY;
	}
	qa_check(resolved, "a flick collapses inside the 2:1 cone and keeps both axes outside it");

	// An upside-down screen mirrors the horizontal half of every flick. The vertical half reaches
	// the helper already inverted, so only the expected x moves.
	smoothies[9-1] = true;
	bool mirrored = true;
	for (unsigned f = 0; f < COUNTOF(flicks); ++f)
	{
		int tx = 0, ty = 0;
		SF_twiddleTarget(PX, PY, flicks[f].dx, flicks[f].dy, &tx, &ty);
		mirrored &= tx == 2 * PX - flicks[f].wantX && ty == flicks[f].wantY;
	}
	qa_check(mirrored, "an upside-down screen mirrors a flick's horizontal half only");
	smoothies[9-1] = false;

	// A twiddle performed while the ship drifts sideways still registers.
	const Player saved0 = player[0];
	JE_byte savedCode[2][21];
	JE_byte savedExec[2];
	bool savedButton[4];
	memcpy(savedCode, SFCurrentCode, sizeof(savedCode));
	memcpy(savedExec, SFExecuted, sizeof(savedExec));
	memcpy(savedButton, button, sizeof(savedButton));

	// Gencore Phoenix's first combo row is Ice Blast: DOWN, then UP with fire held.
	enum { ICE_BLAST_SPECIAL = 42 };
	player[0].items.ship = 3;
	memset(SFCurrentCode, 0, sizeof(SFCurrentCode));
	SFExecuted[0] = 0;

	static const struct {
		int dx, dy;
		bool fire;
	} iceBlastAdrift[] = {
		{ -1,  3, false },  // down, drifting left
		{  1, -3, true  },  // up and fire, drifting right
	};
	for (unsigned s = 0; s < COUNTOF(iceBlastAdrift); ++s)
	{
		int tx = 0, ty = 0;
		button[0] = iceBlastAdrift[s].fire;
		SF_twiddleTarget(PX, PY, iceBlastAdrift[s].dx, iceBlastAdrift[s].dy, &tx, &ty);
		JE_SFCodes(1, PX, PY, tx, ty);
	}
	qa_check(SFExecuted[0] == ICE_BLAST_SPECIAL,
	         "a twiddle performed while drifting sideways still fires");

	/* A flick shallower than the cone is neutral. Ice Blast sits in ship 3's first combo slot, so
	 * SFCurrentCode[0][0] exposes its progress directly. */
	memset(SFCurrentCode, 0, sizeof(SFCurrentCode));
	SFExecuted[0] = 0;
	int tx = 0, ty = 0;
	button[0] = false;
	SF_twiddleTarget(PX, PY, 0, 3, &tx, &ty);  // down, the first Ice Blast step
	JE_SFCodes(1, PX, PY, tx, ty);
	const JE_byte afterStep = SFCurrentCode[0][0];
	SF_twiddleTarget(PX, PY, 3, 2, &tx, &ty);  // a wander outside the cone
	JE_SFCodes(1, PX, PY, tx, ty);
	qa_check(afterStep == 1 && SFCurrentCode[0][0] == 1 && SFExecuted[0] == 0,
	         "a flick outside the cone neither advances nor cancels a combo");
	button[0] = true;
	SF_twiddleTarget(PX, PY, 1, -3, &tx, &ty);  // up with fire, the second step
	JE_SFCodes(1, PX, PY, tx, ty);
	qa_check(SFExecuted[0] == ICE_BLAST_SPECIAL, "...and the combo still completes around it");

	memcpy(button, savedButton, sizeof(button));
	memcpy(SFExecuted, savedExec, sizeof(SFExecuted));
	memcpy(SFCurrentCode, savedCode, sizeof(savedCode));
	player[0] = saved0;
	smoothies[9-1] = savedInvert;
}

/* Target directions use JE_SFCodes values 1..4; ambiguous or absent input is 0. */
static int qa_twiddle_code(int px, int py, int tx, int ty)
{
	const int count = (ty > py) + (ty < py) + (px < tx) + (px > tx);
	if (count != 1)
		return 0;
	return (ty > py) * 1 + (ty < py) * 2 + (px < tx) * 3 + (px > tx) * 4;
}

/* Directions reconstructed from rb_move_bits must match local flick detection. */
void qa_test_twiddle_wire(void)
{
	enum { PX = 100, PY = 100 };
	const JE_boolean savedInvert = smoothies[9-1];
	const JE_boolean savedSuper = superTyrian;
	const Player saved0 = player[0];
	JE_byte savedCode[2][21];
	JE_byte savedExec[2];
	bool savedButton[4];
	memcpy(savedCode, SFCurrentCode, sizeof(savedCode));
	memcpy(savedExec, SFExecuted, sizeof(savedExec));
	memcpy(savedButton, button, sizeof(savedButton));

	// Every shape a tick's displacement can take, as rb_fill_tuple measures it.
	static const struct {
		int dx, dy;
	} flicks[] = {
		{  0,  0 }, {  1,  0 }, {  0, -1 }, { -1,  1 },
		{  3,  0 }, { -3,  0 }, {  0,  3 }, {  0, -3 },
		{  3,  1 }, {  1,  3 }, {  5, -2 }, { -1, -4 },
		{  3,  2 }, {  2, -3 }, {  2,  2 }, { -2, -2 },
	};

	bool agree = true;
	for (int inverted = 0; inverted <= 1; ++inverted)
	{
		smoothies[9-1] = inverted != 0;
		for (unsigned f = 0; f < COUNTOF(flicks); ++f)
		{
			int lx = 0, ly = 0, wx = 0, wy = 0, dx = 0, dy = 0;
			SF_twiddleTarget(PX, PY, flicks[f].dx, flicks[f].dy, &lx, &ly);
			rb_move_dir(rb_move_bits(flicks[f].dx, flicks[f].dy), &dx, &dy);
			SF_twiddleTarget(PX, PY, dx, dy, &wx, &wy);
			agree &= qa_twiddle_code(PX, PY, lx, ly) == qa_twiddle_code(PX, PY, wx, wy);
		}
	}
	qa_check(agree, "a flick reads as the same code from the wire as from its displacement, "
	                "upside down or not");

	/* A whole combo over the wire on an upside-down screen. */
	enum { SEEKER_BOMBS_SPECIAL = 39 };
	static const struct {
		int dx, dy;
		bool fire;
	} rotated[] = {
		{  1,  0, false },
		{ -1,  0, false },
		{  0,  1, true  },
	};
	superTyrian = false;
	player[0].items.ship = 12;

	for (int inverted = 1; inverted >= 0; --inverted)
	{
		smoothies[9-1] = inverted != 0;
		memset(SFCurrentCode, 0, sizeof(SFCurrentCode));
		SFExecuted[0] = 0;
		for (unsigned s = 0; s < COUNTOF(rotated); ++s)
		{
			int dx = 0, dy = 0, tx = 0, ty = 0;
			button[0] = rotated[s].fire;
			rb_move_dir(rb_move_bits(rotated[s].dx, rotated[s].dy), &dx, &dy);
			SF_twiddleTarget(PX, PY, dx, dy, &tx, &ty);
			JE_SFCodes(1, PX, PY, tx, ty);
		}
		if (inverted)
			qa_check(SFExecuted[0] == SEEKER_BOMBS_SPECIAL,
			         "a rotated combo entered over the wire fires on an upside-down screen");
		else
			qa_check(SFExecuted[0] == 0,
			         "...and the same wire input on an upright screen is not that combo");
	}

	memcpy(button, savedButton, sizeof(button));
	memcpy(SFExecuted, savedExec, sizeof(SFExecuted));
	memcpy(SFCurrentCode, savedCode, sizeof(savedCode));
	player[0] = saved0;
	superTyrian = savedSuper;
	smoothies[9-1] = savedInvert;
}

/* Unexpected input clears a combo, except a repeated code or a fully released tick. */
void qa_test_twiddle_strictness(void)
{
	const Player saved0 = player[0];
	const JE_boolean savedSuper = superTyrian;
	JE_byte savedCode[2][21];
	JE_byte savedExec[2];
	bool savedButton[4];
	memcpy(savedCode, SFCurrentCode, sizeof(savedCode));
	memcpy(savedExec, SFExecuted, sizeof(savedExec));
	memcpy(savedButton, button, sizeof(savedButton));

	// USP Talon's first combo slot is Invulnerability: DOWN, UP, DOWN, then UP with fire held. Its
	// progress is SFCurrentCode[0][0], and the row's terminator names the special it sets off.
	superTyrian = false;  // SuperTyrian replaces the ship's rows with its own
	player[0].items.ship = 1;
	const JE_byte comboRow = (JE_byte)(shipCombos[1][0] - 1);
	const JE_byte want = (JE_byte)(keyboardCombos[comboRow][4] - 100);

	// A direction stays pressed across ticks, and the controls pass through neutral between two of
	// them. Neither costs the combo anything.
	qa_twiddle_clear();
	static const JE_byte held[] = { 2, 2, 2, 9, 1, 9, 9, 2, 9, 5 };
	for (unsigned k = 0; k < COUNTOF(held); ++k)
		qa_twiddle_step(1, held[k]);
	qa_check(SFExecuted[0] == want && want != 0,
	         "a twiddle completes through held directions and releases between steps");

	// Fire pressed one step early, on a step the combo wants bare.
	qa_twiddle_clear();
	qa_twiddle_step(1, 2);
	const JE_byte afterFirst = SFCurrentCode[0][0];
	qa_twiddle_step(1, 5);  // up, the next step, but with fire held
	qa_check(afterFirst == 1 && SFCurrentCode[0][0] == 0,
	         "starting to fire on a step that wants no fire loses the twiddle");

	// ...and fire let go on the last step, which is the only one that wants it.
	qa_twiddle_clear();
	static const JE_byte released[] = { 2, 1, 2 };
	for (unsigned k = 0; k < COUNTOF(released); ++k)
		qa_twiddle_step(1, released[k]);
	const JE_byte atLastStep = SFCurrentCode[0][0];
	qa_twiddle_step(1, 1);  // up, but no longer firing
	qa_check(atLastStep == 3 && SFCurrentCode[0][0] == 0 && SFExecuted[0] == 0,
	         "stopping fire on the step that wants it loses the twiddle");

	// Another direction loses it.
	qa_twiddle_clear();
	qa_twiddle_step(1, 2);
	qa_twiddle_step(1, 3);  // left, which the row never asks for
	qa_check(SFCurrentCode[0][0] == 0, "a direction the combo did not ask for loses it");

	memcpy(button, savedButton, sizeof(button));
	memcpy(SFExecuted, savedExec, sizeof(SFExecuted));
	memcpy(SFCurrentCode, savedCode, sizeof(savedCode));
	player[0] = saved0;
	superTyrian = savedSuper;
}

/* A twiddle keeps its own clock. */
void qa_test_twiddle_cooldown(void)
{
	if (VGAScreenSeg == NULL || game_screen == NULL)
		return;  // the charge path repaints the real shield and armor gauges

	const JE_byte savedStype = special[SPECIAL_NUM].stype;
	const JE_byte savedPwr = special[SPECIAL_NUM].pwr;
	const JE_byte savedExec = SFExecuted[0];
	const JE_byte savedRepeat = shotRepeat[SHOT_SPECIAL];
	const JE_byte savedTemp2 = temp2;
	const JE_boolean savedAuto = autoFireSpecial;
	const JE_boolean savedDbgAuto = debugAutofireTwiddle;
	const JE_boolean savedTrigger = debugTwiddleTrigger;
	const JE_byte savedDbgTwiddle = debugTwiddleSpecial;
	const JE_boolean savedEndless = endlessMode;
	const Player saved0 = player[0];
	SDL_Surface *const savedVGA = VGAScreen;

	// Use inert special type 0 so the test exercises only charging and deduction.
	special[SPECIAL_NUM].stype = 0;
	special[SPECIAL_NUM].pwr = 20;  // a fixed shield charge
	player[0].items.special = 0;
	autoFireSpecial = false;
	debugAutofireTwiddle = false;
	debugTwiddleTrigger = false;
	debugTwiddleSpecial = 0;
	endlessMode = false;
	endlessSetFxPlayer(0);

	// The equipped special is deep in a recharge. The twiddle must still go off.
	uint armor = 30, shield = 50;
	SFExecuted[0] = SPECIAL_NUM;
	specialWait = 0;
	flareDuration = 0;
	flareStart = false;
	zinglonDuration = 0;
	astralDuration = 0;
	JE_resetTwiddleClocks();
	shotRepeat[SHOT_SPECIAL] = 90;
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(shield == 30, "a twiddle fires while the equipped special is recharging");
	qa_check(shotRepeat[SHOT_SPECIAL] == 89,
	         "...and leaves the equipped special's recharge alone, bar the tick it just spent");

	// Its own clock then paces it: an immediate repeat is refused, and it comes back after.
	shield = 50;
	SFExecuted[0] = SPECIAL_NUM;
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(shield == 50, "a second twiddle inside the cooldown is refused");

	for (int tick = 0; tick < TWIDDLE_MIN_WAIT; ++tick)
	{
		SFExecuted[0] = 0;
		JE_doSpecialShot(1, &armor, &shield);
	}
	SFExecuted[0] = SPECIAL_NUM;
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(shield == 30, "...and fires again once that cooldown runs out");

	// An unaffordable charge takes nothing and leaves the bar it could not pay from intact.
	shield = 4;
	special[SPECIAL_NUM].pwr = 98;  // the whole shield bar
	SFExecuted[0] = SPECIAL_NUM;
	JE_resetTwiddleClocks();
	JE_doSpecialShot(1, &armor, &shield);
	const bool spentWhenPaid = shield == 0;
	shield = 3;
	SFExecuted[0] = SPECIAL_NUM;
	JE_resetTwiddleClocks();
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(spentWhenPaid && shield == 3,
	         "a twiddle nobody can pay for charges nothing at all");

	VGAScreen = savedVGA;
	player[0] = saved0;
	endlessMode = savedEndless;
	debugTwiddleSpecial = savedDbgTwiddle;
	debugTwiddleTrigger = savedTrigger;
	debugAutofireTwiddle = savedDbgAuto;
	autoFireSpecial = savedAuto;
	temp2 = savedTemp2;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	SFExecuted[0] = savedExec;
	special[SPECIAL_NUM].pwr = savedPwr;
	special[SPECIAL_NUM].stype = savedStype;
	JE_resetTwiddleClocks();
}

// One tick of the live flare inside JE_doSpecialShot, with every fire gate shut.
static void qa_flare_tick(void)
{
	uint armor = 30, shield = 50;
	SFExecuted[0] = 0;
	JE_doSpecialShot(1, &armor, &shield);
}

/* A flare may release only the full-screen grade it acquired. See
 * doc/notes.md#twiddles-and-specials. */
void qa_test_flare_grade_ownership(void)
{
	const JE_boolean savedActive = filterActive, savedFade = filterFade;
	const JE_boolean savedFadeStart = filterFadeStart, savedOwns = flareOwnsFilter;
	const JE_shortint savedFilter = levelFilter, savedBright = levelBrightness;
	const JE_shortint savedChg = levelBrightnessChg, savedColChg = flareColChg;
	const JE_shortint savedWpnFilter = specialWeaponFilter, savedFreq = specialWeaponFreq;
	const JE_byte savedWait = specialWait;
	const JE_byte savedNextWait = nextSpecialWait, savedRepeat = shotRepeat[SHOT_SPECIAL];
	const JE_byte savedZing = zinglonDuration, savedAstral = astralDuration;
	const JE_word savedFlare = flareDuration;
	const JE_boolean savedFlareStart = flareStart, savedLink = linkToPlayer;
	const JE_boolean savedAuto = autoFireSpecial, savedDbgAuto = debugAutofireTwiddle;
	const JE_boolean savedTrigger = debugTwiddleTrigger, savedEndless = endlessMode;
	const JE_byte savedDbgTwiddle = debugTwiddleSpecial, savedExec = SFExecuted[0];
	const Player saved0 = player[0];

	player[0].items.special = 0;
	autoFireSpecial = false;
	debugAutofireTwiddle = false;
	debugTwiddleTrigger = false;
	debugTwiddleSpecial = 0;
	endlessMode = false;
	endlessSetFxPlayer(0);
	JE_resetTwiddleClocks();
	specialWeaponFreq = 0;  // the flare spawns nothing
	specialWait = 0;
	nextSpecialWait = 0;
	linkToPlayer = false;
	zinglonDuration = 0;
	astralDuration = 0;

	// A level's white flash half way up, and a tint-less flare (MegaLaser, Missile Pod...) on its
	// last tick: the flash keeps ramping.
	filterActive = true; filterFade = true; filterFadeStart = true;
	levelFilter = -99; levelBrightness = 6; levelBrightnessChg = 1;
	flareOwnsFilter = false;
	specialWeaponFilter = -99;
	flareDuration = 1; flareStart = true;
	qa_flare_tick();
	qa_check(!flareStart && flareDuration == 0 && filterActive && filterFade
	         && levelFilter == -99 && levelBrightness == 6,
	         "a tint-less flare running out leaves a level's white flash to finish");

	// A level's own filter 7 (CORE's late red tint) with a Flare running: no pulse, no ownership,
	// and the tint outlives the flare.
	filterActive = true; filterFade = false;
	levelFilter = 7; levelBrightness = -99;
	flareOwnsFilter = false;
	specialWeaponFilter = 7;
	flareDuration = 5; flareStart = true;
	qa_flare_tick();
	const bool leftAlone = !flareOwnsFilter && filterActive && levelFilter == 7
	                    && levelBrightness == -99;
	flareDuration = 1;
	qa_flare_tick();
	qa_check(leftAlone && !flareStart && filterActive && levelFilter == 7 && levelBrightness == -99,
	         "a Flare over a level's own red tint neither pulses nor removes it");

	// An idle grade (the level's fade long finished): the Flare takes it, pulses it, and hands it
	// back when it runs out.
	filterActive = true; filterFade = false;
	levelFilter = -99; levelBrightness = -99;
	flareOwnsFilter = false;
	flareDuration = 5; flareStart = true;
	qa_flare_tick();
	const bool taken = flareOwnsFilter && filterActive && levelFilter == 7
	                && (levelBrightness == 1 || levelBrightness == -1);
	flareDuration = 1;
	qa_flare_tick();
	qa_check(taken && !flareStart && !flareOwnsFilter && !filterActive
	         && levelFilter == -99 && levelBrightness == -99,
	         "a Flare takes an idle grade, pulses it, and hands it back when it runs out");

	player[0] = saved0;
	SFExecuted[0] = savedExec;
	debugTwiddleSpecial = savedDbgTwiddle;
	endlessMode = savedEndless;
	debugTwiddleTrigger = savedTrigger;
	debugAutofireTwiddle = savedDbgAuto;
	autoFireSpecial = savedAuto;
	linkToPlayer = savedLink;
	flareStart = savedFlareStart;
	flareDuration = savedFlare;
	astralDuration = savedAstral;
	zinglonDuration = savedZing;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	nextSpecialWait = savedNextWait;
	specialWait = savedWait;
	specialWeaponFreq = savedFreq;
	specialWeaponFilter = savedWpnFilter;
	flareColChg = savedColChg;
	levelBrightnessChg = savedChg;
	levelBrightness = savedBright;
	levelFilter = savedFilter;
	flareOwnsFilter = savedOwns;
	filterFadeStart = savedFadeStart;
	filterFade = savedFade;
	filterActive = savedActive;
	JE_resetTwiddleClocks();
}

/* Zinglon width, refresh, damage scaling, and co-op ownership. */
void qa_test_zinglon_pillar(void)
{
	const JE_byte savedZing = zinglonDuration, savedRamp = zinglonRamp;
	const JE_byte savedStype = special[SPECIAL_NUM].stype;
	const JE_byte savedRepeat = shotRepeat[SHOT_SPECIAL];
	const JE_boolean savedEndless = endlessMode, savedMods = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	Player savedShips[2];
	memcpy(savedShips, player, sizeof(savedShips));
	int savedSalvo[2];
	memcpy(savedSalvo, endlessSalvoWindow, sizeof(savedSalvo));

	// Upstream drew the beam as `25 - abs(duration - 25)`, which the ramp has to reproduce exactly.
	bool stockCurve = true;
	for (int d = 50, ramp = 0; d > 1; --d)
	{
		stockCurve = stockCurve && zinglon_pillar_width(ramp, d) == 25 - abs(d - 25);
		if (ramp < ZINGLON_PILLAR_HALF_W)
			++ramp;
	}
	qa_check(stockCurve, "the Zinglon pillar traces vanilla's width curve over a stock blast");

	// A stretched one opens on the same ramp, holds every tick the stretch bought, then closes.
	int held = 0, lastWidth = -1;
	bool bounded = true;
	for (int d = 110, ramp = 0; d > 1; --d)
	{
		const int w = zinglon_pillar_width(ramp, d);
		bounded = bounded && w >= 0 && w <= ZINGLON_PILLAR_HALF_W;
		held += (w == ZINGLON_PILLAR_HALF_W);
		lastWidth = w;
		if (ramp < ZINGLON_PILLAR_HALF_W)
			++ramp;
	}
	qa_check(bounded && held == 110 - 2 * ZINGLON_PILLAR_HALF_W + 1 && lastWidth == 2,
	         "a stretched Zinglon blast spends its extra ticks open and still closes at the end");

	// The scratch slot takes Soul of Zinglon's shape: the pillar with no flare beside it.
	special[SPECIAL_NUM].stype = 3;
	endlessMode = false;
	endlessCampaignMods = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	zinglonDuration = 0;
	zinglonRamp = 0;
	JE_specialComplete(1, SPECIAL_NUM);
	const JE_byte stockTicks = zinglonDuration;

	// A blast onto a live beam refreshes the window and leaves the beam at the width it reached.
	zinglonRamp = ZINGLON_PILLAR_HALF_W;
	zinglonDuration = 5;
	JE_specialComplete(1, SPECIAL_NUM);
	const bool refireHolds = zinglonRamp == ZINGLON_PILLAR_HALF_W && zinglonDuration == stockTicks;

	// One fired with the beam spent opens from closed again.
	zinglonDuration = 1;
	zinglonRamp = ZINGLON_PILLAR_HALF_W;
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(stockTicks == 50 && refireHolds && zinglonRamp == 0,
	         "a Zinglon blast on a live beam refreshes it, and one fired cold opens from nothing");

	// Ordnance Reserves stretches the blast, as it already stretched the flare beside it.
	endlessMode = true;
	endlessPerkSetOwned(PERK_ORDNANCE, endlessPerkMaxStack(PERK_ORDNANCE));
	zinglonDuration = 0;
	zinglonRamp = 0;
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(zinglonDuration == endlessPerkSpecialDuration(50, 255) && zinglonDuration > stockTicks,
	         "Ordnance Reserves stretches the Zinglon blast");

	/* The pillar helper supplies collision data for the beam's reserved shot slot. */
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	memset(endlessSalvoWindow, 0, sizeof(endlessSalvoWindow));
	coopEndlessMode = false;
	endlessMode = false;
	player[0].x = 100;
	zinglonRamp = ZINGLON_PILLAR_HALF_W;
	zinglonDuration = 30;

	int width = 0, dmg = 0;
	uint owner = 9;
	const bool onBeam = zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	qa_check(onBeam && dmg == ZINGLON_PILLAR_DAMAGE && width == ZINGLON_PILLAR_HALF_W && owner == 0,
	         "outside an endless run the beam deals its stock damage, on the one ship");
	qa_check(!zinglon_pillar_hit(player[0].x + 7 + ZINGLON_PILLAR_HALF_W, &width, &dmg, &owner)
	         && dmg == 0,
	         "...and a hull past its edge takes nothing");

	// The pillar uses the same damage modifiers as player shots.
	endlessMode = true;
	endlessPerkSetOwned(PERK_DAMAGE, endlessPerkMaxStack(PERK_DAMAGE));
	zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	const int buffed = dmg;
	qa_check(buffed == endlessScaleOwnDamage(ZINGLON_PILLAR_DAMAGE, false)
	         && buffed > ZINGLON_PILLAR_DAMAGE,
	         "Heavy Rounds lifts the beam the way it lifts a bullet");
	endlessSalvoWindow[0] = ENDLESS_PERK_SALVO_WINDOW;
	endlessPerkSetOwned(PERK_SALVO, endlessPerkMaxStack(PERK_SALVO));
	zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	qa_check(dmg == endlessScaleOwnDamage(ZINGLON_PILLAR_DAMAGE, true) && dmg > buffed,
	         "...and a charged Opening Salvo window bumps it further");
	memset(endlessSalvoWindow, 0, sizeof(endlessSalvoWindow));
	endlessPerkSetOwned(PERK_SALVO, 0);
	endlessPerkSetOwned(PERK_DAMAGE, 0);

	/* Overlapping co-op beams use the stronger hit and its owner. */
	coopEndlessMode = true;
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].x = 100;
		player[p].zinglon_ramp = ZINGLON_PILLAR_HALF_W;
		player[p].zinglon_duration = 30;
	}
	endlessPerkTakenBy[1][PERK_DAMAGE] = endlessPerkMaxStack(PERK_DAMAGE);
	endlessPerkRederive();
	const bool pairHit = zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	qa_check(pairHit && owner == 1 && dmg == buffed,
	         "under two beams the hull takes the stronger one, billed to the ship behind it");
	player[1].zinglon_duration = 0;
	zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	qa_check(owner == 0 && dmg == ZINGLON_PILLAR_DAMAGE,
	         "...and with that beam spent the partner's own scale is all that is left");

	memcpy(endlessSalvoWindow, savedSalvo, sizeof(savedSalvo));
	memcpy(player, savedShips, sizeof(savedShips));
	coopEndlessMode = savedCoop;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessCampaignMods = savedMods;
	endlessMode = savedEndless;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	special[SPECIAL_NUM].stype = savedStype;
	zinglonRamp = savedRamp;
	zinglonDuration = savedZing;
}

/* No special outlives the level that fired it, and the debug fire helpers stay out of a demo.
 * Either one left alone puts the previous run's special into the title screen's demo. */
void qa_test_special_state_reset(void)
{
	if (VGAScreenSeg == NULL || game_screen == NULL)
		return;  // the live flare below spawns its shots

	const JE_byte savedStype = special[SPECIAL_NUM].stype, savedPwr = special[SPECIAL_NUM].pwr;
	const JE_byte savedZing = zinglonDuration, savedAstral = astralDuration;
	const JE_byte savedWait = specialWait, savedNextWait = nextSpecialWait;
	const JE_byte savedRepeat = shotRepeat[SHOT_SPECIAL], savedExec = SFExecuted[0];
	const JE_byte savedTemp2 = temp2, savedDbgTwiddle = debugTwiddleSpecial;
	const JE_word savedFlare = flareDuration;
	const JE_boolean savedFlareStart = flareStart, savedSpray = spraySpecial;
	const JE_boolean savedLink = linkToPlayer, savedTrigger = debugTwiddleTrigger;
	const JE_boolean savedAuto = autoFireSpecial, savedDbgAuto = debugAutofireTwiddle;
	const JE_boolean savedEndless = endlessMode, savedDemo = play_demo;
	const JE_shortint savedFilter = specialWeaponFilter, savedFreq = specialWeaponFreq;
	const JE_word savedWpn = specialWeaponWpn;
	const Player saved0 = player[0], saved1 = player[1];
	SDL_Surface *const savedVGA = VGAScreen;

	// A special of every shape is mid-flight: a flare spawning shots, a beam, a recharge, a
	// twiddle the menu armed but nobody consumed.
	zinglonDuration = 40;
	zinglonRamp = ZINGLON_PILLAR_HALF_W;
	astralDuration = 30;
	flareDuration = 200;
	flareStart = true;
	specialWait = 15;
	nextSpecialWait = 20;
	debugTwiddleTrigger = true;

	JE_resetSpecialState();
	qa_check(flareDuration == 0 && !flareStart && zinglonDuration == 0 && zinglonRamp == 0
	         && astralDuration == 0
	         && specialWait == 0 && nextSpecialWait == 0 && !debugTwiddleTrigger,
	         "level start clears every clock a fired special left running");

	// Two ships keep their own copy of those clocks and swap them in per ship, so the mirrors are
	// the other half of the barrier: only a dual-ship level start clears them.
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].flare_duration = 100;
		player[p].flare_start = true;
		player[p].zinglon_duration = 20;
		player[p].zinglon_ramp = ZINGLON_PILLAR_HALF_W;
		player[p].astral_duration = 20;
		player[p].special_wait = 10;
		player[p].next_special_wait = 10;
		player[p].spray_special = true;
	}
	coop_ship_runtime_reset();
	bool mirrorsIdle = true;
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		mirrorsIdle = mirrorsIdle && player[p].flare_duration == 0 && !player[p].flare_start
		           && player[p].zinglon_duration == 0 && player[p].zinglon_ramp == 0
		           && player[p].astral_duration == 0
		           && player[p].special_wait == 0 && player[p].next_special_wait == 0
		           && !player[p].spray_special;
	}
	qa_check(mirrorsIdle, "a dual-ship level start clears both ships' special mirrors");

	// The scratch slot takes the Minefield shape: a flare whose shots follow the ship. Clearing
	// the equipped special and Autofire Special leaves the debug twiddle as the only fire gate.
	special[SPECIAL_NUM].stype = 16;
	special[SPECIAL_NUM].pwr = 2;
	player[0].items.special = 0;
	autoFireSpecial = false;
	endlessMode = false;
	endlessSetFxPlayer(0);
	debugAutofireTwiddle = true;
	debugTwiddleSpecial = SPECIAL_NUM;
	button[0] = true;
	SFExecuted[0] = 0;
	shotRepeat[SHOT_SPECIAL] = 0;

	uint armor = 30, shield = 50;
	play_demo = true;
	JE_doSpecialShot(1, &armor, &shield);
	const bool demoStaysClean = flareDuration == 0;

	play_demo = false;
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(demoStaysClean && flareDuration > 0,
	         "the debug twiddle stays out of a demo and fires for a player at the controls");

	VGAScreen = savedVGA;
	player[0] = saved0;
	player[1] = saved1;
	play_demo = savedDemo;
	endlessMode = savedEndless;
	debugAutofireTwiddle = savedDbgAuto;
	autoFireSpecial = savedAuto;
	debugTwiddleTrigger = savedTrigger;
	debugTwiddleSpecial = savedDbgTwiddle;
	linkToPlayer = savedLink;
	spraySpecial = savedSpray;
	specialWeaponWpn = savedWpn;
	specialWeaponFreq = savedFreq;
	specialWeaponFilter = savedFilter;
	flareStart = savedFlareStart;
	flareDuration = savedFlare;
	temp2 = savedTemp2;
	SFExecuted[0] = savedExec;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	nextSpecialWait = savedNextWait;
	specialWait = savedWait;
	astralDuration = savedAstral;
	zinglonDuration = savedZing;
	special[SPECIAL_NUM].pwr = savedPwr;
	special[SPECIAL_NUM].stype = savedStype;
	button[0] = false;
	JE_resetTwiddleClocks();
}

static void qa_attract_item(int slot, int value, int x, int y)
{
	memset(&enemy[slot], 0, sizeof(enemy[slot]));
	enemyAvail[slot] = 2;
	enemy[slot].evalue = (JE_integer)value;
	enemy[slot].scoreitem = true;
	enemy[slot].ex = (JE_integer)x;
	enemy[slot].ey = (JE_integer)y;
	enemy[slot].exc = 50;
	enemy[slot].eyc = -40;
}

static bool qa_attract_step_closes(int before, int after)
{
	return after == 0 || ((after > 0) == (before > 0) && abs(after) <= abs(before));
}

void qa_test_endless_attractor(void)
{
	if (VGAScreenSeg == NULL || game_screen == NULL)
		return;

	static JE_MultiEnemyType savedEnemies;
	JE_EnemyAvailType savedAvail;
	memcpy(savedEnemies, enemy, sizeof(enemy));
	memcpy(savedAvail, enemyAvail, sizeof(enemyAvail));
	Player savedShips[2];
	memcpy(savedShips, player, sizeof(savedShips));
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	const JE_boolean savedEndless = endlessMode, savedMods = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode, savedTwo = twoPlayerMode;
	const JE_byte savedStype = special[SPECIAL_NUM].stype;

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	twoPlayerMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	special[SPECIAL_NUM].stype = 4;
	memset(enemyAvail, 1, sizeof(enemyAvail));
	player[0].is_alive = true;
	player[0].x = 150;
	player[0].y = 140;
	const long cashBefore = player[0].cash;

	qa_attract_item(0, 150, 20, 20);
	enemy[0].mapoffset = 10;
	qa_attract_item(1, -1, 270, 180);
	qa_attract_item(2, 1, 150, 40);
	qa_attract_item(3, 10500, 60, 60);
	qa_attract_item(4, 32105, 90, 60);
	qa_attract_item(5, 0, 120, 60);
	enemy[5].scoreitem = false;
	enemyAvail[5] = 0;
	JE_specialComplete(1, SPECIAL_NUM);

	qa_check(enemy[0].attractedBy == 1 && enemy[1].attractedBy == 1 &&
	         enemy[0].exc == 0 && enemy[0].eyc == 0,
	         "the Endless Attractor locks each collectible pickup onto the ship and stops it");
	bool skipped = true;
	for (int s = 2; s <= 5; ++s)
		skipped = skipped && enemy[s].attractedBy == 0;
	qa_check(skipped, "...but leaves special pickups, weapon balls and enemies where they are");

	bool closing = true;
	for (int tick = 0; tick < 40; ++tick)
	{
		if (tick < 12)
			player[0].x += 5;

		int dx[2], dy[2];
		for (int s = 0; s < 2; ++s)
		{
			dx[s] = player[0].x - (enemy[s].ex + enemy[s].mapoffset);
			dy[s] = player[0].y - enemy[s].ey;
		}
		endlessAttractorTick();
		for (int s = 0; s < 2; ++s)
		{
			const int nowX = player[0].x - (enemy[s].ex + enemy[s].mapoffset);
			const int nowY = player[0].y - enemy[s].ey;
			closing = closing && qa_attract_step_closes(dx[s], nowX) &&
			          qa_attract_step_closes(dy[s], nowY);
		}
	}
	bool docked = true;
	for (int s = 0; s < 2; ++s)
		docked = docked && enemy[s].ex + enemy[s].mapoffset == player[0].x &&
		         enemy[s].ey == player[0].y;
	qa_check(closing && docked,
	         "...hauls them after a moving ship, never carrying one past it, and docks them on it");
	qa_check(enemy[2].ex == 150 && enemy[3].ex == 60 && enemy[4].ex == 90 && enemy[5].ex == 120,
	         "...while the pickups it skipped stay put");

	JE_playerCollide(&player[0], 1);
	qa_check(enemyAvail[0] == 1 && enemyAvail[1] == 1 && player[0].cash > cashBefore,
	         "...where the ship collects every one of them");

	qa_attract_item(6, 150, 30, 30);
	endlessAttractorFire(0, 1);
	player[0].is_alive = false;
	endlessAttractorTick();
	qa_check(enemy[6].attractedBy == 0 && enemy[6].ex == 30 && enemy[6].ey == 30,
	         "a pickup whose ship goes down is let go where it is");

	player[0].is_alive = true;
	qa_attract_item(7, 150, 30, 30);
	endlessAttractorFire(0, 3);
	const JE_byte salvoStart = enemy[7].attractSpeed;
	for (int tick = 0; tick < ENDLESS_ATTRACT_SPEED_MAX; ++tick)
		endlessAttractorTick();
	qa_check(salvoStart == ENDLESS_ATTRACT_SPEED_START * 3 &&
	         enemy[7].attractSpeed == ENDLESS_ATTRACT_SPEED_MAX,
	         "Opening Salvo starts the haul three times faster; every haul tops out at one speed");

	coopEndlessMode = true;
	twoPlayerMode = true;
	player[1].is_alive = true;
	player[1].x = 60;
	player[1].y = 150;
	qa_attract_item(8, 150, 200, 30);
	JE_specialComplete(2, SPECIAL_NUM);
	qa_check(enemy[8].attractedBy == 2, "in co-op the pickups go to the ship that fired");

	endlessMode = false;
	coopEndlessMode = false;
	twoPlayerMode = false;
	memset(enemyAvail, 1, sizeof(enemyAvail));
	qa_attract_item(9, 150, 100, 100);
	enemy[9].exc = 0;
	enemy[9].eyc = 0;
	JE_specialComplete(1, SPECIAL_NUM);
	const bool stockNudge = enemy[9].attractedBy == 0 && enemy[9].exc == 1 && enemy[9].eyc == 1;
	enemy[9].attractedBy = 1;
	endlessAttractorTick();
	qa_check(stockNudge && enemy[9].ex == 100 && enemy[9].ey == 100,
	         "outside Endless the Attractor keeps its stock nudge and no haul runs");

	special[SPECIAL_NUM].stype = savedStype;
	twoPlayerMode = savedTwo;
	coopEndlessMode = savedCoop;
	endlessCampaignMods = savedMods;
	endlessMode = savedEndless;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	memcpy(player, savedShips, sizeof(savedShips));
	memcpy(enemyAvail, savedAvail, sizeof(enemyAvail));
	memcpy(enemy, savedEnemies, sizeof(enemy));
}

void qa_test_endless_special_names(void)
{
	const JE_boolean savedEndless = endlessMode, savedMods = endlessCampaignMods;

	int microbomb = 0, pearlShot = 0, pearlField = 0;
	for (int id = 1; id < SPECIAL_NUM; ++id)
	{
		if (strncmp(special[id].name, "8-Way Microbomb", 15) == 0)
			microbomb = id;
		else if (strncmp(special[id].name, "Pearl Wind", 10) == 0)
			*(special[id].stype == 1 ? &pearlShot : &pearlField) = id;
	}

	endlessMode = false;
	endlessCampaignMods = false;
	const bool stock = microbomb > 0 && pearlShot > 0 &&
	                   JE_specialName((JE_byte)microbomb) == special[microbomb].name &&
	                   JE_specialName((JE_byte)pearlShot) == special[pearlShot].name;
	endlessMode = true;
	qa_check(stock && strcmp(JE_specialName((JE_byte)microbomb), "Microbomb") == 0,
	         "Endless calls the 8-Way Microbomb what it fires, a Microbomb");
	qa_check(strcmp(JE_specialName((JE_byte)pearlShot), "Pearl Shot") == 0 &&
	         (pearlField == 0 || JE_specialName((JE_byte)pearlField) == special[pearlField].name),
	         "...still names the instant Pearl Wind a Pearl Shot, and leaves the field one alone");

	endlessCampaignMods = savedMods;
	endlessMode = savedEndless;
}
