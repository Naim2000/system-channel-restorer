#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#include "iospatch.h"
#include "video.h"
#include "pad.h"
#include "network.h"
#include "nus.h"

#define VERSION "1.2.0"

[[gnu::weak, gnu::format(printf, 1, 2)]]
void OSReport(const char* fmt, ...) {}

typedef enum {
	Wii, vWii, Mini
} ConsoleType;

static ConsoleType ThisConsole = Wii;
static int ThisRegion = 0;

const char GetSystemRegionLetter(void) {
	switch (ThisRegion) {
		case CONF_REGION_JP: return 'J';
		case CONF_REGION_US: return 'E';
		case CONF_REGION_EU: return 'P';
		case CONF_REGION_KR: return 'K';
	}

	return 0;
}

static const char* strRegionLetter(int c) {
	switch (c) {
		case 'J': return "Japan (NTSC-J)";
		case 'E': return "USA (NTSC-U)";
		case 'P': return "Europe (PAL)";
		case 'K': return "Korean (NTSC-K)";
	}

	return "Unknown (!?)";
}

static const char* strConsoleType(ConsoleType con) {
	switch (con) {
		case Wii : return "Wii";
		case vWii: return "vWii (Wii U)";
		case Mini: return "Wii Mini";
	}

	return "?";
}

enum {
	regiontype_specific = 0,
	regiontype_free,
	regiontype_freeandkr,

#define consoleflag(X) (1 << X)

	regionflag_nokr = 1,
	regionflag_jponly,

};


typedef struct Channel {
	const char* name;
	const char* description;
	int64_t     titleID;
	uint8_t     regiontype;
	uint8_t     consoleflag;
	uint8_t     regionflag;
	int       (*install)(void);

	bool        selected;
} Channel;

int64_t getTitleID(Channel* ch) {
	switch (ch->regiontype) {
		case regiontype_free:		return ch->titleID;
		case regiontype_specific:	return ch->titleID | GetSystemRegionLetter();
		case regiontype_freeandkr:	return ch->titleID | (ThisRegion == CONF_REGION_KR) ? 'K' : 'A';
	}

	return 0;
}

static bool SelectChannels(Channel* channels[], int cnt) {
	int index = 0, curX = 0, curY = 0;

	CON_GetPosition(&curX, &curY);

	for (;;) {
		Channel* ch = channels[index];

		printf("\x1b[%i;0H", curY);
		for (int i = 0; i < cnt; i++)
			printf("%s%s	%s\x1b[40m\x1b[39m\n", i == index? ">>" : "  ",
				   channels[i]->selected? "\x1b[47;1m\x1b[30m" : "", channels[i]->name);

		printf("\n\x1b[0J%s", ch->description ?: "no description");

		for (;;) {
			scanpads();
			uint32_t buttons = buttons_down(0);

			if (buttons & WPAD_BUTTON_DOWN) {
				if (++index == cnt) index = 0;
				break;
			}
			else if (buttons & WPAD_BUTTON_UP) {
				if (--index < 0) index = cnt - 1;
				break;
			}

			else if (buttons & WPAD_BUTTON_A) { ch->selected ^= true; break; }
			else if (buttons & WPAD_BUTTON_PLUS) return true;
			else if (buttons & (WPAD_BUTTON_B | WPAD_BUTTON_HOME)) return false;
		}
	}
}

int InstallChannelGeneric(int64_t titleID, bool force_purge) {
	struct Title local, remote;

	int retl = GetInstalledTitle(titleID, &local);
	int retr = DownloadTitleMeta(titleID, -1, &remote);

	if (retr < 0)
		return retr;

	if (force_purge || retl < 0 || local.tmd->title_version < remote.tmd->title_version)
		return InstallTitle(&remote, force_purge);

	return 0;
}

static int InstallChannel(Channel* ch) {
	return (ch->install) ? ch->install() : InstallChannelGeneric(getTitleID(ch), false);
}

// unused
static bool patch_photo_stub(struct Title* HAAA) {
	HAAA->tmd->title_version = 0xFF00;
	HAAA->tmd->num_contents  = 0;
	HAAA->tmd_size           = SIGNED_TMD_SIZE(HAAA->s_tmd);

	return true;
}

static int install_pc_1_1(void) {
	// vWii - Let's get ourselves an IOS61.
	if (ThisConsole == vWii) {
		struct Title vIOS56;

		int ret = GetInstalledTitle(0x0000000100000000 | 56, &vIOS56);
		if (ret < 0) {
			printf("GetInstalledTitle(IOS56) returned %i, why?\n", ret);
			printf("Is it Decaffeinator time already?\n");
			return ret;
		}

		ChangeTitleID(&vIOS56, 0x0000000100000000 | 61);
		ret = InstallTitle(&vIOS56, true);
		if (ret < 0) {
			return ret;
		}
	}

	// Let's make sure we have the actual Photo Channel 1.1.
	char regionLetter = (ThisRegion == CONF_REGION_KR) ? 'K' : 'A';
	InstallChannelGeneric(0x0001000248415900 | regionLetter, false);

	// Alright, let's make the dummy now.
	struct Title HAAA;
	int ret = GetInstalledTitle(0x0001000248414100 | regionLetter, &HAAA);
	if (ret < 0 || HAAA.tmd->title_version > 2 /* photo_upgrader? */ ) {
		ret = DownloadTitleMeta(0x0001000248414100 | regionLetter, -1, &HAAA);
		if (ret < 0)
			return ret;
	}

	ChangeTitleID(&HAAA, 0x0001000048415A41LL);
	// ChangeTitleID(HAAA, 0x0001000050494B41);

	HAAA.tmd->num_contents  = 1;
	HAAA.tmd->boot_index    = 0;
	HAAA.tmd_size           = SIGNED_TMD_SIZE(HAAA.s_tmd);
	HAAA.tmd->sys_version   = 0x0000000100000000 | 254; // !
	HAAA.tmd->title_version = 0;
	ret = InstallTitle(&HAAA, false);

	return ret;
}

static int install_shop_channel(void) {
	if (ThisConsole == Wii) {
		int ret = InstallChannelGeneric(0x0000000100000000 | 61, false);
		if (ret < 0)
			return ret;
	}

	char regionLetter = (ThisRegion == CONF_REGION_KR) ? 'K' : 'A';
	return InstallChannelGeneric(0x0001000248414200 | regionLetter, false);
}

static int install_mii_channel(void) {
	return InstallChannelGeneric(0x0001000248414341, ThisConsole == vWii);
}

static Channel channels[] = {
	{
		.name = "EULA",

		.description =
		"Often missing because people don't complete their region changes.\n"
		"This will stand out if the User Agreements button demands for a\n"
		"Wii System Update.",

		.titleID = 0x0001000848414B00,
	},

	{
		.name = "Region Select",

		.description =
		"This hidden channel is launched by apps like Mario Kart Wii and\n"
		"the Everybody Votes Channel. And somehow not in the Forecast\n"
		"Channel, but whatever.\n\n"

		"Ideal if your console was region changed.",

		.consoleflag = consoleflag(vWii),
		.titleID     = 0x0001000848414C00,
	},

	{
		.name = "Set Personal Data",

		.description =
		"This hidden channel is only used by some Japanese-exclusive\n"
		"channels, namely the Digicam Print Channel and Demae Channel.\n\n"

		"This won't work very well with the WiiLink services.\n",

		.regiontype = regiontype_free,
		.regionflag = regionflag_jponly,
		.titleID    = 0x000100084843434A,
	},

	{
		.name = "Mii Channel",

		.description =
		"Stock version of the Mii Channel.\n\n"

		"On vWii, this adds back removed features like Wii remote transfer,\n"
		"sending Miis to Wii Friends, and the Mii Parade. Also a quick way\n"
		"to remove wuphax. (i.e. the Mii Channel is booting you to CTGP-R, or\n"
		"'boot.elf not found!', or whatever.)",

		.regionflag = regionflag_nokr,
		.install    = install_mii_channel,
	},

	{
		.name = "Photo Channel 1.0",

		.description =
		"Please note that Photo Channel 1.0 does not support SDHC (>2GB) cards.",

		.regiontype = regiontype_free,
		.regionflag = regionflag_nokr,
		.titleID = 0x0001000248414141,
	},

	{
		// This is where the install function gets interesting.
		.name = "Photo Channel 1.1b",

		.description =
		"Photo Channel 1.1 adds support for SDHC (and SDXC!) memory cards,\n"
		"as well as setting the banner on the Wii Menu to a photo of your choice.",

		.install = install_pc_1_1
	},

	{
		.name = "Wii Shop Channel",

		.install = install_shop_channel,
	},

	{
		.name = "Internet Channel",

		.description =
		"Official Wii Internet browser, powered by Opera.\n"
		"Does not support modern encryption. Won't work with a lot of sites.",

		.regionflag = regionflag_nokr,
		.titleID = 0x0001000148414400,
	},

	{
		.name = "IOS58",

		.description =
		"The only part of the 4.3 update that mattered.\n"
		"If you do not already have this, re-install the Homebrew Channel\n"
		"to make it use IOS58.\n\n"

		"Re-launching the HackMii Installer: https://wii.hacks.guide/hackmii",

		.regiontype = regiontype_free,
		.consoleflag = ~consoleflag(Wii),
		.titleID = 0x0000000100000000 | 58,
	},
};

#define NBR_CHANNELS (sizeof(channels) / sizeof(Channel))

int main() {
	puts(
		"Wii System Channel Restorer by thepikachugamer\n"
	//	"This is a mix of photo_upgrader and cleartool\n"
	);

	if (patchIOS(false) < 0)
		puts("(failed to apply IOS patches..?)");

	initpads();
	ISFS_Initialize();
	CONF_Init();

	ThisRegion = CONF_GetRegion();
	const char regionLetter = GetSystemRegionLetter();
	if (!regionLetter) {
		puts("Failed to identify system region (!?)");
		goto exit;
	}

	uint32_t x = 0;

	if (!ES_GetTitleContentsCount(0x100000200LL, &x) && x) {
		ThisConsole = vWii;
	}
	else {
		struct Title sm = {};
		if (!GetInstalledTitle(0x100000002LL, &sm)) {
			uint16_t sm_rev = sm.tmd->title_version;
			FreeTitle(&sm);
			if ((sm_rev & 0xFFE0) == 0x1200)
				ThisConsole = Mini;
		}
	}

	printf("Console region: %-24s    Console Type: %s\n\n", strRegionLetter(regionLetter), strConsoleType(ThisConsole));

	int ret = network_init();
	if (ret < 0) {
		printf("Failed to initialize network! (%i)\n", ret);
		goto exit;
	}

	int i = 0;
	Channel* allowedChannels[NBR_CHANNELS] = {};
	for (Channel* ch = channels; ch < channels + NBR_CHANNELS; ch++) {
		switch (ch->regionflag) {
			case regionflag_jponly: if (ThisRegion != CONF_REGION_JP) continue; break;
			case regionflag_nokr:   if (ThisRegion == CONF_REGION_KR) continue; break;
		}

		if (ch->consoleflag & consoleflag(ThisConsole)) continue;

		allowedChannels[i++] = ch;
	}

	puts(
		"Select the channels you would like to restore.\n"
		"Press A to toggle an option. Press +/START to begin.\n"
		"Press B to cancel.\n" );


	if (!SelectChannels(allowedChannels, i)) goto exit;

	putchar('\n');

	// what a mess of code this thing is
	for (Channel* ch = channels; ch < channels + NBR_CHANNELS; ch++) {
		if (!ch->selected) continue;

		printf("[*] Installing %s...\n", ch->name);
		ret = InstallChannel(ch);
		if (ret < 0)
			printf("Failed! (%i)\n", ret);
		else
			puts("OK!");
	}

exit:
	network_deinit();
	ISFS_Deinitialize();
	puts("\n\nPress HOME to exit.");
	wait_button(WPAD_BUTTON_HOME);
	WPAD_Shutdown();
	return 0;
}

