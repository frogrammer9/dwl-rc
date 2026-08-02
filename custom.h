#ifndef DWLCUSTOM_H
#define DWLCUSTOM_H

#include <stdbool.h>
#include <wayland-server-core.h>

typedef struct {
	int cpu_load;
	int cpu_temp;
	int mem_usage;

	bool is_backlight;
	int backlight_pct;

	int volume_pct;
	bool is_audio_muted;
	char audio_sink_name[32];

	bool is_internet_connected;
	bool is_internet_wireless;
	char wifi_ssid[32];

	bool is_bat;
	bool is_bat_charging;
	int bat_pct;
} sysinfo_t;

typedef void (*redraw_callback)(void);

sysinfo_t get_system_info(void);
void system_info_init_event_loop(int timeout, struct wl_event_loop* loop, redraw_callback cb);

enum { SchemeNorm, SchemeSel, SchemeUrg };
extern uint32_t colors[3][3];

void config_init_event_loop(struct wl_event_loop* loop, redraw_callback cb);

const void* get_proc_arg(const char* name);

#endif // !DWLCUSTOM_H
