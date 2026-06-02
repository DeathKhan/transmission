// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Utils.h"

#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"

#include <libtransmission/transmission.h> /* TR_RATIO_NA, TR_RATIO_INF */
#include <libtransmission/error.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h> /* tr_strratio() */
#include <libtransmission/values.h>
#include <libtransmission/version.h> /* SHORT_VERSION_STRING */
#include <libtransmission/web-utils.h>

#include <gdkmm/display.h>
#include <giomm/appinfo.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>
#include <glibmm/convert.h>
#include <glibmm/error.h>
#include <glibmm/i18n.h>
#include <glibmm/quark.h>
#include <glibmm/spawn.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/messagedialog.h>

#include "EnumDropdown.h"

#include <gdkmm/clipboard.h>
#include <gtkmm/eventcontroller.h>
#include <gtkmm/gesture.h>
#include <gtkmm/gestureclick.h>

#include <fmt/format.h>

#include <functional>
#include <memory>
#include <stack>
#include <stdexcept>
#include <utility>

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#if GTK_CHECK_VERSION(4, 0, 0) && defined(GDK_WINDOWING_X11)
#include <optional>

#include <gdk/x11/gdkx.h>
#endif

using namespace std::literals;

using namespace tr::Values;

/***
****
***/

void gtr_message(std::string const& message)
{
    // NOLINTNEXTLINE(*-vararg)
    g_message("%s", message.c_str());
}

void gtr_warning(std::string const& message)
{
    // NOLINTNEXTLINE(*-vararg)
    g_warning("%s", message.c_str());
}

void gtr_error(std::string const& message)
{
    // NOLINTNEXTLINE(*-vararg)
    g_error("%s", message.c_str());
}

/***
****
***/

Glib::ustring gtr_get_unicode_string(GtrUnicode uni)
{
    switch (uni)
    {
    case GtrUnicode::Up:
        return "\xE2\x96\xB4";

    case GtrUnicode::Down:
        return "\xE2\x96\xBE";

    case GtrUnicode::Inf:
        return "\xE2\x88\x9E";

    case GtrUnicode::Bullet:
        return "\xE2\x88\x99";

    default:
        return "err";
    }
}

Glib::ustring tr_strlratio(double ratio)
{
    return tr_strratio(ratio, Q_("None"), gtr_get_unicode_string(GtrUnicode::Inf).c_str());
}

Glib::ustring tr_strlsize(tr::Values::Storage const& storage)
{
    return storage.is_zero() ? Q_("None") : storage.to_string();
}

Glib::ustring tr_strlsize(guint64 n_bytes)
{
    return tr_strlsize(Storage{ n_bytes, Storage::Units::Bytes });
}

namespace
{
auto constexpr SecondsPerMinute = time_t{ 60 };
auto constexpr SecondsPerHour = time_t{ 3600 };
auto constexpr SecondsPerDay = time_t{ 86400 };

std::string tr_format_future_time(time_t seconds)
{
    if (auto const days_from_now = seconds / SecondsPerDay; days_from_now > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{days_from_now:L} day from now", "{days_from_now:L} days from now", days_from_now)),
            fmt::arg("days_from_now", days_from_now));
    }

    if (auto const hours_from_now = (seconds % SecondsPerDay) / SecondsPerHour; hours_from_now > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{hours_from_now:L} hour from now", "{hours_from_now:L} hours from now", hours_from_now)),
            fmt::arg("hours_from_now", hours_from_now));
    }

    if (auto const minutes_from_now = (seconds % SecondsPerHour) / SecondsPerMinute; minutes_from_now > 0)
    {
        return fmt::format(
            fmt::runtime(
                ngettext("{minutes_from_now:L} minute from now", "{minutes_from_now:L} minutes from now", minutes_from_now)),
            fmt::arg("minutes_from_now", minutes_from_now));
    }

    if (auto const seconds_from_now = seconds % SecondsPerMinute; seconds_from_now > 0)
    {
        return fmt::format(
            fmt::runtime(
                ngettext("{seconds_from_now:L} second from now", "{seconds_from_now:L} seconds from now", seconds_from_now)),
            fmt::arg("seconds_from_now", seconds_from_now));
    }

    return _("now");
}

std::string tr_format_past_time(time_t seconds)
{
    if (auto const days_ago = seconds / SecondsPerDay; days_ago > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{days_ago:L} day ago", "{days_ago:L} days ago", days_ago)),
            fmt::arg("days_ago", days_ago));
    }

    if (auto const hours_ago = (seconds % SecondsPerDay) / SecondsPerHour; hours_ago > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{hours_ago:L} hour ago", "{hours_ago:L} hours ago", hours_ago)),
            fmt::arg("hours_ago", hours_ago));
    }

    if (auto const minutes_ago = (seconds % SecondsPerHour) / SecondsPerMinute; minutes_ago > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{minutes_ago:L} minute ago", "{minutes_ago:L} minutes ago", minutes_ago)),
            fmt::arg("minutes_ago", minutes_ago));
    }

    if (auto const seconds_ago = seconds % SecondsPerMinute; seconds_ago > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{seconds_ago:L} second ago", "{seconds_ago:L} seconds ago", seconds_ago)),
            fmt::arg("seconds_ago", seconds_ago));
    }

    return _("now");
}

} // namespace

std::string tr_format_time(time_t timestamp)
{
    if (auto const days = timestamp / SecondsPerDay; days > 0)
    {
        return fmt::format(fmt::runtime(ngettext("{days:L} day", "{days:L} days", days)), fmt::arg("days", days));
    }

    if (auto const hours = (timestamp % SecondsPerDay) / SecondsPerHour; hours > 0)
    {
        return fmt::format(fmt::runtime(ngettext("{hours:L} hour", "{hours:L} hours", hours)), fmt::arg("hours", hours));
    }

    if (auto const minutes = (timestamp % SecondsPerHour) / SecondsPerMinute; minutes > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{minutes:L} minute", "{minutes:L} minutes", minutes)),
            fmt::arg("minutes", minutes));
    }

    if (auto const seconds = timestamp % SecondsPerMinute; seconds > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{seconds:L} second", "{seconds:L} seconds", seconds)),
            fmt::arg("seconds", seconds));
    }

    return _("now");
}

std::string tr_format_time_left(time_t timestamp)
{
    if (auto const days_left = timestamp / SecondsPerDay; days_left > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{days_left:L} day left", "{days_left:L} days left", days_left)),
            fmt::arg("days_left", days_left));
    }

    if (auto const hours_left = (timestamp % SecondsPerDay) / SecondsPerHour; hours_left > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{hours_left:L} hour left", "{hours_left:L} hours left", hours_left)),
            fmt::arg("hours_left", hours_left));
    }

    if (auto const minutes_left = (timestamp % SecondsPerHour) / SecondsPerMinute; minutes_left > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{minutes_left:L} minute left", "{minutes_left:L} minutes left", minutes_left)),
            fmt::arg("minutes_left", minutes_left));
    }

    if (auto const seconds_left = timestamp % SecondsPerMinute; seconds_left > 0)
    {
        return fmt::format(
            fmt::runtime(ngettext("{seconds_left:L} second left", "{seconds_left:L} seconds left", seconds_left)),
            fmt::arg("seconds_left", seconds_left));
    }

    return _("now");
}

std::string tr_format_time_relative(time_t timestamp, time_t origin)
{
    return timestamp < origin ? tr_format_future_time(origin - timestamp) : tr_format_past_time(timestamp - origin);
}

void gtr_add_torrent_error_dialog(Gtk::Widget& child, tr_torrent* duplicate_torrent, std::string const& filename)
{
    Glib::ustring secondary;

    if (duplicate_torrent != nullptr)
    {
        secondary = fmt::format(
            fmt::runtime(_("The torrent file '{path}' is already in use by '{torrent_name}'.")),
            fmt::arg("path", filename),
            fmt::arg("torrent_name", tr_torrentName(duplicate_torrent)));
    }
    else
    {
        secondary = fmt::format(fmt::runtime(_("Couldn't add torrent file '{path}'")), fmt::arg("path", filename));
    }

    auto dialog = Gtk::AlertDialog::create();
    dialog->set_message(_("Couldn't open torrent"));
    dialog->set_detail(secondary);
    dialog->show(gtr_widget_get_window(child));
}

namespace
{

// NOTE: Estimated position (`get_position_from_allocation` vfunc is private)
std::optional<guint> get_position_from_allocation(Gtk::Widget& view, double view_x, double view_y)
{
    auto* child = view.pick(view_x, view_y);
    while (child != nullptr && child->get_css_name() != "row")
    {
        child = child->get_parent();
    }

    if (child == nullptr)
    {
        return {};
    }

    (void)child;
    (void)view;
    (void)view_x;
    (void)view_y;
    return {};
}

} // namespace

bool on_item_view_button_pressed_impl(
    Gtk::Widget& view,
    Glib::RefPtr<Gtk::SelectionModel> const& selection_model,
    double event_x,
    double event_y,
    bool context_menu_requested,
    std::function<void(double, double)> const& callback)
{
    if (context_menu_requested)
    {
        if (auto const position = get_position_from_allocation(view, event_x, event_y); position.has_value())
        {
            if (!selection_model->is_selected(position.value()))
            {
                selection_model->select_item(position.value(), true);
            }
        }

        if (callback)
        {
            callback(event_x, event_y);
        }

        return true;
    }

    return false;
}

bool on_item_view_button_pressed(
    Gtk::ListView& view,
    double event_x,
    double event_y,
    bool context_menu_requested,
    std::function<void(double, double)> const& callback)
{
    return on_item_view_button_pressed_impl(view, view.get_model(), event_x, event_y, context_menu_requested, callback);
}

bool on_item_view_button_pressed(
    Gtk::ColumnView& view,
    double event_x,
    double event_y,
    bool context_menu_requested,
    std::function<void(double, double)> const& callback)
{
    return on_item_view_button_pressed_impl(view, view.get_model(), event_x, event_y, context_menu_requested, callback);
}

bool on_item_view_button_released_impl(
    Gtk::Widget& view,
    Glib::RefPtr<Gtk::SelectionModel> const& selection_model,
    double event_x,
    double event_y)
{
    if (!get_position_from_allocation(view, event_x, event_y).has_value())
    {
        selection_model->unselect_all();
    }

    return false;
}

bool on_item_view_button_released(Gtk::ListView& view, double event_x, double event_y)
{
    return on_item_view_button_released_impl(view, view.get_model(), event_x, event_y);
}

bool on_item_view_button_released(Gtk::ColumnView& view, double event_x, double event_y)
{
    return on_item_view_button_released_impl(view, view.get_model(), event_x, event_y);
}

namespace
{

std::pair<int, int> convert_widget_to_bin_window_coords(Gtk::Widget const& /*view*/, int view_x, int view_y)
{
    return { view_x, view_y };
}

void setup_item_view_button_event_handling_impl(
    Gtk::Widget& view,
    std::function<bool(guint, TrGdkModifierType, double, double, bool)> const& press_callback,
    std::function<bool(double, double)> const& release_callback)
{
    auto controller = Gtk::GestureClick::create();
    controller->set_button(0);
    controller->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    if (press_callback)
    {
        controller->signal_pressed().connect(
            [&view, press_callback, controller](int /*n_press*/, double view_x, double view_y)
            {
                auto const [event_x, event_y] = convert_widget_to_bin_window_coords(
                    view,
                    static_cast<int>(view_x),
                    static_cast<int>(view_y));

                auto* const sequence = controller->get_current_sequence();
                auto const event = controller->get_last_event(sequence);

                if (event->get_event_type() == TR_GDK_EVENT_TYPE(BUTTON_PRESS))
                {
                    press_callback(
                        event->get_button(),
                        event->get_modifier_state(),
                        event_x,
                        event_y,
                        event->triggers_context_menu());
                }
            },
            false);
    }
    if (release_callback)
    {
        controller->signal_released().connect(
            [&view, release_callback, controller](int /*n_press*/, double view_x, double view_y)
            {
                auto const [event_x, event_y] = convert_widget_to_bin_window_coords(
                    view,
                    static_cast<int>(view_x),
                    static_cast<int>(view_y));

                auto* const sequence = controller->get_current_sequence();
                auto const event = controller->get_last_event(sequence);

                if (event->get_event_type() == TR_GDK_EVENT_TYPE(BUTTON_RELEASE))
                {
                    release_callback(event_x, event_y);
                }
            });
    }
    view.add_controller(controller);
}

} // namespace

void setup_item_view_button_event_handling(
    Gtk::ListView& view,
    std::function<bool(guint, TrGdkModifierType, double, double, bool)> const& press_callback,
    std::function<bool(double, double)> const& release_callback)
{
    setup_item_view_button_event_handling_impl(view, press_callback, release_callback);
}

void setup_item_view_button_event_handling(
    Gtk::ColumnView& view,
    std::function<bool(guint, TrGdkModifierType, double, double, bool)> const& press_callback,
    std::function<bool(double, double)> const& release_callback)
{
    setup_item_view_button_event_handling_impl(view, press_callback, release_callback);
}

void gtr_window_present(Gtk::Window& window)
{
    window.present();
}

void gtr_alert_error(Gtk::Window& parent, Glib::ustring const& message, Glib::ustring const& detail)
{
    auto dialog = Gtk::AlertDialog::create(message);
    if (!detail.empty())
    {
        dialog->set_detail(detail);
    }

    dialog->set_buttons({ _("_Close") });
    dialog->choose(parent, [](Glib::RefPtr<Gio::AsyncResult>&) {});
}

void gtr_alert_confirm(
    Gtk::Window& parent,
    Glib::ustring const& message,
    Glib::ustring const& detail,
    Glib::ustring const& accept_label,
    std::function<void(bool accepted)> callback)
{
    auto dialog = Gtk::AlertDialog::create(message);
    if (!detail.empty())
    {
        dialog->set_detail(detail);
    }

    dialog->set_buttons({ _("_Cancel"), accept_label });
    dialog->set_cancel_button(0);
    dialog->set_default_button(0);

    dialog->choose(
        parent,
        [dialog, cb = std::move(callback)](Glib::RefPtr<Gio::AsyncResult>& result) mutable
        {
            try
            {
                cb(dialog->choose_finish(result) == 1);
            }
            catch (Glib::Error const&)
            {
                cb(false);
            }
        });
}

bool gtr_file_trash_or_remove(std::string_view const filename, tr_error* error)
{
    g_return_val_if_fail(!filename.empty(), false);

    auto local_error = tr_error{};
    if (error == nullptr)
    {
        error = &local_error;
    }

    auto const file = Gio::File::create_for_path(std::string{ filename });
    bool trashed = false;

    if (gtr_pref_flag_get(TR_KEY_trash_can_enabled))
    {
        try
        {
            trashed = file->trash();
        }
        catch (Glib::Error const& e)
        {
            error->set(e.code(), TR_GLIB_EXCEPTION_WHAT(e));
            gtr_message(
                fmt::format(
                    fmt::runtime(_("Couldn't move '{path}' to trash: {error} ({error_code})")),
                    fmt::arg("path", filename),
                    fmt::arg("error", error->message()),
                    fmt::arg("error_code", error->code())));
        }
    }

    bool result = true;
    if (!trashed)
    {
        try
        {
            file->remove();
        }
        catch (Glib::Error const& e)
        {
            error->set(e.code(), TR_GLIB_EXCEPTION_WHAT(e));
            gtr_message(
                fmt::format(
                    fmt::runtime(_("Couldn't remove '{path}': {error} ({error_code})")),
                    fmt::arg("path", filename),
                    fmt::arg("error", error->message()),
                    fmt::arg("error_code", error->code())));
            result = false;
        }
    }

    return result;
}

namespace
{

void object_signal_notify_callback(GObject* object, GParamSpec* /*param_spec*/, gpointer data)
{
    if (object != nullptr && data != nullptr)
    {
        if (auto const* const slot = Glib::SignalProxyBase::data_to_slot(data); slot != nullptr)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            (*static_cast<sigc::slot<TrObjectSignalNotifyCallback> const*>(slot))(Glib::wrap(object, true));
        }
    }
}

} // namespace

Glib::SignalProxy<TrObjectSignalNotifyCallback> gtr_object_signal_notify(Glib::ObjectBase& object)
{
    static auto const object_signal_notify_info = Glib::SignalProxyInfo{
        .signal_name = "notify",
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        .callback = reinterpret_cast<GCallback>(&object_signal_notify_callback),
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        .notify_callback = reinterpret_cast<GCallback>(&object_signal_notify_callback),
    };

    return { &object, &object_signal_notify_info };
}

void gtr_object_notify_emit(Glib::ObjectBase& object)
{
    // NOLINTNEXTLINE(*-vararg)
    g_signal_emit_by_name(object.gobj(), "notify", nullptr);
}

std::string gtr_get_help_uri(std::string_view const relative_path)
{
    return fmt::format("https://transmissionbt.com/help/gtk/{}.{}x/{}", MAJOR_VERSION, MINOR_VERSION / 10, relative_path);
}

void gtr_open_file(std::string_view const base, std::string_view const relative_path)
{
    auto const filename = tr_pathbuf{ base, "/"sv, relative_path };
    gtr_open_file(filename.sv());
}

void gtr_open_file(std::string_view const filename)
{
    auto const filename_ustr = Glib::ustring{ filename.data(), filename.size() };
    gtr_open_uri(Glib::filename_to_uri(filename_ustr).raw());
}

void gtr_open_uri(std::string_view const uri)
{
    if (std::empty(uri))
    {
        return;
    }

    auto const uri_str = std::string{ uri };

    try
    {
        if (Gio::AppInfo::launch_default_for_uri(uri_str))
        {
            return;
        }
    }
    catch (Glib::Error const& e)
    {
        gtr_warning(
            fmt::format(
                fmt::runtime(_("Couldn't launch default application for URI '{uri}': {error} ({error_code})")),
                fmt::arg("uri", uri),
                fmt::arg("error", e.what()),
                fmt::arg("error_code", e.code())));
    }

    try
    {
        Glib::spawn_async({}, std::vector<std::string>{ "xdg-open", uri_str }, TR_GLIB_SPAWN_FLAGS(SEARCH_PATH));
        return;
    }
    catch (Glib::SpawnError const& e)
    {
        gtr_warning(
            fmt::format(
                fmt::runtime(_("Couldn't invoke xdg-open for URI '{uri}': {error} ({error_code})")),
                fmt::arg("uri", uri),
                fmt::arg("error", e.what()),
                fmt::arg("error_code", static_cast<int>(e.code()))));
    }

    gtr_message(fmt::format(fmt::runtime(_("Couldn't open '{url}'")), fmt::arg("url", uri)));
}

// ---

void gtr_combo_box_set_active_enum(Gtk::DropDown& dropdown, int const value)
{
    if (enum_dropdown_get_value(dropdown) == value)
    {
        return;
    }

    enum_dropdown_set_value(dropdown, value);
}

void gtr_combo_box_set_enum(Gtk::DropDown& dropdown, std::vector<std::pair<Glib::ustring, int>> const& items)
{
    enum_dropdown_init(dropdown, items);
}

int gtr_combo_box_get_active_enum(Gtk::DropDown const& dropdown)
{
    return enum_dropdown_get_value(dropdown);
}

void gtr_priority_combo_init(Gtk::DropDown& dropdown)
{
    priority_dropdown_init(dropdown);
}

// ---

void gtr_widget_set_visible(Gtk::Widget& widget, bool is_visible)
{
    static auto const ChildHiddenKey = Glib::Quark("gtr-child-hidden");

    auto* const widget_as_window = dynamic_cast<Gtk::Window*>(&widget);
    if (widget_as_window == nullptr)
    {
        widget.set_visible(is_visible);
        return;
    }

    /* toggle the transient children, too */
    auto windows = std::stack<Gtk::Window*>();
    windows.push(widget_as_window);

    while (!windows.empty())
    {
        auto* const window = windows.top();
        bool transient_child_found = false;

        for (auto* const top_level_window : Gtk::Window::list_toplevels())
        {
#if !GTKMM_CHECK_VERSION(4, 0, 0)
            if (top_level_window->get_window_type() != Gtk::WINDOW_TOPLEVEL)
            {
                continue;
            }
#endif

            if (top_level_window->get_transient_for() != window || top_level_window->get_visible() == is_visible)
            {
                continue;
            }

            windows.push(top_level_window);
            transient_child_found = true;
            break;
        }

        if (transient_child_found)
        {
            continue;
        }

        if (is_visible && window->get_data(ChildHiddenKey) != nullptr)
        {
            window->steal_data(ChildHiddenKey);
            window->set_visible(true);
        }
        else if (!is_visible)
        {
            window->set_data(ChildHiddenKey, GINT_TO_POINTER(1));
            window->set_visible(false);
        }

        windows.pop();
    }
}

Gtk::Window& gtr_widget_get_window(Gtk::Widget& widget)
{
    if (auto* const window = dynamic_cast<Gtk::Window*>(TR_GTK_WIDGET_GET_ROOT(widget)); window != nullptr)
    {
        return *window;
    }

#if defined(G_DISABLE_ASSERT)
    throw std::logic_error("Supplied widget doesn't have a window");
#else
    g_assert_not_reached();
#endif
}

void gtr_window_set_skip_taskbar_hint([[maybe_unused]] Gtk::Window& window, [[maybe_unused]] bool value)
{
#if GTK_CHECK_VERSION(4, 0, 0)
#if defined(GDK_WINDOWING_X11)
    if (auto* const surface = Glib::unwrap(window.get_surface()); GDK_IS_X11_SURFACE(surface))
    {
        gdk_x11_surface_set_skip_taskbar_hint(surface, value ? TRUE : FALSE);
    }
#endif
#else
    window.set_skip_taskbar_hint(value);
#endif
}

void gtr_window_set_urgency_hint([[maybe_unused]] Gtk::Window& window, [[maybe_unused]] bool value)
{
#if GTK_CHECK_VERSION(4, 0, 0)
#if defined(GDK_WINDOWING_X11)
    if (auto* const surface = Glib::unwrap(window.get_surface()); GDK_IS_X11_SURFACE(surface))
    {
        gdk_x11_surface_set_urgency_hint(surface, value ? TRUE : FALSE);
    }
#endif
#else
    window.set_urgency_hint(value);
#endif
}

void gtr_window_raise([[maybe_unused]] Gtk::Window& window)
{
#if !GTKMM_CHECK_VERSION(4, 0, 0)
    window.get_window()->raise();
#endif
}

// ---

void gtr_unrecognized_url_dialog(Gtk::Widget& parent, Glib::ustring const& url)
{
    auto detail = fmt::format(fmt::runtime(_("Transmission doesn't know how to use '{url}'")), fmt::arg("url", url));

    if (tr_magnet_metainfo{}.parseMagnet(url.raw()))
    {
        detail += "\n \n";
        detail += _("This magnet link appears to be intended for something other than BitTorrent.");
    }

    gtr_alert_error(
        gtr_widget_get_window(parent),
        fmt::format(fmt::runtime(_("Unsupported URL: '{url}'")), fmt::arg("url", url)),
        detail);
}

/***
****
***/

void gtr_paste_clipboard_url_into_entry(Gtk::Entry& entry)
{
    auto const process = [&entry](Glib::ustring const& text)
    {
        if (auto const sv = tr_strv_strip(text.raw());
            !sv.empty() && (tr_urlIsValid(sv) || tr_magnet_metainfo{}.parseMagnet(sv)))
        {
            entry.set_text(text);
            return true;
        }
        return false;
    };

#if GTKMM_CHECK_VERSION(4, 0, 0)
    auto const request = [](Glib::RefPtr<Gdk::Clipboard> const& clipboard, auto&& callback)
    {
        clipboard->read_text_async([clipboard, callback](Glib::RefPtr<Gio::AsyncResult>& result)
                                   { callback(clipboard->read_text_finish(result)); });
    };

    request(
        Gdk::Display::get_default()->get_primary_clipboard(),
        [request, process](Glib::ustring const& text)
        {
            if (!process(text))
            {
                request(Gdk::Display::get_default()->get_clipboard(), process);
            }
        });
#else
    for (auto const& str : { Gtk::Clipboard::get(GDK_SELECTION_PRIMARY)->wait_for_text(),
                             Gtk::Clipboard::get(GDK_SELECTION_CLIPBOARD)->wait_for_text() })
    {
        if (process(str))
        {
            break;
        }
    }
#endif
}

/***
****
***/

void gtr_label_set_text(Gtk::Label& lb, Glib::ustring const& text)
{
    if (lb.get_text() != text)
    {
        lb.set_text(text);
    }
}

std::string gtr_get_full_resource_path(std::string const& rel_path)
{
    static auto const BasePath = "/com/transmissionbt/transmission/"s;
    return BasePath + rel_path;
}

/***
****
***/

size_t const max_recent_dirs = size_t{ 4 };

std::list<std::string> gtr_get_recent_dirs(std::string const& pref)
{
    std::list<std::string> list;

    for (size_t i = 0; i < max_recent_dirs; ++i)
    {
        auto const key = fmt::format("recent-{}-dir-{}", pref, i + 1);

        if (auto const val = gtr_pref_string_get(tr_quark_new(key)); !val.empty())
        {
            list.push_back(val);
        }
    }

    return list;
}

void gtr_save_recent_dir(std::string const& pref, Glib::RefPtr<Session> const& core, std::string const& dir)
{
    if (dir.empty())
    {
        return;
    }

    auto list = gtr_get_recent_dirs(pref);

    /* if it was already in the list, remove it */
    list.remove(dir);

    /* add it to the front of the list */
    list.push_front(dir);

    /* save the first max_recent_dirs directories */
    list.resize(max_recent_dirs);
    int i = 0;
    for (auto const& d : list)
    {
        auto const key = fmt::format("recent-{}-dir-{}", pref, ++i);
        gtr_pref_string_set(tr_quark_new(key), d);
    }

    if (core->get_session() != nullptr)
    {
        gtr_pref_save(core->get_session());
    }
    else
    {
        gtr_pref_save_client_only();
    }
}
