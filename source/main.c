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

#define VERSION "1.1.0"

[[gnu::weak, gnu::format(printf, 1, 2)]]
void OSReport(const char* fmt, ...) {}

typedef enum {
	Wii  = 0x1,
	vWii = 0x2,
	Mini = 0x4,
} ConsoleType;

enum ChannelFlags {
	/* Just download this title ID. */
	RegionFree		= 0x00,

	/* Fill in the last byte with the system's region. */
	RegionSpecific	= 0x01,

	/* Fill in the last byte with A, or K if the system is Korean. */
	RegionFreeAndKR = 0x02,

	/* Will only be available for Japanese systems. */
	JPonly			= 0x04,

	/* Will not be available on Korean systems. */
	NoKRVersion		= 0x08,

	/* Purge this title before installing */
	Purge			= 0x10,
};

static ConsoleType ThisConsole = Wii;

typedef struct {
	const char* name;
	const char* description;
	int64_t titleID;
	enum ChannelFlags flags;
	ConsoleType disallowed;
	bool (*patcher)(struct Title*);

	bool selected;
} Channel;

static const char* strConsoleType(ConsoleType con) {
	switch (con) {
		case Wii : return "Wii";
		case vWii: return "vWii (Wii U)";
		case Mini: return "Wii Mini";
	}

	return "?";
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

static int InstallChannel(Channel* ch) {
	struct Title title = {};

//	printf("	>> Downloading %016llx metadata...\n", ch->titleID);
	int ret = DownloadTitleMeta(ch->titleID, -1, &title);
	if (ret < 0)
		return ret;
/*
	if (ch->titleID_new) {
		ChangeTitleID(&title, ch->titleID_new);
		Fakesign(&title);
	}
	if (ch->vWii_IOS && ThisConsole == vWii) {
		title.tmd->sys_version=0x1LL<<32 | ch->vWii_IOS;
		Fakesign(&title);
	}
*/
	if (ch->patcher && ch->patcher(&title))
		Fakesign(&title);

	ret = InstallTitle(&title, (bool)(ch->flags & Purge));
	FreeTitle(&title);
	if (ret < 0)
		return ret;

	return 0;
}

const char GetSystemRegionLetter(void) {
	CONF_Init();

	switch (CONF_GetRegion()) {
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

static bool patch_photo2_stub(struct Title* HAYA) {
	// Change TID of course
	ChangeTitleID(HAYA, 0x0001000048415A41LL);

	HAYA->tmd->num_contents = 1;
	HAYA->tmd->boot_index   = 0;
	// HAYA->tmd->sys_version  = 0x0000000100000000 | 31;

	return true;
}

static bool patch_photo2_update(struct Title* HAYA) {
	if (ThisConsole == vWii) {
		HAYA->tmd->sys_version = 0x0000000100000000 | 56; // IOS56
		return true;
	}

	return false;
}

static bool patch_ios56_ios61(struct Title* IOS56) {
	ChangeTitleID(IOS56, 0x0000000100000000 | 61);

	return true;
}

static Channel channels[] = {
	{
		.name = "EULA",

		.description = "Often missing because people don't complete their region changes.\n"
		"This will stand out if the User Agreements button demands for a\n"
		"Wii System Update.",

		.titleID = 0x0001000848414B00,
		.flags = (RegionSpecific),
	},

	{
		.name = "Region Select",

		.description = "This hidden channel is launched by apps like Mario Kart Wii and\n"
		"the Everybody Votes Channel. And somehow not in the Forecast\n"
		"Channel, but whatever.\n\n"

		"Ideal if your console was region changed.",

		.titleID = 0x0001000848414C00,
		.flags = (RegionSpecific),
		.disallowed = vWii
	},

	{
		.name = "Set Personal Data",

		.description = "This hidden channel is only used by some Japanese-exclusive\n"
		"channels, namely the Digicam Print Channel and Demae Channel.\n\n"

		"This won't work very well with the WiiLink services.\n",

		.titleID = 0x000100084843434A,
		.flags = (JPonly),
	},

	{
		.name = "Mii Channel",

		.description = "Stock version of the Mii Channel.\n\n"

		"Will not remove your Miis when (re)installed,\n"
		"they are stored elsewhere.\n",

		.titleID = 0x0001000248414341,
		.disallowed = vWii
	},

	{
		.name = "Mii Channel (Wii version)",

		.description = "This version of the Mii Channel comes with features removed\n"
		"from the vWii version, specifically sending Miis to\n"
		"Wii remotes, Wii friends and the Mii Parade.\n\n"

		"Installing this will also remove wuphax (if applicable.)",

		.titleID = 0x0001000248414341,
		.flags = (Purge),
		.disallowed = (Wii | Mini) // this entry is targetted to vWii users
	},

	{
		.name = "Photo Channel 1.0",

		.description = "Please note that this version does not support SDHC (>2GB) cards.",

		.titleID = 0x0001000248414141,
		.flags = (NoKRVersion | Purge)
	},

	{
		.name = "Photo Channel 1.1b (hidden channel)",

		.description = "This hidden channel is launched by the Wii menu when it detects\n"
		"the Photo Channel 1.1 stub (00010000-HAZA) on the system,\n"
		"which is usually downloaded through the Wii Shop Channel.\n\n"

		"(Wii users) See also: IOS61",

		.titleID = 0x0001000248415900,
		.flags = (RegionFreeAndKR),
		.patcher = patch_photo2_update,
	},

#if 0
	{
		.name = "Photo Channel 1.1b (photo_upgrader style)",

		.description = "This is the hidden channel with it's title ID changed to HAAA,\n"
		"replacing Photo Channel 1.0 in the process.\n",

		.titleID = 0x0001000248415900,
		.flags = (RegionFreeAndKR | Purge),

		.titleID_new = 0x0001000248414141,
		.vWii_IOS = 58
	},
#endif
	{
		.name = "Photo Channel 1.1b (stub trick)",

		.description = "This is the hidden channel with it's title ID changed to HAZA,\n"
		"to imitate the stub and show the proper channel.\n",

		.titleID = 0x0001000248415900,
		.flags = (RegionFreeAndKR | Purge),
		.patcher = patch_photo2_stub,
	},

	{
		.name = "Wii Shop Channel",

		.description = "Install this if the shop is bugging you to update.\n\n"

		"See also: IOS61",

		.titleID = 0x0001000248414200,
		.flags = (RegionFreeAndKR),
		.disallowed = vWii
	},

	{
		.name = "Internet Channel",

		.description = "Official Wii Internet browser, powered by Opera.\n"
		"Does not support modern encryption. Won't work with a lot of sites.",

		.titleID = 0x0001000148414400,
		.flags = (RegionSpecific | NoKRVersion)
	},

	{
		.name = "IOS58",

		.description = "The only part of the 4.3 update that mattered.\n"
		"If you do not already have this, re-install the Homebrew Channel\n"
		"to make it use IOS58.\n\n"

		"Re-launching the HackMii Installer: https://wii.hacks.guide/hackmii",

		.titleID = 0x0000000100000000 | 58,
		.disallowed = (vWii | Mini)
	},
#if 0
	{
		.name = "IOS59",

		.description = "IOS58 part 2 electric boogaloo, featuring a WFS\n"
		"driver for use with Dragon Quest X and the USB Repair Tool.\n\n"

		"Despite the above sentence, this doesn't mean that plugging in\n"
		"your Wii U's hard drive after installing this will make magic happen.",

		.titleID = 0x0000000100000000 | 59,
		.disallowed = (vWii | Mini)
	},
#endif
	{
		.name = "IOS61",

		.description = "Released in 4.0.\n"
		"Used by the latest Wii Shop Channel and Photo Channel 1.1.",

		.titleID = 0x0000000100000000 | 61,
		.disallowed = (vWii | Mini)
	},

#if 0
	{
		.name = "vIOS61",

		.description = "This name might ring a bell for RiiConnect24 Patcher users.\n\n"

		"Despite not existing on vWii, it's literally the exact same as IOS56,\n"
		"so nobody's stopping us from just....",

		.titleID = 0x0000000700000000 | 56,
		.disallowed = (Wii | Mini),

		.patcher = patch_ios56_ios61,
	}
#endif

	{
		.name = "IOS62",

		.description = "Used by the Wii U Transfer Tool. If your Wii Shop Channel is not updated,\n"
		"you likely need this as well.",

		.titleID = 0x0000000100000000 | 62,
		.disallowed = (vWii | Mini)
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
		goto exit;;
	}

	puts(
		"Select the channels you would like to restore.\n"
		"Press A to toggle an option. Press +/START to begin.\n"
		"Press B to cancel.\n" );

	int i = 0;
	Channel* allowedChannels[NBR_CHANNELS] = {};
	for (Channel* ch = channels; ch < channels + NBR_CHANNELS; ch++) {
		if (ch->disallowed & ThisConsole) continue;

		if ((ch->flags & JPonly) && regionLetter != 'J') continue;
		else if ((ch->flags & NoKRVersion) && regionLetter == 'K') continue;

		if (ch->flags & RegionSpecific) ch->titleID |= regionLetter;
		else if (ch->flags & RegionFreeAndKR) ch->titleID |= (regionLetter == 'K') ? 'K' : 'A';
		allowedChannels[i++] = ch;
	}

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

