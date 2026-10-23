#ifndef QA_INTERNAL_H
#define QA_INTERNAL_H

#include "opentyr.h"

Uint32 qa_prng(Uint32 *state);

void qa_test_course_tables(void);
void qa_test_canonical_mods(void);
void qa_test_structural_rng(void);
void qa_test_perk_registry(void);
void qa_test_record_readers(void);
void qa_test_seeker_tiers(void);
void qa_test_courses(void);
void qa_test_finale_mods(void);
void qa_test_course_base_rule(void);
void qa_test_course_shuffle_rule(void);
void qa_test_course_reroll(void);
void qa_test_course_seat_parity(void);
void qa_test_course_reroll_dodge(void);
void qa_test_custom_endless_pool(void);

void qa_test_config_option_removal(void);
void qa_test_weapon_editor(void);
void qa_test_custom_weapon_wire(void);
void qa_test_fixed_pool_layout(void);
void qa_test_save_record_wire(void);
void qa_test_save_slot_seats(void);
void qa_test_save_fixtures(void);
void qa_test_enhancement_presets(void);

void qa_test_cash_ledger(void);
void qa_test_bounty_matrix(void);
void qa_test_score_pickup_multiplier(void);
void qa_test_zone_payout(void);
void qa_test_arcade_scaling(void);
void qa_test_effect_gates(void);

void qa_test_shot_hitboxes(void);
void qa_test_guidance_perk(void);
void qa_test_surveyor_drop_perk(void);
void qa_test_twin_pods_perk(void);
void qa_test_reinforced_prow_perk(void);
void qa_test_knife_fight_perk(void);
void qa_test_deflector_perk(void);
void qa_test_countermeasure_burst(void);
void qa_test_guided_screen_aim(void);
void qa_test_elite_tier_eligibility(void);
void qa_test_homing_chaser_eligibility(void);
void qa_test_elite_explosion_tint(void);
void qa_test_elite_shot_tint(void);
void qa_test_elite_message_tint(void);
void qa_test_kill_fire_drives(void);
void qa_test_kill_fire_wiring(void);
void qa_test_kinetic_converter(void);

void qa_test_superspark_caps(void);
void qa_test_superspark_discarded_pass(void);
void qa_test_superspark_rng_cost(void);
void qa_test_superspark_brief(void);
void qa_test_superspark_seeded_spread(void);
void qa_test_superspark_shapes(void);
void qa_test_spark_ring_bounds(void);
void qa_test_vaporised_shot_sparks(void);
void qa_test_knife_fight_blood(void);

void qa_test_item_data_settings(void);
void qa_test_firing_sound_levels(void);
#ifdef WITH_MIDI
void qa_test_lds_midi_detune(void);
#endif

void qa_test_sidekick_rollback_state(void);
void qa_test_modifier_online_parity(void);
void qa_test_online_ship_style(void);
void qa_test_partner_hp_bars(void);
void qa_test_network_settings(void);
void qa_test_network_endless_lobby(void);
void qa_test_endless_coop(void);
void qa_test_coop_combo_and_pickups(void);
void qa_test_peer_left_level(void);
void qa_test_peer_idle_rule(void);
void qa_test_menu_claim(void);
void qa_test_resync_serialization(void);

void qa_test_arcade_matrices(void);
void qa_test_gauge_flash_lifetime(void);
void qa_test_special_icon_tops(void);
void qa_test_flying_punch_bolt(void);
void qa_test_dragonwing_row(void);
void qa_test_wide_hull_columns(void);
void qa_test_dual_mode_tag(void);
void qa_test_weapon_bay_tags(void);
void qa_test_ship_editor_file(void);
void qa_test_partner_repair_special(void);
void qa_test_health_bar_scale(void);
void qa_test_vulnerable_cue(void);

void qa_test_twiddle_ships(void);
void qa_test_twiddle_diagonals(void);
void qa_test_twiddle_wire(void);
void qa_test_twiddle_strictness(void);
void qa_test_twiddle_cooldown(void);
void qa_test_twiddle_charges(void);
void qa_test_flare_grade_ownership(void);
void qa_test_zinglon_pillar(void);
void qa_test_special_state_reset(void);

void qa_test_present_paths(void);
void qa_test_scaled_sprite_blocks(void);
void qa_test_scaled_table_sprites(void);
void qa_test_swar_pixel_loops(void);
void qa_test_background_bands(void);
void qa_test_filter_subrows(void);
void qa_test_flat_bands(void);

#endif /* QA_INTERNAL_H */
