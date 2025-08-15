#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <wiiuse/wpad.h>

#include "sysconf.h"

// Unix Epoch 1970-01-01 00:00
// Gamecube Epoch 2000-01-01 00:00
#define UNIX_EPOCH_TO_GC_EPOCH_DELTA 946684800ull

extern u32 __SYS_GetRTC(u32 *gctime);

void *initialise();
int daysInMonth(int month, int year);

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

	uint64_t localTime = systemRTC + bias + UNIX_EPOCH_TO_GC_EPOCH_DELTA;
	s32 selectedField = 0; // 0-5 -- hour, minute, second, month, day, year
	uint32_t buttonsDown;
	u32 buttonsDownGC;
	BOOL timeDirty = TRUE;
	char timeStr[80];
	struct tm *cTime = localtime((time_t *) &localTime);

	printf("Use left and right button to select field, up and down to adjust field\nPress A to write time to system config\n");

	while (TRUE) {
		VIDEO_WaitVSync();

		WPAD_ScanPads();
		buttonsDown = WPAD_ButtonsDown(0);
		PAD_ScanPads();
		buttonsDownGC = PAD_ButtonsDown(0);

		// Separate checks just to make the text nicer
		if (buttonsDown & WPAD_BUTTON_HOME) {
			printf("\nHome button pressed. Exiting...\n");
			exit(0);
		}
		else if (buttonsDownGC & PAD_BUTTON_START) {
			printf("\nStart button pressed. Exiting...\n");
			exit(0);
		}

		if (timeDirty) {
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

			timeDirty = FALSE;
		}

		// Don't proceed if no buttons are being held
		if (!buttonsDown && !buttonsDownGC) {
			continue;
		}

		// Something was pressed, so update the time preview at the next opportunity
		timeDirty = TRUE;

		// Left/right just change options
		if (buttonsDown & WPAD_BUTTON_LEFT || buttonsDownGC & PAD_BUTTON_LEFT) {
			if (selectedField > 0) selectedField--;

		} else if (buttonsDown & WPAD_BUTTON_RIGHT || buttonsDownGC & PAD_BUTTON_RIGHT) {
			if (selectedField < 5) selectedField++;

		// Up/down set the current option
		} else if (buttonsDown & WPAD_BUTTON_UP || buttonsDownGC & PAD_BUTTON_UP || buttonsDown & WPAD_BUTTON_DOWN || buttonsDownGC & PAD_BUTTON_DOWN) {
			BOOL isIncrement = (buttonsDown & WPAD_BUTTON_UP || buttonsDownGC & PAD_BUTTON_UP); // Store whether this is up or down for repeated use
			int *field; // Pointer to the exact part of tm we are manipulating

			// Set the pointer, then check to make sure we are not trying something invalid
			switch (selectedField) {
				case 0: // Hour
					field = &cTime->tm_hour;
					if (isIncrement && *field >= 23) continue;
					break;
				case 1: // Minute
					field = &cTime->tm_min;	
					if (isIncrement && *field >= 59) continue;
					break;
				case 2: // Second
					field = &cTime->tm_sec;
					if (isIncrement && *field >= 59) continue; // tm allows for a leap second but don't think that matters here
					break;
				case 3: // Month
					field = &cTime->tm_mon;
					if (isIncrement && *field >= 11) continue;
					break;
				case 4: // Day
					field = &cTime->tm_mday;
					if (isIncrement && *field >= daysInMonth(cTime->tm_mon, cTime->tm_year)) continue; // Feb varies, don't do this inline
					break;
				default: // Year
					field = &cTime->tm_year;
					if (isIncrement) { if (*field >= 135) continue; } // tm is "since 1900", so 2035 (RTC can go higher but Sustem Menu cannot)
					else if (*field <= 100) continue; // 2000 as minimum, special case versus the others
			}

			if (!isIncrement && *field <= 0) continue; // Universal invalidity check instead of duplicating it

			// Increment or decrement as already determined
			if (isIncrement) (*field)++;
			else (*field)--;

			// Month or year changed; cap out day if needed
			if (selectedField == 3 || selectedField == 5) {
				int maxDays = daysInMonth(cTime->tm_mon, cTime->tm_year);
				if (cTime->tm_mday > maxDays) cTime->tm_mday = maxDays;
			}

		} else if (buttonsDown & WPAD_BUTTON_A || buttonsDownGC & PAD_BUTTON_A) {
			printf("\nWriting new time (bias) to sysconf\n");

			retVal = __SYS_GetRTC(&systemRTC);
			if (retVal == 0) {
				printf("Failed to get RTC. Err: %d. Aborting!\n", retVal);
				exit(1);
			}

			bias = mktime(cTime) - systemRTC - UNIX_EPOCH_TO_GC_EPOCH_DELTA;

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
			u32 biasCheck = 0;
			retVal = SYSCONF_GetCounterBias(&biasCheck);
			if (retVal < 0) {
				printf("Failed to get counter bias. Err: %d. Aborting!\n", retVal);
				exit(1);
			}

			if (bias != biasCheck) {
				printf("Failed to verify written bias value. Got %u, expected %u\n", biasCheck, bias);
				exit(1);
			}

			// Technically redundant to recalculate, but affirms to the user that the time is set as expected
			localTime = systemRTC + bias + UNIX_EPOCH_TO_GC_EPOCH_DELTA;
			cTime = localtime((time_t *) &localTime);
			strftime(timeStr, sizeof(timeStr), "%H:%M:%S %B %d %Y", cTime);

			printf("Time successfully updated to: %s\n", timeStr);
			printf("You may now terminate this program by pressing home or start\n(or continue to adjust the time)\n");
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
//---------------------------------------------------------------------------------
int daysInMonth(int month, int year) {
//---------------------------------------------------------------------------------

	switch (month) {
		case 3:  // April
		case 5:  // June
		case 8:  // September
		case 10: // November
			return 30;
		case 1:  // February
			if (year % 4 == 0 && !(year % 100 == 0 && year % 400 != 0)) return 29; // Leap year
			else return 28; // Non-leap year
		default: // Everything else
			return 31;
	}
}
