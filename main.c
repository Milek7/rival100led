#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <sys/sysinfo.h>
#include <hidapi/hidapi.h>

float get_mem_usage()
{
	struct sysinfo info;
	sysinfo(&info);
	return (float)(info.totalram - info.freeram - info.bufferram) /
	       (float)info.totalram;
}

long int prev_idle = 0, prev_busy = 0;

float get_cpu_usage()
{
	FILE *fp = fopen("/proc/stat","r");
	long int user, nice, system, idle, iowait,
	         irq, softirq, steal, guest, guest_nice;
	fscanf(fp, "%*s %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
	       &user, &nice, &system, &idle, &iowait,
	       &irq, &softirq, &steal, &guest, &guest_nice);
	fclose(fp);
	long int busy = user + nice + system + iowait +
	                irq + softirq + steal + guest + guest_nice;
	long int busy_diff = busy - prev_busy;
	long int idle_diff = idle - prev_idle;
	prev_busy = busy;
	prev_idle = idle;
	float usage = (float)busy_diff / (float)(busy_diff + idle_diff);
	if (usage > 1.0)
		usage = 1.0;
	return usage;
}

uint8_t packet[] =
{
	//report id
	0x00,       //R   //G   //B
	0x05, 0x00, 0x00, 0x00, 0x00
};

hid_device* find_device()
{
	hid_device *handle = 0;
	struct hid_device_info *devs, *dev;
	devs = hid_enumerate(0x1038, 0x1702);
	dev = devs;
	while (dev)
	{
		if (dev->interface_number == 0)
		{
			handle = hid_open_path(dev->path);
			break;
		}
		dev = dev->next;
	}
	hid_free_enumeration(devs);
	return handle;
}

struct rgb_f
{
	float r, g, b;
};

struct rgb_f hl_to_rgb(float hue, float l)
{
	float c = 1.0 - fabs(2 * l - 1.0);
	float h = hue / 60.0;
	float x = c * (1.0 - fabs(fmod(h, 2) - 1));

	if (h >= 0.0 && h <= 1.0)
		return (struct rgb_f) {c, x, 0.0f};
	if (h >= 1.0 && h <= 2.0)
		return (struct rgb_f) {x, c, 0.0f};
	if (h >= 2.0 && h <= 3.0)
		return (struct rgb_f) {0.0f, c, x};
	if (h >= 3.0 && h <= 4.0)
		return (struct rgb_f) {0.0f, x, c};
	if (h >= 4.0 && h <= 5.0)
		return (struct rgb_f) {x, 0.0f, c};
	if (h >= 5.0 && h <= 6.0)
		return (struct rgb_f) {c, 0.0f, x};

	return (struct rgb_f) {0.0f, 0.0f, 0.0f};
}

sig_atomic_t stop_flag;

void stop(int sig)
{
	stop_flag = 1;
}

int main()
{
	signal(SIGINT, stop);
	signal(SIGTERM, stop);
	hid_init();
	hid_device *handle = 0;

	while (!stop_flag)
	{
		usleep(100 * 1000);
		if (!handle)
		{
			handle = find_device();
			continue;
		}

		float hue = 120.0f + get_mem_usage() * 240.0f;
		float luma = 0.1f + fmin(1.0, 2.0 * get_cpu_usage()) * 0.5;
		struct rgb_f f = hl_to_rgb(hue, luma);

		packet[3] = (uint8_t)(f.r * 255.0f);
		packet[4] = (uint8_t)(f.g * 255.0f);
		packet[5] = (uint8_t)(f.b * 255.0f);

		if (hid_write(handle, packet, sizeof(packet)) == -1)
		{
			hid_close(handle);
			handle = NULL;
		}
	}

	if (handle)
		hid_close(handle);

	hid_exit();
	return 0;
}
