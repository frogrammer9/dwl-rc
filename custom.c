#include "custom.h"
#include "dbus.h"
#include "util.h"

#include <dbus/dbus.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

// ==================== META HELPERS ====================

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define CLAMP(V, A, B) (MIN(MAX((V), (A)), (B)))

// ==================== INIT ====================

static sysinfo_t s_sysinfo;

// colors isn't here because it was to baked in for me to want to deal with it
// TODO perhaps it would be nice to clean it up someday
typedef struct {
	int bemenu_border;
	int bemenu_lines;
	uint32_t bemenu_color_border;
	uint32_t bemenu_color_fg[4]; // normal, alt, selected, title
	uint32_t bemenu_color_bg[4]; // normal, alt, selected, title
} config_t;
static config_t s_config;

uint32_t colors[3][3];

static char s_temp_sensor_path[128];
static void s_init_temp_sensor(void) {
	DIR* dir = opendir("/sys/class/hwmon");

	if (!dir) {
		s_sysinfo.cpu_temp = -1;
		return;
	}

	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		char path[128];
		int pfc = snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", ent->d_name);
		if (pfc < 0 || pfc >= (int)sizeof(path)) {
			closedir(dir);
			s_sysinfo.cpu_temp = -1;
			return;
		}
		FILE* name_f = fopen(path, "r");
		if (!name_f)
			continue;

		char name[32];
		fgets(name, sizeof(name), name_f);
		fclose(name_f);
		if (strncmp(name, "coretemp", 8) == 0 || strncmp(name, "k10temp", 7) == 0 || strncmp(name, "zenpower", 8) == 0) {
			pfc = snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp1_input", ent->d_name);
			if (pfc < 0 || pfc >= (int)sizeof(path)) {
				closedir(dir);
				s_sysinfo.cpu_temp = -1;
				return;
			}
			// TODO This is stupid, make it better
			strncpy(s_temp_sensor_path, path, sizeof(s_temp_sensor_path));
			closedir(dir);
			return;
		}
	}
	closedir(dir);
	s_sysinfo.cpu_temp = -1;
	return;
}

static char s_battery_dir_path[128]; // it has / at the end (eg: /sys/class/power_supply/BAT0/)
static void s_init_battery_path(void) {
	DIR* dir = opendir("/sys/class/power_supply");
	if (!dir) {
		s_sysinfo.is_bat = false;
		return;
	}

	struct dirent* ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		char path[128];
		int pfc = snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", ent->d_name);
		if (pfc < 0 || pfc >= (int)sizeof(path)) {
			closedir(dir);
			s_sysinfo.is_bat = false;
			return;
		}
		char val[32];
		FILE* f = fopen(path, "r");
		if (!f) {
			closedir(dir);
			s_sysinfo.is_bat = false;
			return;
		}
		fgets(val, sizeof(val), f);
		fclose(f);
		if (strncmp(val, "Battery", 7) == 0) {
			pfc = snprintf(s_battery_dir_path, sizeof(s_battery_dir_path), "/sys/class/power_supply/%s/", ent->d_name);
			closedir(dir);
			if (pfc < 0 || pfc >= (int)sizeof(path)) {

				s_sysinfo.is_bat = false;
				return;
			}
			s_sysinfo.is_bat = true;
			return;
		}
	}
	closedir(dir);
	s_sysinfo.is_bat = false;
	return;
}

static char s_backlight_dir_path[128]; // It has / at the end;
static void s_init_backlight_path(void) {
	DIR* dir = opendir("/sys/class/backlight");
	if (!dir) {
		s_sysinfo.is_backlight = false;
		return;
	}

	struct dirent* ent;
	int found_prio = 0;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		char path[128];

		int pfc = snprintf(path, sizeof(path), "/sys/class/backlight/%s/type", ent->d_name);
		if (pfc < 0 || pfc >= (int)sizeof(path)) {
			closedir(dir);
			s_sysinfo.is_backlight = false;
			return;
		}

		char buff[32];
		FILE* f = fopen(path, "r");
		if (!f)
			continue;
		fgets(buff, sizeof(buff), f);
		fclose(f);

		if (strncmp(buff, "raw", 3) == 0) {
			pfc = snprintf(s_backlight_dir_path, sizeof(s_backlight_dir_path), "/sys/class/backlight/%s/", ent->d_name);
			if (pfc < 0 || pfc >= (int)sizeof(path)) {
				closedir(dir);
				s_sysinfo.is_backlight = false;
				return;
			}
			s_sysinfo.is_backlight = true;
			return;
		}
		if (strncmp(buff, "platform", 8) == 0) {
			if (found_prio < 3) {
				pfc = snprintf(s_backlight_dir_path, sizeof(s_backlight_dir_path), "/sys/class/backlight/%s/", ent->d_name);
				if (pfc < 0 || pfc >= (int)sizeof(path)) {
					closedir(dir);
					s_sysinfo.is_backlight = false;
					return;
				}
				found_prio = 3;
			}
		} else if (strncmp(buff, "firmware", 8) == 0) {
			if (found_prio < 2) {
				pfc = snprintf(s_backlight_dir_path, sizeof(s_backlight_dir_path), "/sys/class/backlight/%s/", ent->d_name);
				if (pfc < 0 || pfc >= (int)sizeof(path)) {
					closedir(dir);
					s_sysinfo.is_backlight = false;
					return;
				}
				found_prio = 2;
			}
		} else if (strncmp(buff, "unknown", 7) == 0) {
			if (found_prio < 1) {
				pfc = snprintf(s_backlight_dir_path, sizeof(s_backlight_dir_path), "/sys/class/backlight/%s/", ent->d_name);
				if (pfc < 0 || pfc >= (int)sizeof(path)) {
					closedir(dir);
					s_sysinfo.is_backlight = false;
					return;
				}
				found_prio = 1;
			}
		}
	}
	closedir(dir);
	s_sysinfo.is_backlight = (found_prio == 0);
	return;
}

// ==================== UPDATERS ====================

static void s_update_cpu_load(void) {
	FILE* f = fopen("/proc/stat", "r");
	static uint64_t old_total = 0;
	static uint64_t old_idle = 0;
	if (!f)
		return;

	uint64_t user;
	uint64_t nice;
	uint64_t system;
	uint64_t idle;
	uint64_t iowait;
	uint64_t irq;
	uint64_t softirq;
	uint64_t steal;
	uint64_t guest;
	uint64_t guest_nice;
	fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal,
		   &guest, &guest_nice);
	uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
	idle = idle + iowait;

	uint64_t total_delta = total - old_total;
	uint64_t idle_delta = idle - old_idle;

	s_sysinfo.cpu_load = 100 * (total_delta - idle_delta) / total_delta;

	old_total = total;
	old_idle = idle;
	fclose(f);
}

static void s_update_mem_usage(void) {
	FILE* f = fopen("/proc/meminfo", "r");
	long val = 0;
	long mem_total = 0;
	long mem_available = 0;
	char key[32];
	if (!f)
		return;
	while (fscanf(f, "%31s %ld kB\n", key, &val) == 2) {
		if (strcmp(key, "MemTotal:") == 0)
			mem_total = val;
		if (strcmp(key, "MemAvailable:") == 0) {
			mem_available = val;
			break;
		}
	}

	s_sysinfo.mem_usage = (int)(100 * mem_available / mem_total);
	fclose(f);
}

static void s_update_cpu_temp(void) {
	FILE* f = fopen(s_temp_sensor_path, "r");
	if (!f)
		return;

	long temp = 0;
	fscanf(f, "%ld", &temp);
	fclose(f);
	s_sysinfo.cpu_temp = temp / 1000;
}

static void s_update_battery_pct(void) {
	char path[128];
	int pfc = snprintf(path, sizeof(path), "%scapacity", s_battery_dir_path);
	if (pfc < 0 || pfc >= (int)sizeof(path))
		return;

	FILE* f = fopen(path, "r");
	if (!f)
		return;

	fscanf(f, "%d", &s_sysinfo.bat_pct);
	fclose(f);
}

static void s_update_battery_status(void) {
	char path[128];
	int pfc = snprintf(path, sizeof(path), "%sstatus", s_battery_dir_path);
	if (pfc < 0 || pfc >= (int)sizeof(path))
		return;

	FILE* f = fopen(path, "r");
	if (!f)
		return;

	char buff[32];
	fgets(buff, sizeof(buff), f);
	fclose(f);

	if (strncmp(buff, "Discharging", 11) == 0)
		s_sysinfo.is_bat_charging = false;
	else
		s_sysinfo.is_bat_charging = true;
}

static void s_update_brightness(void) {
	char brightness_path[128];
	int pfc = snprintf(brightness_path, sizeof(brightness_path), "%sbrightness", s_backlight_dir_path);
	if (pfc < 0 || pfc >= (int)sizeof(brightness_path))
		return;
	char max_brightness_path[128];
	pfc = snprintf(max_brightness_path, sizeof(max_brightness_path), "%smax_brightness", s_backlight_dir_path);
	if (pfc < 0 || pfc >= (int)sizeof(brightness_path))
		return;

	int brightness;
	int max_brightness;

	FILE* f = fopen(brightness_path, "r");
	if (!f)
		return;
	fscanf(f, "%d", &brightness);
	fclose(f);

	f = fopen(max_brightness_path, "r");
	if (!f)
		return;
	fscanf(f, "%d", &max_brightness);
	fclose(f);

	s_sysinfo.backlight_pct = 100 * brightness / max_brightness;
}

static void s_update_audio(void) {
	FILE* f = popen("wpctl get-volume @DEFAULT_SINK@", "r");

	char mute[32];
	float vol;
	s_sysinfo.is_audio_muted = (fscanf(f, "Volume: %f [%s]\n", &vol, mute) == 2);
	s_sysinfo.volume_pct = (int)(vol * 100.f);
	pclose(f);

	f = popen("wpctl inspect @DEFAULT_SINK@ | grep alsa.card_name", "r");
	if (fscanf(f, "%*[^=]= \"%31[^\"]", s_sysinfo.audio_sink_name) != 1)
		s_sysinfo.audio_sink_name[0] = '\0';
	pclose(f);
}

static void s_update_network(DBusConnection* conn) {
	// TODO Rewrite it to use NM DBus interface
	(void)conn;
	FILE* f = popen("nmcli -t -f CONNECTIVITY general", "r");
	if (!f) {
		s_sysinfo.is_internet_connected = false;
		return;
	}
	char val[32];
	fgets(val, sizeof(val), f);
	s_sysinfo.is_internet_connected = (strncmp(val, "full", 4) == 0);
	if (!s_sysinfo.is_internet_connected)
		return;
	pclose(f);

	f = popen("nmcli -t -f TYPE,STATE,CONNECTION device", "r");
	if (!f) {
		s_sysinfo.is_internet_wireless = false;
		s_sysinfo.wifi_ssid[0] = '\0';
		return;
	}

	char line[256];

	while (fgets(line, sizeof(line), f)) {
		char* type = strtok(line, ":");
		char* state = strtok(NULL, ":");
		char* name = strtok(NULL, "\n");

		if (strncmp(state, "connected", 9) == 0) {
			if (strncmp(type, "wifi", 4) == 0) {
				s_sysinfo.is_internet_wireless = true;
				strncpy(s_sysinfo.wifi_ssid, name, sizeof(s_sysinfo.wifi_ssid));
				s_sysinfo.wifi_ssid[sizeof(s_sysinfo.wifi_ssid) - 1] = '\0';
			} else if (strncmp(type, "ethernet", 8) == 0) {
				s_sysinfo.is_internet_wireless = false;
				s_sysinfo.wifi_ssid[0] = '\0';
				pclose(f);
				return;
			}
		}
	}
	pclose(f);
}

static void s_config_set_default(void) {
	// 0 - fg
	// 1 - bg
	// 2 - border
	// Enum with this is defined in drwl.h and only used there!?
	colors[SchemeNorm][0] = 0xbbbbbbff; // #bbbbbb
	colors[SchemeNorm][1] = 0x222222ff; // #222222
	colors[SchemeNorm][2] = 0x444444ff; // #444444

	colors[SchemeSel][0] = 0xeeeeeeff; // #eeeeee
	colors[SchemeSel][1] = 0x005577ff; // #005577
	colors[SchemeSel][2] = 0x005577ff; // #005577

	colors[SchemeUrg][0] = 0;		   // #000000
	colors[SchemeUrg][1] = 0;		   // #000000
	colors[SchemeUrg][2] = 0x770000ff; // #770000

	s_config.bemenu_border = 0;
	s_config.bemenu_lines = 0;
	s_config.bemenu_color_border = 0x285577; // #285577
	s_config.bemenu_color_fg[0] = 0xffffff;	 // #ffffff
	s_config.bemenu_color_fg[1] = 0xffffff;	 // #ffffff
	s_config.bemenu_color_fg[2] = 0xffffff;	 // #ffffff
	s_config.bemenu_color_fg[3] = 0xffffff;	 // #ffffff
	s_config.bemenu_color_bg[0] = 0x222222;	 // #222222
	s_config.bemenu_color_bg[1] = 0x282828;	 // #282828
	s_config.bemenu_color_bg[2] = 0x285577;	 // #285577
	s_config.bemenu_color_bg[3] = 0x222222;	 // #222222
}

static void s_update_config(const char* dirpath) {
	s_config_set_default();
	char path[128];
	int pfc = snprintf(path, sizeof(path), "%s/config", dirpath);
	if (pfc < 0 || pfc >= (int)sizeof(path))
		die("snprintf failed: path buffer too small");
	FILE* f = fopen(path, "r");
	if (!f) {
		f = fopen("/etc/dwl-rc/config", "r");
		if (!f) {
			s_config_set_default();
			return;
		}
	}

	char* buffer = NULL;
	size_t n = 0;
	while (getline(&buffer, &n, f) > 0) {
		uint32_t cols[4];
		int bemenu = 0;
		if (sscanf(buffer, "color_normal: #%6x #%6x #%6x\n", &cols[0], &cols[1], &cols[2]) == 3) {
			for (int i = 0; i < 3; ++i)
				colors[SchemeNorm][i] = (cols[i] << 8) | 0xff;
		} else if (sscanf(buffer, "color_selected: #%6x #%6x #%6x\n", &cols[0], &cols[1], &cols[2]) == 3) {
			for (int i = 0; i < 3; ++i)
				colors[SchemeSel][i] = (cols[i] << 8) | 0xff;
		} else if (sscanf(buffer, "color_urgent: #%6x #%6x #%6x\n", &cols[0], &cols[1], &cols[2]) == 3) {
			for (int i = 0; i < 3; ++i)
				colors[SchemeUrg][i] = (cols[i] << 8) | 0xff;
		} else if (sscanf(buffer, "bemenu_border: %d\n", &bemenu) == 1) {
			s_config.bemenu_border = CLAMP(bemenu, 0, 99);
		} else if (sscanf(buffer, "bemenu_lines: %d\n", &bemenu) == 1) {
			s_config.bemenu_lines = CLAMP(bemenu, 0, 99);
		} else if (sscanf(buffer, "bemenu_colors_fg: #%6x #%6x #%6x #%6x", &cols[0], &cols[1], &cols[2], &cols[3]) == 4) {
			for (int i = 0; i < 4; ++i)
				s_config.bemenu_color_fg[i] = cols[i] & 0xffffff;
		} else if (sscanf(buffer, "bemenu_colors_bg: #%6x #%6x #%6x #%6x", &cols[0], &cols[1], &cols[2], &cols[3]) == 4) {
			for (int i = 0; i < 4; ++i)
				s_config.bemenu_color_bg[i] = cols[i] & 0xffffff;
		} else if (sscanf(buffer, "bemenu_color_border: #%6x", &cols[0]) == 1) {
			s_config.bemenu_color_border = cols[0] & 0xffffff;
		}
	}
	free(buffer);
	fclose(f);
}

// ==================== CALLBACKS ====================

static struct wl_event_source* s_timer;
static int s_timer_timeout = 5000;
static redraw_callback s_redraw_bar_cb;
static redraw_callback s_redraw_all_cb;
static struct udev* udev;
static struct udev_monitor* udev_mon;

static int s_timer_update_callback(void* data) {
	s_update_cpu_load();
	s_update_cpu_temp();
	s_update_mem_usage();
	s_update_battery_pct();

	s_redraw_bar_cb();

	wl_event_source_timer_update(s_timer, *(int*)data);

	return 0;
}

static int s_network_debounce_callback(void* data) {
	DBusConnection* conn = (DBusConnection*)data;
	s_update_network(conn);
	s_redraw_bar_cb();
	return 0;
}

static struct wl_event_source* timer = NULL;
static DBusHandlerResult s_network_callback(DBusConnection* conn, DBusMessage* msg, void* data) {
	struct wl_event_loop* loop = (struct wl_event_loop*)data;
	if (!timer)
		timer = wl_event_loop_add_timer(loop, s_network_debounce_callback, (void*)conn);
	wl_event_source_timer_update(timer, 500);
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int s_audio_callback(int fd, uint32_t mask, void* data) {
	(void)mask;
	char buffer[4096];
	ssize_t len = 0;
	bool update = false;
	for (;;) {
		len = read(fd, buffer, sizeof(buffer));
		if (len == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			break;
		else if (len == -1)
			return -1;
		if (len == 0)
			return -1;

		if (strstr(buffer, "sink") || strstr(buffer, "source") || strstr(buffer, "server"))
			update = true;
	}
	if (update) {
		s_update_audio();
		s_redraw_bar_cb();
	}
	return 0;
}

static int s_udev_callback(int fd, uint32_t mask, void* data) {
	struct udev_device* dev = udev_monitor_receive_device(udev_mon);
	if (!dev)
		return 0;
	const char* subsystem = udev_device_get_subsystem(dev);
	if (strncmp(subsystem, "backlight", 9) == 0)
		s_update_brightness();
	if (strncmp(subsystem, "power_supply", 12) == 0)
		s_update_battery_status();
	udev_device_unref(dev);
	s_redraw_bar_cb();
	return 0;
}

static int s_config_change_callback(int fd, unsigned int mask, void* data) {
	const char* path = (const char*)data;
	char buf[4096];

	ssize_t len = read(fd, buf, sizeof(buf));

	for (char* ptr = buf; ptr < buf + len;) {
		struct inotify_event* ev = (struct inotify_event*)ptr;

		if (ev->len && strcmp(ev->name, "config") == 0) {
			s_update_config(path);
			s_redraw_all_cb();
		}

		ptr += sizeof(struct inotify_event) + ev->len;
	}
	return 0;
}

// ==================== PUBLIC ====================

sysinfo_t get_system_info(void) { return s_sysinfo; }

void system_info_init_event_loop(int timeout, struct wl_event_loop* loop, redraw_callback cb) {
	s_timer_timeout = timeout;
	s_redraw_bar_cb = cb;

	// Init paths
	s_init_temp_sensor();
	s_init_battery_path();
	s_init_backlight_path();

	// Register polled updates
	s_timer = wl_event_loop_add_timer(loop, s_timer_update_callback, (void*)&s_timer_timeout);
	(void)s_timer_update_callback((void*)&s_timer_timeout);

	// Inotify subscriptions
	udev = udev_new();
	udev_mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "power_supply", NULL);
	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "backlight", NULL);
	udev_monitor_enable_receiving(udev_mon);
	int udev_fd = udev_monitor_get_fd(udev_mon);

	s_update_brightness();
	s_update_battery_status();

	wl_event_loop_add_fd(loop, udev_fd, WL_EVENT_READABLE, s_udev_callback, (void*)udev_mon);

	// Register audio subscription
	int pa_pipefd[2];
	pipe(pa_pipefd);
	pid_t pa_sub_pid = fork();
	if (pa_sub_pid == 0) {
		dup2(pa_pipefd[1], STDOUT_FILENO);
		close(pa_pipefd[0]);
		close(pa_pipefd[1]);
		execlp("pactl", "pactl", "subscribe", NULL);
		_exit(1);
	}

	close(pa_pipefd[1]);
	int pa_fd = pa_pipefd[0];
	fcntl(pa_fd, F_SETFL, fcntl(pa_fd, F_GETFL) | O_NONBLOCK);
	s_update_audio();
	wl_event_loop_add_fd(loop, pa_fd, WL_EVENT_READABLE, s_audio_callback, NULL);

	// Dbus subscription for NetworkManager
	DBusError dbus_err;
	dbus_error_init(&dbus_err);

	DBusConnection* dbus_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_err);
	if (dbus_error_is_set(&dbus_err) || dbus_conn == NULL)
		die("System info dbus connection creation failed");

	startbus(dbus_conn, loop);

	s_update_network(dbus_conn);
	dbus_connection_add_filter(dbus_conn, s_network_callback, (void*)loop, NULL);
	dbus_bus_add_match(dbus_conn,
					   "type='signal',"
					   "sender='org.freedesktop.NetworkManager',"
					   "interface='org.freedesktop.DBus.Properties',"
					   "member='PropertiesChanged'",
					   &dbus_err);
	dbus_connection_flush(dbus_conn);
}

// Dynamic colors
void config_init_event_loop(struct wl_event_loop* loop, redraw_callback cb) {
	s_redraw_all_cb = cb;
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0)
		die("Failed to init inotify");

	static char path[128];
	const char* xdg_conf = getenv("XDG_CONFIG_HOME");
	if (xdg_conf) {
		int pfc = snprintf(path, sizeof(path), "%s/dwl-rc", xdg_conf);
		if (pfc < 0 || pfc >= (int)sizeof(path))
			die("snprintf failed: path buffer too small");
		goto config_register_fd;
	}

	const char* home = getenv("HOME");
	if (home) {
		int pfc = snprintf(path, sizeof(path), "%s/.config/dwl-rc", home);
		if (pfc < 0 || pfc >= (int)sizeof(path))
			die("snprintf failed: path buffer too small");
		goto config_register_fd;
	}

	const char* user = getenv("USER");
	if (user) {
		int pfc = snprintf(path, sizeof(path), "/home/%s/.config/dwl-rc", user);
		if (pfc < 0 || pfc >= (int)sizeof(path))
			die("snprintf failed: path buffer too small");
		goto config_register_fd;
	}

	die("Ur env is stuoid");

config_register_fd: {
	int wd = inotify_add_watch(fd, path, IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE);
	s_config_set_default();
	if (wd < 0) {
		close(fd);
		return;
	}
	s_update_config(path);

	wl_event_loop_add_fd(loop, fd, WL_EVENT_READABLE, s_config_change_callback, (void*)path);
}
}

// Custom commands
const void* get_proc_arg(const char* name) {
	if (strcmp(name, "run") == 0) {
		static const char* bemenu[] = {"bemenu-run", "-C", "-i", "-p", ">", "-T", //
									   "-B",		 NULL,						  // 7
									   "-l",		 NULL,						  // 9
									   "--bdr",		 NULL,						  // 11
									   "--nf",		 NULL,						  // 13
									   "--nb",		 NULL,						  // 15
									   "--hf",		 NULL,						  // 17
									   "--hb",		 NULL,						  // 19
									   "--tf",		 NULL,						  // 21
									   "--tb",		 NULL,						  // 23
									   "--af",		 NULL,						  // 25
									   "--ab",		 NULL,						  // 27
									   NULL};
		static char border_buffer[3];
		static char lines_buffer[3];
		static char border_color_buffer[8];
		static char normal_fg_color_buffer[8];
		static char normal_bg_color_buffer[8];
		static char alt_fg_color_buffer[8];
		static char alt_bg_color_buffer[8];
		static char highlight_fg_color_buffer[8];
		static char highlight_bg_color_buffer[8];
		static char title_fg_color_buffer[8];
		static char title_bg_color_buffer[8];
		snprintf(border_buffer, sizeof(border_buffer), "%d", s_config.bemenu_border);
		snprintf(lines_buffer, sizeof(lines_buffer), "%d", s_config.bemenu_lines);
		snprintf(border_color_buffer, sizeof(border_color_buffer), "#%x", s_config.bemenu_color_border);
		snprintf(normal_fg_color_buffer, sizeof(normal_fg_color_buffer), "#%x", s_config.bemenu_color_fg[0]);
		snprintf(normal_bg_color_buffer, sizeof(normal_bg_color_buffer), "#%x", s_config.bemenu_color_bg[0]);
		snprintf(alt_fg_color_buffer, sizeof(alt_fg_color_buffer), "#%x", s_config.bemenu_color_fg[1]);
		snprintf(alt_bg_color_buffer, sizeof(alt_bg_color_buffer), "#%x", s_config.bemenu_color_bg[1]);
		snprintf(highlight_fg_color_buffer, sizeof(highlight_fg_color_buffer), "#%x", s_config.bemenu_color_fg[2]);
		snprintf(highlight_bg_color_buffer, sizeof(highlight_bg_color_buffer), "#%x", s_config.bemenu_color_bg[2]);
		snprintf(title_fg_color_buffer, sizeof(title_fg_color_buffer), "#%x", s_config.bemenu_color_fg[3]);
		snprintf(title_bg_color_buffer, sizeof(title_bg_color_buffer), "#%x", s_config.bemenu_color_bg[3]);
		bemenu[7] = border_buffer;
		bemenu[9] = lines_buffer;
		bemenu[11] = border_color_buffer;
		bemenu[13] = normal_fg_color_buffer;
		bemenu[15] = normal_bg_color_buffer;
		bemenu[17] = highlight_fg_color_buffer;
		bemenu[19] = highlight_bg_color_buffer;
		bemenu[21] = title_fg_color_buffer;
		bemenu[23] = title_bg_color_buffer;
		bemenu[25] = alt_fg_color_buffer;
		bemenu[27] = alt_bg_color_buffer;
		return bemenu;
	}
	if (strcmp(name, "term") == 0) {
		static const char* foot[] = {"foot", NULL};
		return foot;
	}
	if (strcmp(name, "browser") == 0) {
		static const char* browser[] = {"firefox", NULL};
		return browser;
	}
	return NULL;
}
