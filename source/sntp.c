#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <network.h>
#include <errno.h>
#include <fat.h>

#include <wiiuse/wpad.h>
#include <ogc/lwp_queue.h>

#include "ntp.h"
#include "sysconf.h"

extern u32 __SYS_GetRTC(u32 *gctime);

static uint32_t unix_time = 1754919480;

void *initialise();
void *ntp_client(void *arg);
void get_timestamp();

static void *xfb = NULL;
GXRModeObj *rmode = NULL;

static lwp_t ntp_handle = (lwp_t) NULL;

#define MAX_QUEUE_ITEMS 4
static lwp_queue queue;

typedef struct _queue_item {
	lwp_node node;
	u32 buttonsDown;
} queue_item;

static __inline__ lwp_node* __lwp_queue_head(lwp_queue *queue)
{
	return (lwp_node*)queue;
}

static __inline__ lwp_node* __lwp_queue_tail(lwp_queue *queue)
{
	return (lwp_node*)&queue->perm_null;
}

static __inline__ void __lwp_queue_init_empty(lwp_queue *queue)
{
	queue->first = __lwp_queue_tail(queue);
	queue->perm_null = NULL;
	queue->last = __lwp_queue_head(queue);
}

#define NO_RETRIES 20

int main(int argc, char **argv) {
	s32 ret;

	xfb = initialise();

	__lwp_queue_init_empty(&queue);

	printf ("\nRTC time setter\n");

	ret = SYSCONF_Init();
	if (ret < 0) {
		printf("Failed to init sysconf and settings.txt. Err: %d\n", ret);
		exit(1);
	}

	get_timestamp();
	printf("\n");

	if (LWP_CreateThread(&ntp_handle,	/* thread handle */
					 ntp_client,	/* code */
					 NULL,		    /* arg pointer for thread */
					 NULL,			/* stack base */
					 16*1024,		/* stack size */
					 50				/* thread priority */ ) < 0) {
		printf("Failed to create RTC thread. Aborting!\n");
		exit(0);
	}

	uint32_t buttonsDown;
	u32 buttonsDownGC;
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
		if (buttonsDownGC & PAD_BUTTON_LEFT) {
			buttonsDown |= WPAD_BUTTON_LEFT;
		}
		if (buttonsDownGC & PAD_BUTTON_RIGHT) {
			buttonsDown |= WPAD_BUTTON_RIGHT;
		}
		if (buttonsDownGC & PAD_BUTTON_UP) {
			buttonsDown |= WPAD_BUTTON_UP;
		}
		if (buttonsDownGC & PAD_BUTTON_DOWN) {
			buttonsDown |= WPAD_BUTTON_DOWN;
		}
		if (buttonsDownGC & PAD_BUTTON_A) {
			buttonsDown |= WPAD_BUTTON_A;
		}

		if (buttonsDown != 0) {
			queue_item *q = malloc(sizeof(queue_item));
			if (q == NULL) {
				printf("Failed to allocate queue item! Err:%d, %s\n", errno, strerror(errno));
				continue;
			}
			q->buttonsDown = buttonsDown;

			__lwp_queue_append(&queue, (lwp_node *) q);
		}
	}

	return 0;
}

void *ntp_client(void *arg) {
	int n;
	uint32_t rtc_s;
	uint64_t local_time;
	uint64_t ntp_time_in_gc_epoch;
	u32 bias, chk_bias;
	s32 selected = 0; // 0-5 -- hour, minute, second, month, day, year

	n = __SYS_GetRTC(&rtc_s);
	if (n == 0) {
		printf("Failed to get RTC. Err: %d. Aborting!\n", n);
		return NULL;
	}

	ntp_time_in_gc_epoch = unix_time - UNIX_EPOCH_TO_GC_EPOCH_DELTA;

	n = SYSCONF_GetCounterBias(&bias);
	if (n < 0) {
		printf("%s:%d. Failed to get counter bias. Err: %d. Aborting!\n", __FILE__, __LINE__, n);
		return NULL;
	}

	local_time = rtc_s + bias;

	printf("Use left and right button to select field, up and down to modify field\nPress A to write time to system config\n");

	// Calculate new bias
	bias = ntp_time_in_gc_epoch - rtc_s;

	uint32_t old_rtc_s = 0;
	char time_str[80];
	struct tm *p;

	while (true) {
		queue_item *q = (queue_item *) __lwp_queue_get(&queue);

		n = __SYS_GetRTC(&rtc_s);
		if (n == 0) {
			printf("Failed to get RTC. Err: %d. Aborting!\n", n);
			return NULL;
		}

		if (old_rtc_s != rtc_s) {
			old_rtc_s = rtc_s;
			local_time = rtc_s + bias + UNIX_EPOCH_TO_GC_EPOCH_DELTA;

			p = localtime((time_t *) &local_time);
			// Hour (24) : Minute : Second Month Day Year
			switch (selected) {
				case 0: // Hour
					strftime(time_str, sizeof(time_str), "\e[0;32m%H\e[0m:%M:%S %B %d %Y", p);
					break;
				case 1: // Minute
					strftime(time_str, sizeof(time_str), "%H:\e[0;32m%M\e[0m:%S %B %d %Y", p);
					break;
				case 2: // Second
					strftime(time_str, sizeof(time_str), "%H:%M:\e[0;32m%S\e[0m %B %d %Y", p);
					break;
				case 3: // Month
					strftime(time_str, sizeof(time_str), "%H:%M:%S \e[0;32m%B\e[0m %d %Y", p);
					break;
				case 4: // Day
					strftime(time_str, sizeof(time_str), "%H:%M:%S %B \e[0;32m%d\e[0m %Y", p);
					break;
				default: // Year
					strftime(time_str, sizeof(time_str), "%H:%M:%S %B %d \e[0;32m%Y\e[0m", p);
			}

			printf("\rProposed RTC system time: %s   ", time_str);
			fflush(stdout);
		}

		if (q == NULL) {
			LWP_YieldThread();
			continue;
		}

		if (q && q->buttonsDown & WPAD_BUTTON_LEFT) {
			if (selected > 0) selected--;

		} else if (q && q->buttonsDown & WPAD_BUTTON_RIGHT) {
			if (selected < 5) selected++;

		} if (q && q->buttonsDown & WPAD_BUTTON_UP) {
			switch (selected) {
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

		} else if (q && q->buttonsDown & WPAD_BUTTON_DOWN) {
			switch (selected) {
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

		} else if (q->buttonsDown & WPAD_BUTTON_A) {
			printf("\nWriting new time (bias) to sysconf\n");
			n = SYSCONF_SetCounterBias(bias);
			if (n < 0) {
				printf("Failed to set counter bias. Err: %d. Aborting!\n", n);
				return NULL;
			}

			n = SYSCONF_SaveChanges();
			if (n != 0) {
				printf("Failed to save updated counter bias. Err: %d\n", n);
			}
			printf("Successfully saved counter bias change\n");

			printf("Checking time written (counter bias) value\n");
			chk_bias = 0;
			n = SYSCONF_GetCounterBias(&chk_bias);
			if (n < 0) {
				printf("Failed to get counter bias. Err: %d. Aborting!\n", n);
				return NULL;
			}

			if (bias != chk_bias) {
				printf("Failed to verify written bias value. Got %u, expected %u\n", chk_bias, bias);
				return NULL;
			}

			local_time = rtc_s + bias + UNIX_EPOCH_TO_GC_EPOCH_DELTA;
			p = localtime((time_t *) &local_time);
			strftime(time_str, sizeof(time_str), "%H:%M:%S %B %d %Y", p);

			printf("Time successfully updated to: %s\n", time_str);
			printf("You may now terminate this program by pressing the home key\n(or continue to adjust the time zones)\n");
		}
		free(q);
	}

	return NULL;
}

//---------------------------------------------------------------------------------
void *initialise() {
//---------------------------------------------------------------------------------

	void *framebuffer;

	VIDEO_Init();
	fatInitDefault();
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
void get_timestamp() {
//---------------------------------------------------------------------------------

	FILE *ntpf;

	printf("Attempting to open timestamp file...\n");
	chdir(NTP_HOME);
	ntpf = fopen(NTP_FILE, "r");
	if(ntpf == NULL)
		return;
	printf("Timestamp file opened!\n");
	fscanf(ntpf, "%d", &unix_time);
	fclose(ntpf);
}
