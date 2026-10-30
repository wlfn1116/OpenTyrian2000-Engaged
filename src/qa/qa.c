/* Project-owned unit, property, serialization, and replay tests. */
#define ENDLESS_INTERNAL_ACCESS
#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "custom_episode.h"
#include "custom_weapon.h"
#include "demo.h"
#include "destruct.h"
#include "destruct_rollback.h"
#include "endless.h"
#include "episodes.h"
#include "endless_internal.h"
#include "extra_ships.h"
#include "game_loop.h"
#include "game_state.h"
#include "keyboard.h"
#include "level_load.h"
#include "mtrand.h"
#include "net_savexfer.h"
#include "network.h"
#include "player.h"
#include "rollback.h"
#include "video.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qa_test_suite = false;
const char *qa_fixture_dir = "testing/fixtures";
int qa_replay_demo = 0;
unsigned long qa_replay_ticks = 0;
unsigned long qa_destruct_selftest_ticks = 0;
bool qa_replay_expect_set = false;
Uint32 qa_replay_expect = 0;
int qa_replay_chain = 0;
const char *qa_replay_file = NULL;
bool qa_replay_coop = false;
unsigned int qa_replay_depth = 1;
const char *qa_replay_trace = NULL;
Uint32 qa_replay_seed = 1;
int qa_replay_detail = 0;
int qa_net_rounds = 0;
int qa_net_scenario = 0;
int qa_net_version_skew = 0;
unsigned long qa_net_gameplay_ticks = 0;
unsigned long qa_net_delay_frames = 0;
unsigned long qa_net_special_flashes = 0;
unsigned long qa_net_corrupt_frame = 0;
bool qa_net_save_exit = false;
int qa_net_resume_slot = 0;
int qa_net_loadout = 0;
unsigned long qa_net_menu_frame = 0;
int qa_net_game_type = -1;
bool qa_net_outpost_quit = false;
int qa_net_disconnect_save = 0;
const char *qa_xfer_send = NULL;
const char *qa_xfer_recv = NULL;
bool qa_xfer_auto = false;
const char *qa_xfer_host = NULL;
bool qa_xfer_push = false;

/* Runs one Transfer-menu wire path without interactive menus. */
int qa_run_xfer(void)
{
	const char *const which = qa_xfer_send != NULL ? qa_xfer_send : qa_xfer_recv;
	const bool sending = qa_xfer_send != NULL;
	if (which == NULL)
		return 2;

	customEpisodeScan();
	const int before = customEpisodeCount();

	static const struct
	{
		const char *name;
		void (*send)(void);
		bool (*recv)(void);
	} kinds[] = {
		{ "levels",  levelsXferUpload,  levelsXferDownload  },
		{ "custom",  customXferUpload,  customXferDownload  },
		{ "all",     allXferUpload,     allXferDownload     },
		{ "ships",   shipsXferUpload,   shipsXferDownload   },
		{ "weapons", weaponsXferUpload, weaponsXferDownload },
		{ "saves",   savesXferUpload,   savesXferDownload   },
	};

	for (size_t i = 0; i < COUNTOF(kinds); ++i)
	{
		if (SDL_strcasecmp(which, kinds[i].name) != 0)
			continue;

		bool ok = true;
		if (sending)
			kinds[i].send();
		else
			ok = kinds[i].recv();

		customEpisodeScan();
		fprintf(stderr, "xfer test: %s %s %s containers %d -> %d\n",
		        sending ? "send" : "recv", kinds[i].name, ok ? "ok" : "FAILED",
		        before, customEpisodeCount());
		fflush(stderr);
		return ok ? 0 : 1;
	}

	fprintf(stderr, "xfer test: unknown transfer kind '%s'\n", which);
	return 2;
}

int qa_net_custom_endless = -1;
const char *qa_net_custom_episode = NULL;
int qa_net_zones = 0;
int qa_net_zones_cleared = 0;
bool qa_net_lobby_settings = false;
bool qa_net_arcade_separate = false;
bool qa_net_scrollock = false;
bool qa_net_guest_esc = false;

/* Deterministic modifier slates for the first ten Endless wire-test zones.
 * Later zones fly unmodified. */
static Uint64 qa_net_zone_mods(int depth)
{
	static const Uint64 slate[] = {
		ENDLESS_MOD_TOPSY | ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_GRAVITY,
		ENDLESS_MOD_APEX | ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_BOUNTY,
		ENDLESS_MOD_LEGION | ENDLESS_MOD_SEEKER | ENDLESS_MOD_RETALIATION,
		ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT
			| ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_OVERCHARGE,
		ENDLESS_MOD_NOELITE | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_GRAVITY_OMNI
			| ENDLESS_MOD_STATIC | ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_CONCUSSION,
		ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_DILATION
			| ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_ENRAGE,
		ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_SLIPSTREAM
			| ENDLESS_MOD_AEGIS | ENDLESS_MOD_LOWPROFILE,
		ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_WARP
			| ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_DEADGEN,
		ENDLESS_MOD_MISFIRE | ENDLESS_MOD_HOMING | (Uint64)ENDLESS_MOD_RAMPAGE
			| ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_FAVOR,
		ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_CURSED | ENDLESS_MOD_MARKED | ENDLESS_MOD_NITRO
			| ENDLESS_MOD_OVERHEAT | ENDLESS_MOD_DUD
			| ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_BREAKTHROUGH,
	};

	if (depth < 0 || depth >= (int)COUNTOF(slate))
		return 0;
	return slate[depth];
}

void qa_net_grant_perk_offer(uint p, int offer)
{
	endlessPerkGrant(p, endlessPerkChoice[offer], 1);
}

void qa_net_grant_wire_perks(uint p)
{
	endlessPerkGrant(p, PERK_SURVEYOR, endlessPerkMaxStack(PERK_SURVEYOR));
	endlessPerkGrant(p, PERK_EXECUTIONER, endlessPerkMaxStack(PERK_EXECUTIONER));
}

void qa_net_force_course_mods(int course)
{
	endlessCourseMod[course] = qa_net_zone_mods(endlessRunDepth);
}

// First sidekick whose mount style matches, for the wire tests' loadout profiles. Styles:
// 0 side pod, 1/3 trailing companion, 2 front pod, 4 orbiting satellite.
static JE_byte qa_first_sidekick_with_tr(JE_byte tr)
{
	for (uint i = 1; i <= OPTION_NUM; ++i)
		if (options[i].tr == tr && options[i].name[0] != '\0')
			return (JE_byte)i;
	return 0;
}

static JE_byte qa_first_sidekick_with_ammo(void)
{
	for (uint i = 1; i <= OPTION_NUM; ++i)
		if (options[i].ammo > 0 && options[i].name[0] != '\0')
			return (JE_byte)i;
	return 0;
}

static JE_byte qa_first_sidekick_with_charge(void)
{
	for (uint i = 1; i <= OPTION_NUM; ++i)
		if (options[i].pwr > 0 && options[i].name[0] != '\0')
			return (JE_byte)i;
	return 0;
}

/* Give the linked-arcade wire test an instant special. Holding fire releases it once, then the
 * ready light must retain that fired edge across player two's shared-special pass. */
void qa_net_apply_linked_special(void)
{
	for (uint i = 1; i <= SPECIAL_NUM; ++i)
	{
		if (special[i].stype == 2)  // Repulsor
		{
			player[0].items.special = (JE_byte)i;
			return;
		}
	}
}

/* Sidekick mount combinations for the gameplay wire tests, applied identically on both
 * machines. */
void qa_net_apply_loadout(int profile)
{
	const JE_byte side  = qa_first_sidekick_with_tr(0);
	const JE_byte trail = qa_first_sidekick_with_tr(1);
	const JE_byte front = qa_first_sidekick_with_tr(2);
	const JE_byte chase = qa_first_sidekick_with_tr(3);
	const JE_byte sat   = qa_first_sidekick_with_tr(4);

	switch (profile)
	{
	case 1:
		player[0].items.sidekick[0] = front;
		player[0].items.sidekick[1] = side;
		player[1].items.sidekick[0] = trail;
		player[1].items.sidekick[1] = trail;
		break;
	case 2:
		player[0].items.sidekick[0] = front;
		player[0].items.sidekick[1] = front;
		player[1].items.sidekick[0] = sat;
		player[1].items.sidekick[1] = chase ? chase : trail;
		break;
	case 3:
		player[0].items.sidekick[0] = sat;
		player[0].items.sidekick[1] = sat;
		player[1].items.sidekick[0] = chase ? chase : trail;
		player[1].items.sidekick[1] = front;
		break;
	case 4:
	{
		/* Mix ammo, charge, custom, and satellite sidekicks to exercise rollback of
		 * player[].sidekick counters and compiled owner slots. */
		const JE_byte ammoKick = qa_first_sidekick_with_ammo();
		const JE_byte chargeKick = qa_first_sidekick_with_charge();
		JE_byte customKick = 0;

		customWeaponNetPrepare();
		Uint8 *const stream = malloc(CUSTOM_WEAPON_WIRE_MAX);
		if (stream != NULL)
		{
			const size_t total = customWeaponSerializeDesign(stream, CUSTOM_WEAPON_WIRE_MAX);
			if (total > 0 && customWeaponAdoptDesign(1, stream, total)
			    && customSidekickOwnerSlot[1] > 0)
				customKick = (JE_byte)customSidekickOwnerSlot[1];
			free(stream);
		}

		player[0].items.sidekick[0] = ammoKick ? ammoKick : side;
		player[0].items.sidekick[1] = chargeKick ? chargeKick : trail;
		player[1].items.sidekick[0] = customKick ? customKick : sat;
		player[1].items.sidekick[1] = sat;
		break;
	}
	default:
		return;
	}

	fprintf(stderr, "net gameplay: loadout %d (sidekicks %u+%u vs %u+%u)\n", profile,
	        player[0].items.sidekick[0], player[0].items.sidekick[1],
	        player[1].items.sidekick[0], player[1].items.sidekick[1]);
	fflush(stderr);
}
bool qa_fast_forward = false;

static unsigned qa_checks;
static unsigned qa_failures;

void qa_check(bool okay, const char *what)
{
	++qa_checks;
	if (!okay)
	{
		++qa_failures;
		fprintf(stderr, "not ok %u - %s\n", qa_checks, what);
	}
}

static void qa_test_any_button_latch(void)
{
	newkey = true;
	newmouse = false;
	const bool keyEdge = JE_anyButton();

	newkey = false;
	newmouse = true;
	const bool mouseEdge = JE_anyButton();

	newkey = newmouse = false;
	qa_check(keyEdge && mouseEdge,
	         "an any-button wait preserves key and mouse edges seen by an earlier event pump");
}

Uint32 qa_prng(Uint32 *state)
{
	Uint32 x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static Uint8 qaRngStash[4096];

void qa_rng_save(void)
{
	qa_check(mt_state_size() <= sizeof(qaRngStash), "the gameplay RNG state fits its QA stash");
	if (mt_state_size() <= sizeof(qaRngStash))
		mt_state_save(qaRngStash);
}

void qa_rng_restore(void)
{
	if (mt_state_size() <= sizeof(qaRngStash))
		mt_state_restore(qaRngStash);
}

static void qa_test_rollback(void)
{
	rollback_register_all();
	const Uint32 before = rollback_state_hash();
	const int saved_x = player[0].x;

	rollback_snapshot(0x5141u);
	player[0].x ^= 0x55;
	qa_check(rollback_restore(0x5141u) && player[0].x == saved_x
	         && rollback_state_hash() == before,
	         "rollback snapshot restores the registered state exactly");

	const size_t state_size = rollback_state_size();
	Uint8 *wire = malloc(state_size);
	qa_check(wire != NULL, "rollback wire buffer allocation succeeds");
	if (wire != NULL)
	{
		const bool exported = rollback_wire_export(wire);
		player[0].x ^= 0x33;
		qa_check(exported && rollback_wire_adopt(wire) && player[0].x == saved_x
		         && rollback_state_hash() == before,
		         "wire snapshot export/adopt restores canonical state");
		free(wire);
	}

	printf("# rollback registry: %lu bytes, layout %08x\n",
	       (unsigned long)state_size, (unsigned)rollback_layout_fingerprint());
}

static void qa_test_rollback_wire_seats(void)
{
	static const struct
	{
		bool campaign, endless, separate;
		const char *what;
	}
	modes[] =
	{
		{ false, false, false, "desync recovery rebinds the linked pair's lives and ship banks" },
		{ true,  false, false, "desync recovery rebinds online Campaign's lives and ship banks" },
		{ false, true,  false, "desync recovery rebinds online Endless's lives and ship banks" },
		{ false, false, true,  "desync recovery rebinds Separate arcade's lives and ship banks" },
	};

	const bool savedNet = isNetworkGame;
	const JE_boolean savedTwo = twoPlayerMode;
	const JE_boolean savedCampaign = coopCampaignMode, savedEndless = coopEndlessMode;
	const JE_boolean savedSeparate = arcadeSeparateMode;
	Sprite2_array *const savedSheet = shipGrPtr, *const savedSheet2 = shipGr2ptr;
	const size_t state_size = rollback_state_size();
	Uint8 *wire = malloc(state_size);

	isNetworkGame = true;
	twoPlayerMode = true;
	for (size_t m = 0; m < COUNTOF(modes) && wire != NULL; ++m)
	{
		coopCampaignMode = modes[m].campaign;
		coopEndlessMode = modes[m].endless;
		arcadeSeparateMode = modes[m].separate;
		player_bind_lives();
		shipGrPtr = extraShapesFor(0);
		shipGr2ptr = extraShapesFor(1);

		const Uint32 before = rollback_state_hash();
		const bool exported = rollback_wire_export(wire);
		player[1].lives = NULL;
		shipGrPtr = shipGr2ptr = NULL;
		const bool adopted = exported && rollback_wire_adopt(wire);

		bool bound = before != 0;
		for (uint i = 0; i < COUNTOF(player); ++i)
			bound = bound && player[i].lives == player_lives_counter(i);
		qa_check(adopted && bound && shipGrPtr == extraShapesFor(0)
		         && shipGr2ptr == extraShapesFor(1) && rollback_state_hash() == before,
		         modes[m].what);
	}
	free(wire);

	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedEndless;
	arcadeSeparateMode = savedSeparate;
	shipGrPtr = savedSheet;
	shipGr2ptr = savedSheet2;
	player_bind_lives();
}

int qa_run_unit_suite(void)
{
	qa_checks = qa_failures = 0;
	printf("TAP version 13\n");

	// Test transfer before normal episode setup, matching a fresh title screen.
	qa_test_save_transfer_preinit();
	qa_test_any_button_latch();

	/* Mirror normal episode setup before testing item, ship, weapon, and sidekick invariants. */
	JE_loadItemDat();
	JE_initPlayerData();
	qa_test_config_option_removal();
	qa_test_rework_config();
	qa_test_rollback();
	qa_test_rollback_wire_seats();
	qa_test_course_tables();
	qa_test_canonical_mods();
	qa_test_structural_rng();
	qa_test_perk_registry();
	qa_test_record_readers();
	qa_test_weapon_editor();
	qa_test_custom_weapon_wire();
	qa_test_fixed_pool_layout();
	qa_test_save_record_wire();
	qa_test_save_transfer();
	qa_test_load_screen_help();
	qa_test_extra_ship_return();
	qa_test_save_slot_seats();
	qa_test_cash_ledger();
	qa_test_bounty_matrix();
	qa_test_score_pickup_multiplier();
	qa_test_zone_payout();
	qa_test_arcade_scaling();
	qa_test_arcade_matrices();
	qa_test_sidekick_rollback_state();
	qa_test_gauge_flash_lifetime();
	qa_test_special_icon_tops();
	qa_test_flying_punch_bolt();
	qa_test_dragonwing_row();
	qa_test_wide_hull_columns();
	qa_test_dual_mode_tag();
	qa_test_weapon_bay_tags();
	qa_test_ship_editor_file();
	qa_test_ship_skins();
	qa_test_ship_specs();
	qa_test_special_light_events();
	qa_test_partner_repair_special();
	qa_test_twiddle_ships();
	qa_test_twiddle_diagonals();
	qa_test_twiddle_wire();
	qa_test_twiddle_strictness();
	qa_test_twiddle_cooldown();
	qa_test_twiddle_charges();
	qa_test_flare_grade_ownership();
	qa_test_zinglon_pillar();
	qa_test_special_state_reset();
	qa_test_endless_attractor();
	qa_test_attractor_targets();
	qa_test_custom_ship_equip();
	qa_test_endless_special_names();
	qa_test_modifier_online_parity();
	qa_test_effect_gates();
	qa_test_shot_hitboxes();
	qa_test_shot_exit_margin();
	qa_test_shot_shadow_edge();
	qa_test_guidance_perk();
	qa_test_zica_buff_fire();
	qa_test_orange_shield_rework();
	qa_test_surveyor_drop_perk();
	qa_test_twin_pods_perk();
	qa_test_reinforced_prow_perk();
	qa_test_ramming_rework();
	qa_test_knife_fight_perk();
	qa_test_deflector_perk();
	qa_test_executioner_perk();
	qa_test_afterburner_perk();
	qa_test_concussion_mod();
	qa_test_boss_scroll_hold();
	qa_test_gravity_pull_banking();
	qa_test_countermeasure_burst();
	qa_test_guided_screen_aim();
	qa_test_health_bar_scale();
	qa_test_vulnerable_cue();
	qa_test_elite_tier_eligibility();
	qa_test_homing_chaser_eligibility();
	qa_test_elite_explosion_tint();
	qa_test_elite_shot_tint();
	qa_test_elite_message_tint();
	qa_test_superspark_caps();
	qa_test_superspark_discarded_pass();
	qa_test_superspark_rng_cost();
	qa_test_superspark_brief();
	qa_test_superspark_seeded_spread();
	qa_test_superspark_shapes();
	qa_test_spark_ring_bounds();
	qa_test_vaporised_shot_sparks();
	qa_test_knife_fight_blood();
	qa_test_online_ship_style();
	qa_test_partner_hp_bars();
	qa_test_network_settings();
	qa_test_desync_guards();
	qa_test_network_endless_lobby();
	qa_test_discover_reply();
	qa_test_seeker_tiers();
	qa_test_endless_coop();
	qa_test_kill_fire_drives();
	qa_test_kill_fire_wiring();
	qa_test_kinetic_converter();
	qa_test_coop_combo_and_pickups();
	qa_test_peer_left_level();
	qa_test_peer_idle_rule();
	qa_test_menu_claim();
	qa_test_online_suite();
	qa_test_endless_shop_stock();
	qa_test_endless_suite();
	qa_test_save_fixtures();
	qa_test_present_paths();
	qa_test_scaled_sprite_blocks();
	qa_test_scaled_table_sprites();
	qa_test_resync_serialization();
	qa_test_input_record_pull();
	qa_test_courses();
	qa_test_finale_mods();
	qa_test_course_base_rule();
	qa_test_course_shuffle_rule();
	qa_test_course_reroll();
	qa_test_course_seat_parity();
	qa_test_course_reroll_dodge();
	qa_test_level_event_scripts();
	qa_test_boosted_boss_pacing();
	qa_test_item_data_settings();
	qa_test_zica_lv11_pattern();
	qa_test_firing_sound_levels();
	qa_test_sfx_filter();
	qa_test_custom_audio_files();
	qa_test_custom_music();
	qa_test_custom_sounds();
	qa_test_controller_hotplug();
	qa_test_swar_pixel_loops();
	qa_test_background_bands();
	qa_test_filter_subrows();
	qa_test_flat_bands();
	qa_test_png_codec();
#ifdef WITH_MIDI
	qa_test_lds_midi_detune();
#endif
	qa_test_custom_episode();
	qa_test_custom_endless_pool();
	qa_test_enhancement_presets();  // keep last: it leaves the enhancement settings where it put them

	printf("1..%u\n", qa_checks);
	printf("# %u checks, %u failures\n", qa_checks, qa_failures);
	return qa_failures == 0 ? 0 : 1;
}

static Uint32 qa_coop_rng;
static int qa_coop_target_x, qa_coop_target_y, qa_coop_hold;

static Uint32 qa_coop_next(Uint32 range)
{
	qa_coop_rng = qa_coop_rng * 1664525u + 1013904223u;
	return (qa_coop_rng >> 8) % range;
}

void qa_replay_coop_arm(void)
{
	if (!qa_replay_coop)
		return;

	player[1].items = player[0].items;
	player[1].last_items = player[0].last_items;
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		player[i].is_dragonwing = false;
		player[i].lives = player_lives_counter(i);
	}
}

bool qa_replay_coop_input(uint player_index, int x, int y, RbInput *in)
{
	if (!qa_replay_coop || player_index != 1)
		return false;

	if (qa_coop_hold-- <= 0)
	{
		const int span_x = PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN - SHIP_LEFT_MARGIN + 1;
		const int span_y = SHIP_BOTTOM_MARGIN - SHIP_TOP_MARGIN + 1;
		qa_coop_target_x = SHIP_LEFT_MARGIN + (int)qa_coop_next((Uint32)span_x);
		qa_coop_target_y = SHIP_TOP_MARGIN + (int)qa_coop_next((Uint32)span_y);
		qa_coop_hold = 10 + (int)qa_coop_next(50);
	}

	const int dx = MAX(-4, MIN(4, qa_coop_target_x - x));
	const int dy = MAX(-4, MIN(4, qa_coop_target_y - y));

	memset(in, 0, sizeof(*in));
	in->x = (Sint16)(x + dx);
	in->y = (Sint16)(y + dy);
	in->velX = (Sint16)(dx / 2);
	in->velY = (Sint16)(dy / 2);
	in->accelX = (Sint16)((dx > 0) - (dx < 0));
	in->accelY = (Sint16)((dy > 0) - (dy < 0));
	in->buttons = (qa_coop_next(10) != 0 ? RB_BTN_FIRE : 0)
	            | (qa_coop_next(4) == 0 ? RB_BTN_LSIDEKICK : 0)
	            | (qa_coop_next(4) == 0 ? RB_BTN_RSIDEKICK : 0)
	            | (qa_coop_next(97) == 0 ? RB_BTN_CHANGEFIRE : 0);
	return true;
}

int qa_run_replay_fixture(void)
{
	if (qa_replay_demo < 1 || qa_replay_demo > 5 || qa_replay_ticks == 0)
	{
		fprintf(stderr, "replay test requires demo 1..5 and a positive tick bound\n");
		return 2;
	}

	JE_initPlayerData();
	if (qa_replay_detail >= 1 && qa_replay_detail <= 6)
	{
		processorType = (JE_byte)qa_replay_detail;
		JE_initProcessorType();
	}
	qa_coop_rng = qa_replay_seed;
	qa_coop_hold = 0;
	if (qa_replay_coop)
	{
		twoPlayerMode = true;
		coopCampaignMode = true;
	}
	gameLoaded = false;
	jumpSection = false;
	stopped_demo = false;
	play_demo = true;
	demo_num = (Uint8)(qa_replay_demo - 1);
	qa_fast_forward = true;

	/* Arm Endless effects so campaign demos exercise the Chain Reaction queue. */
	if (qa_replay_chain != 0)
	{
		endlessCampaignMods = true;
		rollback_selftest_allow_endless(true);
		/* 2 arms the effects without the perk, as the control for whatever 1 reports. */
		if (qa_replay_chain == 1)
		{
			endlessPerkGrant(0, PERK_CHAINRXN, endlessPerkTable[PERK_CHAINRXN].maxStack);

			/* Use one fixed and one timed damage modifier to exercise pulse scaling. */
			endlessPerkGrant(0, PERK_DAMAGE, endlessPerkTable[PERK_DAMAGE].maxStack);
			endlessPlayerMods[0] |= ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_DMGUP;
		}
	}

	FILE *const trace = qa_replay_trace != NULL ? fopen(qa_replay_trace, "w") : NULL;
	if (qa_replay_trace != NULL && trace == NULL)
	{
		fprintf(stderr, "cannot write replay trace %s\n", qa_replay_trace);
		return 2;
	}

	rollback_selftest_ticks = 0;
	rollback_selftest_failures = 0;
	rollback_selftest_set_limit(qa_replay_ticks);
	rollback_selftest_set_depth(qa_replay_depth);
	rollback_selftest_set_trace(trace);
	rollback_selftest_set(true);
	JE_main();
	rollback_selftest_set(false);
	rollback_selftest_set_trace(NULL);
	if (trace != NULL)
		fclose(trace);

	const Uint32 hash = rollback_selftest_bounded_hash();
	printf("REPLAY demo=%d ticks=%lu failures=%lu hash=%08x layout=%08x\n",
	       qa_replay_demo, rollback_selftest_ticks, rollback_selftest_failures,
	       (unsigned)hash, (unsigned)rollback_layout_fingerprint());

	if (rollback_selftest_ticks != qa_replay_ticks || rollback_selftest_failures != 0)
		return 1;
	if (qa_replay_expect_set && hash != qa_replay_expect)
	{
		fprintf(stderr, "replay hash mismatch: expected %08x, got %08x\n",
		        (unsigned)qa_replay_expect, (unsigned)hash);
		return 1;
	}
	return 0;
}

/* Replay every headless Destruct frame from its snapshot to audit rollback coverage. */
int qa_run_destruct_selftest(void)
{
	if (qa_destruct_selftest_ticks == 0)
	{
		fprintf(stderr, "destruct self-test requires a positive tick bound\n");
		return 2;
	}

	JE_initPlayerData();
	JE_destructGame();

	size_t probeRaw = 0, probeComp = 0;
	drb_selftest_resync_bytes(&probeRaw, &probeComp);

	printf("DESTRUCT ticks=%lu failures=%lu resync=%s raw=%lu compressed=%lu chunks=%lu\n",
	       drb_selftest_ticks_run(), drb_selftest_failures(),
	       drb_selftest_resync_ok() ? "ok" : "FAILED",
	       (unsigned long)probeRaw, (unsigned long)probeComp,
	       probeComp == 0 ? 0UL : (unsigned long)((probeComp + 12 + 307) / 308));

	if (drb_selftest_ticks_run() != qa_destruct_selftest_ticks)
	{
		fprintf(stderr, "destruct self-test ran %lu of %lu ticks\n",
		        drb_selftest_ticks_run(), qa_destruct_selftest_ticks);
		return 1;
	}
	if (!drb_selftest_resync_ok())
	{
		fprintf(stderr, "destruct desync recovery: the battle did not survive its own "
		                "compress/expand round trip\n");
		return 1;
	}
	return drb_selftest_failures() == 0 ? 0 : 1;
}
