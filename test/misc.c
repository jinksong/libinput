/*
 * Copyright © 2014 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <config.h>

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <unistd.h>

#include "litest.h"

static int open_restricted(const char *path, int flags, void *data)
{
	int fd = open(path, flags);
	return fd < 0 ? -errno : fd;
}
static void close_restricted(int fd, void *data)
{
	close(fd);
}

const struct libinput_interface simple_interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

static struct libevdev_uinput *
create_simple_test_device(const char *name, ...)
{
	va_list args;
	struct libevdev_uinput *uinput;
	struct libevdev *evdev;
	unsigned int type, code;
	int rc;
	struct input_absinfo abs = {
		.value = -1,
		.minimum = 0,
		.maximum = 100,
		.fuzz = 0,
		.flat = 0,
		.resolution = 100,
	};

	evdev = libevdev_new();
	ck_assert(evdev != NULL);
	libevdev_set_name(evdev, name);

	va_start(args, name);

	while ((type = va_arg(args, unsigned int)) != -1 &&
	       (code = va_arg(args, unsigned int)) != -1) {
		const struct input_absinfo *a = NULL;
		if (type == EV_ABS)
			a = &abs;
		libevdev_enable_event_code(evdev, type, code, a);
	}

	va_end(args);

	rc = libevdev_uinput_create_from_device(evdev,
						LIBEVDEV_UINPUT_OPEN_MANAGED,
						&uinput);
	ck_assert_int_eq(rc, 0);
	libevdev_free(evdev);

	return uinput;
}

START_TEST(event_conversion_device_notify)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_event *event;
	int device_added = 0, device_removed = 0;

	uinput = create_simple_test_device("test device",
					   EV_REL, REL_X,
					   EV_REL, REL_Y,
					   EV_KEY, BTN_LEFT,
					   EV_KEY, BTN_MIDDLE,
					   EV_KEY, BTN_LEFT,
					   -1, -1);
	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, libevdev_uinput_get_devnode(uinput));

	libinput_dispatch(li);
	libinput_suspend(li);
	libinput_resume(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_DEVICE_ADDED ||
		    type == LIBINPUT_EVENT_DEVICE_REMOVED) {
			struct libinput_event_device_notify *dn;
			struct libinput_event *base;
			dn = libinput_event_get_device_notify_event(event);
			base = libinput_event_device_notify_get_base_event(dn);
			ck_assert(event == base);

			if (type == LIBINPUT_EVENT_DEVICE_ADDED)
				device_added++;
			else if (type == LIBINPUT_EVENT_DEVICE_REMOVED)
				device_removed++;

			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
		}

		libinput_event_destroy(event);
	}

	libinput_destroy(li);
	libevdev_uinput_destroy(uinput);

	ck_assert_int_gt(device_added, 0);
	ck_assert_int_gt(device_removed, 0);
}
END_TEST

START_TEST(event_conversion_pointer)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_event *event;
	int motion = 0, button = 0;

	uinput = create_simple_test_device("test device",
					   EV_REL, REL_X,
					   EV_REL, REL_Y,
					   EV_KEY, BTN_LEFT,
					   EV_KEY, BTN_MIDDLE,
					   EV_KEY, BTN_LEFT,
					   -1, -1);
	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, libevdev_uinput_get_devnode(uinput));

	/* Queue at least two relative motion events as the first one may
	 * be absorbed by the pointer acceleration filter. */
	libevdev_uinput_write_event(uinput, EV_REL, REL_X, -1);
	libevdev_uinput_write_event(uinput, EV_REL, REL_Y, -1);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);
	libevdev_uinput_write_event(uinput, EV_REL, REL_X, -1);
	libevdev_uinput_write_event(uinput, EV_REL, REL_Y, -1);
	libevdev_uinput_write_event(uinput, EV_KEY, BTN_LEFT, 1);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_POINTER_MOTION ||
		    type == LIBINPUT_EVENT_POINTER_BUTTON) {
			struct libinput_event_pointer *p;
			struct libinput_event *base;
			p = libinput_event_get_pointer_event(event);
			base = libinput_event_pointer_get_base_event(p);
			ck_assert(event == base);

			if (type == LIBINPUT_EVENT_POINTER_MOTION)
				motion++;
			else if (type == LIBINPUT_EVENT_POINTER_BUTTON)
				button++;

			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
		}
		libinput_event_destroy(event);
	}

	libinput_destroy(li);
	libevdev_uinput_destroy(uinput);

	ck_assert_int_gt(motion, 0);
	ck_assert_int_gt(button, 0);
}
END_TEST

START_TEST(event_conversion_pointer_abs)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_event *event;
	int motion = 0, button = 0;

	uinput = create_simple_test_device("test device",
					   EV_ABS, ABS_X,
					   EV_ABS, ABS_Y,
					   EV_KEY, BTN_LEFT,
					   EV_KEY, BTN_MIDDLE,
					   EV_KEY, BTN_LEFT,
					   -1, -1);
	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, libevdev_uinput_get_devnode(uinput));
	libinput_dispatch(li);

	libevdev_uinput_write_event(uinput, EV_ABS, ABS_X, 10);
	libevdev_uinput_write_event(uinput, EV_ABS, ABS_Y, 50);
	libevdev_uinput_write_event(uinput, EV_KEY, BTN_LEFT, 1);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);
	libevdev_uinput_write_event(uinput, EV_ABS, ABS_X, 30);
	libevdev_uinput_write_event(uinput, EV_ABS, ABS_Y, 30);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE ||
		    type == LIBINPUT_EVENT_POINTER_BUTTON) {
			struct libinput_event_pointer *p;
			struct libinput_event *base;
			p = libinput_event_get_pointer_event(event);
			base = libinput_event_pointer_get_base_event(p);
			ck_assert(event == base);

			if (type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE)
				motion++;
			else if (type == LIBINPUT_EVENT_POINTER_BUTTON)
				button++;

			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
		}
		libinput_event_destroy(event);
	}

	libinput_destroy(li);
	libevdev_uinput_destroy(uinput);

	ck_assert_int_gt(motion, 0);
	ck_assert_int_gt(button, 0);
}
END_TEST

START_TEST(event_conversion_key)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_event *event;
	int key = 0;

	uinput = create_simple_test_device("test device",
					   EV_KEY, KEY_A,
					   EV_KEY, KEY_B,
					   -1, -1);
	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, libevdev_uinput_get_devnode(uinput));
	libinput_dispatch(li);

	libevdev_uinput_write_event(uinput, EV_KEY, KEY_A, 1);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);
	libevdev_uinput_write_event(uinput, EV_KEY, KEY_A, 0);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type == LIBINPUT_EVENT_KEYBOARD_KEY) {
			struct libinput_event_keyboard *k;
			struct libinput_event *base;
			k = libinput_event_get_keyboard_event(event);
			base = libinput_event_keyboard_get_base_event(k);
			ck_assert(event == base);

			key++;

			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_touch_event(event) == NULL);
		}
		libinput_event_destroy(event);
	}

	libinput_destroy(li);
	libevdev_uinput_destroy(uinput);

	ck_assert_int_gt(key, 0);
}
END_TEST

START_TEST(event_conversion_touch)
{
	struct libevdev_uinput *uinput;
	struct libinput *li;
	struct libinput_event *event;
	int touch = 0;

	uinput = create_simple_test_device("test device",
					   EV_KEY, BTN_TOUCH,
					   EV_ABS, ABS_X,
					   EV_ABS, ABS_Y,
					   EV_ABS, ABS_MT_SLOT,
					   EV_ABS, ABS_MT_TRACKING_ID,
					   EV_ABS, ABS_MT_POSITION_X,
					   EV_ABS, ABS_MT_POSITION_Y,
					   -1, -1);
	li = libinput_path_create_context(&simple_interface, NULL);
	libinput_path_add_device(li, libevdev_uinput_get_devnode(uinput));
	libinput_dispatch(li);

	libevdev_uinput_write_event(uinput, EV_KEY, BTN_TOOL_FINGER, 1);
	libevdev_uinput_write_event(uinput, EV_KEY, BTN_TOUCH, 1);
	libevdev_uinput_write_event(uinput, EV_ABS, ABS_X, 10);
	libevdev_uinput_write_event(uinput, EV_ABS, ABS_Y, 10);
	libevdev_uinput_write_event(uinput, EV_ABS, ABS_MT_SLOT, 0);
	libevdev_uinput_write_event(uinput, EV_ABS, ABS_MT_TRACKING_ID, 1);
	libevdev_uinput_write_event(uinput, EV_ABS, ABS_MT_POSITION_X, 10);
	libevdev_uinput_write_event(uinput, EV_ABS, ABS_MT_POSITION_Y, 10);
	libevdev_uinput_write_event(uinput, EV_SYN, SYN_REPORT, 0);

	libinput_dispatch(li);

	while ((event = libinput_get_event(li))) {
		enum libinput_event_type type;
		type = libinput_event_get_type(event);

		if (type >= LIBINPUT_EVENT_TOUCH_DOWN &&
		    type <= LIBINPUT_EVENT_TOUCH_FRAME) {
			struct libinput_event_touch *t;
			struct libinput_event *base;
			t = libinput_event_get_touch_event(event);
			base = libinput_event_touch_get_base_event(t);
			ck_assert(event == base);

			touch++;

			ck_assert(libinput_event_get_device_notify_event(event) == NULL);
			ck_assert(libinput_event_get_pointer_event(event) == NULL);
			ck_assert(libinput_event_get_keyboard_event(event) == NULL);
		}
		libinput_event_destroy(event);
	}

	libinput_destroy(li);
	libevdev_uinput_destroy(uinput);

	ck_assert_int_gt(touch, 0);
}
END_TEST

int main (int argc, char **argv) {
	litest_add_no_device("events:conversion", event_conversion_device_notify);
	litest_add_no_device("events:conversion", event_conversion_pointer);
	litest_add_no_device("events:conversion", event_conversion_pointer_abs);
	litest_add_no_device("events:conversion", event_conversion_key);
	litest_add_no_device("events:conversion", event_conversion_touch);

	return litest_run(argc, argv);
}