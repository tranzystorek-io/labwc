#include "labwc.h"
#include "common/log.h"
#include "common/bug-on.h"

static void handle_commit(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, commit);
	BUG_ON(!view->surface);

	/* Must receive commit signal before accessing surface->current* */
	view->w = view->surface->current.width;
	view->h = view->surface->current.height;
}

static void handle_map(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, map);
	view->impl->map(view);
}

static void handle_unmap(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, unmap);
	view->impl->unmap(view);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->link);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_configure.link);
	free(view);
}

static void handle_request_configure(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	wlr_xwayland_surface_configure(view->xwayland_surface, event->x,
				       event->y, event->width, event->height);
}

static void configure(struct view *view, struct wlr_box geo)
{
	wlr_xwayland_surface_configure(view->xwayland_surface, (int16_t)geo.x,
				       (int16_t)geo.y, (uint16_t)geo.width,
				       (uint16_t)geo.height);
}

static void _close(struct view *view)
{
	wlr_xwayland_surface_close(view->xwayland_surface);
}

static bool want_ssd(struct view *view)
{
	if (view->xwayland_surface->decorations !=
	    WLR_XWAYLAND_SURFACE_DECORATIONS_ALL)
		return false;
	return true;
}

static void map(struct view *view)
{
	view->mapped = true;
	view->x = view->xwayland_surface->x;
	view->y = view->xwayland_surface->y;
	view->surface = view->xwayland_surface->surface;
	if (!view->been_mapped) {
		view->show_server_side_deco = want_ssd(view);
		view_init_position(view);
	}
	view->been_mapped = true;

	/* Add commit here, as xwayland map/unmap can change the wlr_surface */
	wl_signal_add(&view->xwayland_surface->surface->events.commit,
		      &view->commit);
	view->commit.notify = handle_commit;

	view_focus(view);
}

static void unmap(struct view *view)
{
	view->mapped = false;
	wl_list_remove(&view->commit.link);
	view_focus(view_next(view));
}

static const struct view_impl xwl_view_impl = {
	.configure = configure,
	.close = _close,
	.map = map,
	.unmap = unmap,
};

void xwayland_surface_new(struct wl_listener *listener, void *data)
{
	struct server *server =
		wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xsurface = data;
	wlr_xwayland_surface_ping(xsurface);

	/*
	 * We do not create 'views' for xwayland override_redirect surfaces,
	 * but add them to server.unmanaged_surfaces so that we can render them
	 */
	if (xsurface->override_redirect) {
		xwayland_unmanaged_create(xsurface);
		return;
	}

	struct view *view = calloc(1, sizeof(struct view));
	view->server = server;
	view->type = LAB_XWAYLAND_VIEW;
	view->impl = &xwl_view_impl;
	view->xwayland_surface = xsurface;

	view->map.notify = handle_map;
	wl_signal_add(&xsurface->events.map, &view->map);
	view->unmap.notify = handle_unmap;
	wl_signal_add(&xsurface->events.unmap, &view->unmap);
	view->destroy.notify = handle_destroy;
	wl_signal_add(&xsurface->events.destroy, &view->destroy);
	view->request_configure.notify = handle_request_configure;
	wl_signal_add(&xsurface->events.request_configure,
		      &view->request_configure);

	wl_list_insert(&view->server->views, &view->link);
}