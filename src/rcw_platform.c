/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2009 - Vic Lee
 * Copyright (C) 2014-2015 Antenore Gatta, Fabio Castelli, Giovanni Panozzo
 * Copyright (C) 2016-2023 Antenore Gatta, Giovanni Panozzo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *  In addition, as a special exception, the copyright holders give
 *  permission to link the code of portions of this program with the
 *  OpenSSL library under certain conditions as described in each
 *  individual source file, and distribute linked combinations
 *  including the two.
 *  You must obey the GNU General Public License in all respects
 *  for all of the code used other than OpenSSL. *  If you modify
 *  file(s) with this exception, you may extend this exception to your
 *  version of the file(s), but you are not obligated to do so. *  If you
 *  do not wish to do so, delete this exception statement from your
 *  version. *  If you delete this exception statement from all source
 *  files in the program, then also delete it here.
 *
 */

#include "rcw_platform.h"

#include "config.h"
#if defined(HAVE_KDE_OUTPUT_ORDER_V1)
#include "rcw_platform_kde.h"
#endif

#if defined(GDK_WINDOWING_WAYLAND) && defined(HAVE_WAYLAND_CLIENT)
#include <gdk/gdkwayland.h>
#include <wayland-client.h>

typedef struct {
	gint x;
	gint y;
	gint mode_width;
	gint mode_height;
} WlOutputMode;

static void remmina_wl_output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
				       int32_t physical_width, int32_t physical_height, int32_t subpixel,
				       const char *make, const char *model, int32_t transform)
{
	WlOutputMode *o = data;
	o->x = x;
	o->y = y;
}

static void remmina_wl_output_mode(void *data, struct wl_output *output, uint32_t flags,
				   int32_t width, int32_t height, int32_t refresh)
{
	WlOutputMode *o = data;
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		o->mode_width = width;
		o->mode_height = height;
	}
}

static void remmina_wl_output_done(void *data, struct wl_output *output) {}
static void remmina_wl_output_scale(void *data, struct wl_output *output, int32_t factor) {}
static void remmina_wl_output_name(void *data, struct wl_output *output, const char *name) {}
static void remmina_wl_output_description(void *data, struct wl_output *output, const char *description) {}

static const struct wl_output_listener remmina_wl_output_listener = {
	.geometry = remmina_wl_output_geometry,
	.mode = remmina_wl_output_mode,
	.done = remmina_wl_output_done,
	.scale = remmina_wl_output_scale,
	.name = remmina_wl_output_name,
	.description = remmina_wl_output_description,
};

static void remmina_wl_registry_global(void *data, struct wl_registry *registry, uint32_t name,
				       const char *interface, uint32_t version)
{
	GPtrArray *outputs = data;
	if (strcmp(interface, wl_output_interface.name) == 0) {
		WlOutputMode *o = g_new0(WlOutputMode, 1);
		g_ptr_array_add(outputs, o);
		wl_output_add_listener(wl_registry_bind(registry, name, &wl_output_interface,
						       MIN(version, 4)), &remmina_wl_output_listener, o);
	}
}

static const struct wl_registry_listener remmina_wl_registry_listener = {
	.global = remmina_wl_registry_global,
};

/* Physical size of the output at the given compositor position */
static gboolean remmina_wayland_output_mode(gint x, gint y, gint *width, gint *height)
{
	struct wl_display *display;
	struct wl_registry *registry;
	GPtrArray *outputs;
	gboolean found = FALSE;
	guint i;

	display = wl_display_connect(NULL);
	if (!display)
		return FALSE;

	outputs = g_ptr_array_new_with_free_func(g_free);
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &remmina_wl_registry_listener, outputs);
	wl_display_roundtrip(display);
	wl_display_roundtrip(display);

	for (i = 0; i < outputs->len && !found; i++) {
		WlOutputMode *o = g_ptr_array_index(outputs, i);
		if (o->mode_width > 0 && o->mode_height > 0 && o->x == x && o->y == y) {
			*width = o->mode_width;
			*height = o->mode_height;
			found = TRUE;
		}
	}
	if (!found) {
		for (i = 0; i < outputs->len; i++) {
			WlOutputMode *o = g_ptr_array_index(outputs, i);
			if (o->mode_width > 0 && o->mode_height > 0) {
				*width = o->mode_width;
				*height = o->mode_height;
				found = TRUE;
				break;
			}
		}
	}

	g_ptr_array_free(outputs, TRUE);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return found;
}
#endif

const GdkRectangle rcw_first_monitor_geometry() {
	GdkRectangle result = {};
#if defined(HAVE_KDE_OUTPUT_ORDER_V1)
	result = rcw_kde_first_monitor_geometry();
#endif

	return result;
}

gdouble rcw_monitor_display_scale(GdkMonitor *monitor)
{
	GdkRectangle geometry;
	gint width = 0, height = 0;
	gdouble scale;

	if (!monitor)
		return 0;

	gdk_monitor_get_geometry(monitor, &geometry);
	if (geometry.width <= 0 || geometry.height <= 0)
		return 0;

#if defined(GDK_WINDOWING_WAYLAND) && defined(HAVE_WAYLAND_CLIENT)
	if (GDK_IS_WAYLAND_DISPLAY(gdk_monitor_get_display(monitor)) &&
	    remmina_wayland_output_mode(geometry.x, geometry.y, &width, &height)) {
		/* Under Wayland GDK reports the monitor geometry in the compositor
		 * coordinate space, while GTK 3 can only report an integer scale
		 * factor. Compare it with the physical size of the output mode to get
		 * the fractional scale the compositor actually applies (e.g. 1.4). */
		scale = (gdouble)width / geometry.width;
		gdouble vertical = (gdouble)height / geometry.height;
		if (ABS(scale - vertical) < 0.1)
			scale = (scale + vertical) / 2;
		if (scale >= 1.0 && scale <= 8.0)
			return scale;
	}
#endif

	return gdk_monitor_get_scale_factor(monitor);
}