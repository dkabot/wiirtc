#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <wiiuse/wpad.h>

#include "sysconf.h"

#define UNIX_EPOCH_TO_GC_EPOCH_DELTA 946684800ull

extern u32 __SYS_GetRTC(u32 *gctime);

void *initialise();

static void *xfb = NULL;
GXRModeObj *rmode = NULL;

int main(int argc, char **argv) {
	int retVal;

	xfb = initialise();

	printf ("\nRTC time setter\n");

	retVal = SYSCONF_Init();
	if (retVal < 0) {
		printf("Failed to init sysconf and settings.txt. Err: %d\n", retVal);
		exit(1);
	}

	printf("\n");

	uint32_t systemRTC;

	retVal = __SYS_GetRTC(&systemRTC);
	if (retVal == 0) {
		printf("Failed to get RTC. Err: %d. Aborting!\n", retVal);
		exit(1);
	}

	u32 bias;

	retVal = SYSCONF_GetCounterBias(&bias);
	if (retVal < 0) {
		printf("%s:%d. Failed to get counter bias. Err: %d. Aborting!\n", __FILE__, __LINE__, retVal);
		exit(1);
	}

	uint32_t oldSystemRTC = 0;
	uint64_t localTime = systemRTC + bias;
	u32 biasCheck;
	s32 selectedField = 0; // 0-5 -- hour, minute, second, month, day, year
	uint32_t buttonsDown;
	u32 buttonsDownGC;
	struct tm *cTime;
	char timeStr[80];

	printf("Use left and right button to select field, up and down to adjust field\nPress A to write time to system config\n");

	while (true) {
		VIDEO_WaitVSync();

		WPAD_ScanPads();
		buttonsDown = WPAD_ButtonsDown(0);
		PAD_ScanPads();
		buttonsDownGC = PAD_ButtonsDown(0);

		if (buttonsDown & WPAD_BUTTON_HOME || buttonsDownGC & PAD_BUTTON_START) {
			printf("\nHome button pressed. Exiting...\n");
			exit(0);
		}

		retVal = __SYS_GetRTC(&systemRTC);
		if (retVal == 0) {
			printf("Failed to get RTC. Err: %d. Aborting!\n", retVal);
			exit(1);
		}

		if (oldSystemRTC != systemRTC) {
			oldSystemRTC = systemRTC;
			localTime = systemRTC + bias + UNIX_EPOCH_TO_GC_EPOCH_DELTA;

			cTime = localtime((time_t *) &localTime);
			// Hour (24) : Minute : Second Month Day Year
			switch (selectedField) {
				case 0: // Hour
					strftime(timeStr, sizeof(timeStr), "\e[0;32m%H\e[0m:%M:%S %B %d %Y", cTime);
					break;
				case 1: // Minute
					strftime(timeStr, sizeof(timeStr), "%H:\e[0;32m%M\e[0m:%S %B %d %Y", cTime);
					break;
				case 2: // Second
					strftime(timeStr, sizeof(timeStr), "%H:%M:\e[0;32m%S\e[0m %B %d %Y", cTime);
					break;
				case 3: // Month
					strftime(timeStr, sizeof(timeStr), "%H:%M:%S \e[0;32m%B\e[0m %d %Y", cTime);
					break;
				case 4: // Day
					strftime(timeStr, sizeof(timeStr), "%H:%M:%S %B \e[0;32m%d\e[0m %Y", cTime);
					break;
				default: // Year
					strftime(timeStr, sizeof(timeStr), "%H:%M:%S %B %d \e[0;32m%Y\e[0m", cTime);
			}

			printf("\rProposed RTC system time: %s   ", timeStr);
			fflush(stdout);
		}

		if (!buttonsDown && !buttonsDownGC) {
			continue;
		}

		if (buttonsDown & WPAD_BUTTON_LEFT || buttonsDownGC & PAD_BUTTON_LEFT) {
			if (selectedField > 0) selectedField--;

		} else if (buttonsDown & WPAD_BUTTON_RIGHT || buttonsDownGC & PAD_BUTTON_RIGHT) {
			if (selectedField < 5) selectedField++;

		} else if (buttonsDown & WPAD_BUTTON_UP || buttonsDownGC & PAD_BUTTON_UP) {
			switch (selectedField) {
				case 0: // Hour
					bias += 3600;
					break;
				case 1: // Minute
					bias += 60;
					break;
				case 2: // Second
					bias += 1;
					break;
				case 3: // Month (30 days)
					bias += 2592000;
					break;
				case 4: // Day
					bias += 86400;
					break;
				default: // Year (non-leap)
					bias += 31536000;
			}

		} else if (buttonsDown & WPAD_BUTTON_DOWN || buttonsDownGC & PAD_BUTTON_DOWN) {
			switch (selectedField) {
				case 0: // Hour
					bias -= 3600;
					break;
				case 1: // Minute
					bias -= 60;
					break;
				case 2: // Second
					bias -= 1;
					break;
				case 3: // Month (30 days)
					bias -= 2592000;
					break;
				case 4: // Day
					bias -= 86400;
					break;
				default: // Year (non-leap)
					bias -= 31536000;
			}

		} else if (buttonsDown & WPAD_BUTTON_A) {
			printf("\nWriting new time (bias) to sysconf\n");
			retVal = SYSCONF_SetCounterBias(bias);
			if (retVal < 0) {
				printf("Failed to set counter bias. Err: %d. Aborting!\n", retVal);
				exit(1);
			}

			retVal = SYSCONF_SaveChanges();
			if (retVal != 0) {
				printf("Failed to save updated counter bias. Err: %d\n", retVal);
			}
			printf("Successfully saved counter bias change\n");

			printf("Checking time written (counter bias) value\n");
			biasCheck = 0;
			retVal = SYSCONF_GetCounterBias(&biasCheck);
			if (retVal < 0) {
				printf("Failed to get counter bias. Err: %d. Aborting!\n", retVal);
				exit(1);
			}

			if (bias != biasCheck) {
				printf("Failed to verify written bias value. Got %u, expected %u\n", biasCheck, bias);
				exit(1);
			}

			localTime = systemRTC + bias + UNIX_EPOCH_TO_GC_EPOCH_DELTA;
			cTime = localtime((time_t *) &localTime);
			strftime(timeStr, sizeof(timeStr), "%H:%M:%S %B %d %Y", cTime);

			printf("Time successfully updated to: %s\n", timeStr);
			printf("You may now terminate this program by pressing the home key\n(or continue to adjust the time)\n");
		}
	}

	return 0;
}
//---------------------------------------------------------------------------------
void *initialise() {
//---------------------------------------------------------------------------------

	void *framebuffer;

	VIDEO_Init();
	PAD_Init();
	if (WPAD_Init() != WPAD_ERR_NONE) {
		printf("Failed to initialize any wii motes\n");
	}

	rmode = VIDEO_GetPreferredMode(NULL);
	framebuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(framebuffer,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);

	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(framebuffer);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) {
		VIDEO_WaitVSync();
	}

	return framebuffer;
}
