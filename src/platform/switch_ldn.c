/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Nintendo Switch local wireless (libnx ldn); see switch_ldn.h.
 */
#include "switch_ldn.h"

#ifdef __SWITCH__

#include <switch.h>

#include <string.h>

// A fixed phrase every copy shares; LDN derives the link key from it.
static const char ldn_passphrase[] = "OpenTyrian2000 Engaged local play";

// Tags this game's networks, so a scan skips other homebrew launched through the same title.
#define LDN_SCENE_ID 0x5432

static bool ldn_up = false;
static LdnNetworkInfo ldn_games[SWITCH_LDN_MAX_GAMES];
static int ldn_game_count = 0;

static void ldn_intent(LdnIntentId *intent)
{
	memset(intent, 0, sizeof(*intent));
	intent->local_communication_id = -1;  // the launching title's id, from its NACP
	intent->scene_id = LDN_SCENE_ID;
}

static void ldn_security(LdnSecurityConfig *sec)
{
	memset(sec, 0, sizeof(*sec));
	sec->security_mode = LdnSecurityMode_Product;
	sec->passphrase_size = sizeof(ldn_passphrase) - 1;
	memcpy(sec->passphrase, ldn_passphrase, sizeof(ldn_passphrase) - 1);
}

static void ldn_user(LdnUserConfig *user, const char *nickname)
{
	memset(user, 0, sizeof(*user));
	strncpy(user->user_name, (nickname != NULL && nickname[0] != '\0') ? nickname : "Player",
	        sizeof(user->user_name) - 1);
}

static bool ldn_start(void)
{
	if (!ldn_up && R_SUCCEEDED(ldnInitialize(LdnServiceType_User)))
		ldn_up = true;
	return ldn_up;
}

bool switch_ldn_host(const char *nickname, const uint8_t *advert, size_t advert_len)
{
	switch_ldn_leave();
	if (!ldn_start())
		return false;

	LdnSecurityConfig sec;
	LdnUserConfig user;
	LdnNetworkConfig net;
	ldn_security(&sec);
	ldn_user(&user, nickname);
	memset(&net, 0, sizeof(net));
	ldn_intent(&net.intent_id);
	net.node_count_max = 2;  // netplay seats two players

	if (R_SUCCEEDED(ldnOpenAccessPoint()) &&
	    R_SUCCEEDED(ldnCreateNetwork(&sec, &user, &net)) &&
	    R_SUCCEEDED(ldnSetAdvertiseData(advert, advert_len)))
		return true;

	switch_ldn_leave();
	return false;
}

int switch_ldn_scan(void)
{
	switch_ldn_leave();
	if (!ldn_start())
		return 0;

	if (R_FAILED(ldnOpenStation()))
	{
		switch_ldn_leave();
		return 0;
	}

	LdnScanFilter filter;
	memset(&filter, 0, sizeof(filter));
	ldn_intent(&filter.network_id.intent_id);
	filter.flags = LdnScanFilterFlag_IntentId;

	s32 total = 0;
	if (R_FAILED(ldnScan(0, &filter, ldn_games, SWITCH_LDN_MAX_GAMES, &total)) || total < 0)
		total = 0;

	ldn_game_count = total < SWITCH_LDN_MAX_GAMES ? (int)total : SWITCH_LDN_MAX_GAMES;
	return ldn_game_count;
}

bool switch_ldn_game(int index, const uint8_t **advert, size_t *advert_len)
{
	if (index < 0 || index >= ldn_game_count)
		return false;

	const LdnNetworkInfo *const info = &ldn_games[index];
	*advert = info->advertise_data;
	*advert_len = info->advertise_data_size < sizeof(info->advertise_data)
	            ? info->advertise_data_size : sizeof(info->advertise_data);
	return true;
}

bool switch_ldn_join(int index, const char *nickname, uint32_t *host)
{
	if (!ldn_up || index < 0 || index >= ldn_game_count)
		return false;

	LdnSecurityConfig sec;
	LdnUserConfig user;
	ldn_security(&sec);
	ldn_user(&user, nickname);

	if (R_FAILED(ldnConnect(&sec, &user, 0, 0, &ldn_games[index])))
		return false;

	// Node 0 is the access point, which is the hosting console. Prefer the joined view.
	LdnNetworkInfo info;
	if (R_SUCCEEDED(ldnGetNetworkInfo(&info)) && info.nodes[0].ip_addr.addr != 0)
		*host = info.nodes[0].ip_addr.addr;
	else
		*host = ldn_games[index].nodes[0].ip_addr.addr;

	return *host != 0;
}

void switch_ldn_leave(void)
{
	ldn_game_count = 0;
	if (!ldn_up)
		return;

	LdnState state;
	if (R_SUCCEEDED(ldnGetState(&state)))
	{
		if (state == LdnState_AccessPointCreated)
			ldnDestroyNetwork();
		if (state == LdnState_AccessPoint || state == LdnState_AccessPointCreated)
			ldnCloseAccessPoint();
		if (state == LdnState_StationConnected)
			ldnDisconnect();
		if (state == LdnState_Station || state == LdnState_StationConnected)
			ldnCloseStation();
	}

	ldnExit();
	ldn_up = false;
}

#endif // __SWITCH__
