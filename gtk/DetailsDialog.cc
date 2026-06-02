// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "DetailsDialog.h"

#include "Actions.h"
#include "DetailsDialogRows.h"
#include "FileList.h"
#include "FilterListModel.hh"
#include "GtkCompat.h"
#include "HigWorkarea.h" // GUI_PAD, GUI_PAD_BIG, GUI_PAD_SMALL
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission/quark.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/utils.h>
#include <libtransmission/values.h>
#include <libtransmission/variant.h>
#include <libtransmission/web-utils.h>

#include <gdkmm/pixbuf.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/quark.h>
#include <glibmm/ustring.h>
#include <giomm/liststore.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/columnview.h>
#include <gtkmm/columnviewcolumn.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listitem.h>
#include <gtkmm/listview.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/noselection.h>
#include <gtkmm/notebook.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/sortlistmodel.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/textbuffer.h>
#include <gtkmm/textview.h>
#include <gtkmm/tooltip.h>

#include <gtk/gtk.h>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib> // abort()
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

using namespace std::literals;

using namespace tr::Values;

class DetailsDialog::Impl
{
public:
    Impl(DetailsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

    void set_torrents(std::vector<tr_torrent_id_t> const& torrent_ids);
    void refresh();

private:
    void info_page_init(Glib::RefPtr<Gtk::Builder> const& builder);
    void peer_page_init(Glib::RefPtr<Gtk::Builder> const& builder);
    void tracker_page_init(Glib::RefPtr<Gtk::Builder> const& builder);
    void options_page_init(Glib::RefPtr<Gtk::Builder> const& builder);

    void on_details_window_size_allocated();

    bool onPeerViewQueryTooltip(int x, int y, bool keyboard_tip, Glib::RefPtr<Gtk::Tooltip> const& tooltip);
    void onMorePeerInfoToggled();
    void configure_peer_columns();

    void on_tracker_list_selection_changed();

    void on_tracker_list_add_button_clicked();
    void on_edit_trackers();
    void on_tracker_list_remove_button_clicked();
    void onScrapeToggled();
    void onBackupToggled();

    template<typename T>
    void torrent_set_field(tr_quark const key, T value)
    {
        auto params = tr_variant::Map{ 2U };
        params.try_emplace(key, std::forward<T>(value));
        params.try_emplace(TR_KEY_ids, Session::to_variant(ids_));
        core_->exec(TR_KEY_torrent_set, std::move(params));
    }

    void refreshInfo(std::vector<tr_torrent*> const& torrents);
    void refreshPeers(std::vector<tr_torrent*> const& torrents);
    void refreshTracker(std::vector<tr_torrent*> const& torrents);
    void refreshFiles(std::vector<tr_torrent*> const& torrents);
    void refreshOptions(std::vector<tr_torrent*> const& torrents);

    void refresh_from_rpc_maps(std::vector<tr_variant::Map const*> const& maps);
    void refreshInfoRpc(std::vector<tr_variant::Map const*> const& maps);
    void refreshOptionsRpc(std::vector<tr_variant::Map const*> const& maps);
    void refreshPeersRpc(std::vector<tr_variant::Map const*> const& maps);
    void refreshTrackerRpc(std::vector<tr_variant::Map const*> const& maps);
    void refreshFilesRpc(std::vector<tr_variant::Map const*> const& maps);

    void refreshPeerList(std::vector<tr_torrent*> const& torrents);
    void refreshWebseedList(std::vector<tr_torrent*> const& torrents);

    tr_torrent_id_t tracker_list_get_current_torrent_id() const;
    tr_torrent* tracker_list_get_current_torrent() const;

    std::vector<tr_torrent*> getTorrents() const;

private:
    DetailsDialog& dialog_;
    Glib::RefPtr<Session> const core_;

    Gtk::CheckButton* honor_limits_check_ = nullptr;
    Gtk::CheckButton* up_limited_check_ = nullptr;
    Gtk::SpinButton* up_limit_sping_ = nullptr;
    Gtk::CheckButton* down_limited_check_ = nullptr;
    Gtk::SpinButton* down_limit_spin_ = nullptr;
    Gtk::DropDown* bandwidth_combo_ = nullptr;

    Gtk::DropDown* ratio_combo_ = nullptr;
    Gtk::SpinButton* ratio_spin_ = nullptr;
    Gtk::DropDown* idle_combo_ = nullptr;
    Gtk::SpinButton* idle_spin_ = nullptr;
    Gtk::SpinButton* max_peers_spin_ = nullptr;

    sigc::connection honor_limits_check_tag_;
    sigc::connection up_limited_check_tag_;
    sigc::connection down_limited_check_tag_;
    sigc::connection down_limit_spin_tag_;
    sigc::connection up_limit_spin_tag_;
    sigc::connection bandwidth_combo_tag_;
    sigc::connection ratio_combo_tag_;
    sigc::connection ratio_spin_tag_;
    sigc::connection idle_combo_tag_;
    sigc::connection idle_spin_tag_;
    sigc::connection max_peers_spin_tag_;

    Gtk::Label* added_lb_ = nullptr;
    Gtk::Label* size_lb_ = nullptr;
    Gtk::Label* state_lb_ = nullptr;
    Gtk::Label* have_lb_ = nullptr;
    Gtk::Label* dl_lb_ = nullptr;
    Gtk::Label* ul_lb_ = nullptr;
    Gtk::Label* error_lb_ = nullptr;
    Gtk::Label* date_started_lb_ = nullptr;
    Gtk::Label* eta_lb_ = nullptr;
    Gtk::Label* last_activity_lb_ = nullptr;

    Gtk::Label* hash_lb_ = nullptr;
    Gtk::Label* privacy_lb_ = nullptr;
    Gtk::Label* origin_lb_ = nullptr;
    Gtk::Label* destination_lb_ = nullptr;
    Glib::RefPtr<Gtk::TextBuffer> comment_buffer_;

    std::unordered_map<std::string, Glib::RefPtr<DetailsPeerRow>> peer_hash_;
    std::unordered_map<std::string, Glib::RefPtr<DetailsWebseedRow>> webseed_hash_;
    Glib::RefPtr<Gio::ListStore<DetailsPeerRow>> peer_store_;
    Glib::RefPtr<Gio::ListStore<DetailsWebseedRow>> webseed_store_;
    Glib::RefPtr<DetailsPeerProgressSorter> peer_sorter_;
    Glib::RefPtr<Gtk::SortListModel> peer_sort_model_;
    Glib::RefPtr<Gtk::SingleSelection> peer_selection_;
    Gtk::ScrolledWindow* webseeds_scroll_ = nullptr;
    Gtk::ColumnView* webseeds_view_ = nullptr;
    Gtk::ColumnView* peer_view_ = nullptr;
    Gtk::CheckButton* more_peer_details_check_ = nullptr;
    Glib::RefPtr<Gio::ListStore<DetailsTrackerRow>> tracker_store_;
    std::unordered_map<std::string, Glib::RefPtr<DetailsTrackerRow>> tracker_hash_;
    Glib::RefPtr<DetailsTrackerBackupFilter> tracker_backup_filter_;
    Glib::RefPtr<FilterListModel<DetailsTrackerRow>> trackers_filtered_;
    Glib::RefPtr<Gtk::SingleSelection> tracker_selection_;
    Glib::RefPtr<Gtk::SignalListItemFactory> tracker_item_factory_;
    Gtk::Button* add_tracker_button_ = nullptr;
    Gtk::Button* edit_trackers_button_ = nullptr;
    Gtk::Button* remove_tracker_button_ = nullptr;
    Gtk::ListView* tracker_view_ = nullptr;
    Gtk::CheckButton* scrape_check_ = nullptr;
    Gtk::CheckButton* all_check_ = nullptr;

    FileList* file_list_ = nullptr;
    Gtk::Label* file_label_ = nullptr;

    std::vector<tr_torrent_id_t> ids_;
    sigc::connection periodic_refresh_tag_;

    // tracker_list string keyed by torrent id — populated when fetching remote properties
    std::unordered_map<tr_torrent_id_t, std::string> rpc_tracker_lists_;

    Glib::Quark const TORRENT_ID_KEY = Glib::Quark("tr-torrent-id-key");
    Glib::Quark const TEXT_BUFFER_KEY = Glib::Quark("tr-text-buffer-key");
    Glib::Quark const URL_ENTRY_KEY = Glib::Quark("tr-url-entry-key");

    static guint last_page_;
};

guint DetailsDialog::Impl::last_page_ = 0;

std::vector<tr_torrent*> DetailsDialog::Impl::getTorrents() const
{
    std::vector<tr_torrent*> torrents;
    torrents.reserve(ids_.size());

    for (auto const id : ids_)
    {
        if (auto* torrent = core_->find_torrent(id); torrent != nullptr)
        {
            torrents.push_back(torrent);
        }
    }

    return torrents;
}

/****
*****
*****  OPTIONS TAB
*****
****/

namespace
{

void set_togglebutton_if_different(Gtk::CheckButton* toggle, sigc::connection& tag, bool value)
{
    bool const currentValue = toggle->get_active();

    if (currentValue != value)
    {
        tag.block();
        toggle->set_active(value);
        tag.unblock();
    }
}

void set_int_spin_if_different(Gtk::SpinButton* spin, sigc::connection& tag, int value)
{
    int const currentValue = spin->get_value_as_int();

    if (currentValue != value)
    {
        tag.block();
        spin->set_value(value);
        tag.unblock();
    }
}

void set_double_spin_if_different(Gtk::SpinButton* spin, sigc::connection& tag, double value)
{
    double const currentValue = spin->get_value();

    if ((int)(currentValue * 100) != (int)(value * 100))
    {
        tag.block();
        spin->set_value(value);
        tag.unblock();
    }
}

void unset_dropdown(Gtk::DropDown* dropdown, sigc::connection& tag)
{
    tag.block();
    dropdown->set_selected(GTK_INVALID_LIST_POSITION);
    tag.unblock();
}

} // namespace

void DetailsDialog::Impl::refreshOptions(std::vector<tr_torrent*> const& torrents)
{
    /***
    ****  Options Page
    ***/

    /* honor_limits_check */
    if (!torrents.empty())
    {
        bool const baseline = tr_torrentUsesSessionLimits(torrents.front());
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentUsesSessionLimits(torrent); });

        if (is_uniform)
        {
            set_togglebutton_if_different(honor_limits_check_, honor_limits_check_tag_, baseline);
        }
    }

    /* down_limited_check */
    if (!torrents.empty())
    {
        bool const baseline = tr_torrentUsesSpeedLimit(torrents.front(), tr_direction::Down);
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentUsesSpeedLimit(torrent, tr_direction::Down); });

        if (is_uniform)
        {
            set_togglebutton_if_different(down_limited_check_, down_limited_check_tag_, baseline);
        }
    }

    /* down_limit_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetSpeedLimit_KBps(torrents.front(), tr_direction::Down);
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetSpeedLimit_KBps(torrent, tr_direction::Down); });

        if (is_uniform)
        {
            set_int_spin_if_different(down_limit_spin_, down_limit_spin_tag_, baseline);
        }
    }

    /* up_limited_check */
    if (!torrents.empty())
    {
        bool const baseline = tr_torrentUsesSpeedLimit(torrents.front(), tr_direction::Up);
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentUsesSpeedLimit(torrent, tr_direction::Up); });

        if (is_uniform)
        {
            set_togglebutton_if_different(up_limited_check_, up_limited_check_tag_, baseline);
        }
    }

    /* up_limit_sping */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetSpeedLimit_KBps(torrents.front(), tr_direction::Up);
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetSpeedLimit_KBps(torrent, tr_direction::Up); });

        if (is_uniform)
        {
            set_int_spin_if_different(up_limit_sping_, up_limit_spin_tag_, baseline);
        }
    }

    /* bandwidth_combo */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetPriority(torrents.front());
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetPriority(torrent); });

        if (is_uniform)
        {
            bandwidth_combo_tag_.block();
            gtr_combo_box_set_active_enum(*bandwidth_combo_, baseline);
            bandwidth_combo_tag_.unblock();
        }
        else
        {
            unset_dropdown(bandwidth_combo_, bandwidth_combo_tag_);
        }
    }

    /* ratio_combo */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetRatioMode(torrents.front());
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetRatioMode(torrent); });

        if (is_uniform)
        {
            ratio_combo_tag_.block();
            gtr_combo_box_set_active_enum(*ratio_combo_, baseline);
            gtr_widget_set_visible(*ratio_spin_, baseline == TR_RATIOLIMIT_SINGLE);
            ratio_combo_tag_.unblock();
        }
    }

    /* ratio_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetRatioLimit(torrents.front());
        set_double_spin_if_different(ratio_spin_, ratio_spin_tag_, baseline);
    }

    /* idle_combo */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetIdleMode(torrents.front());
        bool const is_uniform = std::all_of(
            torrents.begin(),
            torrents.end(),
            [baseline](auto const* torrent) { return baseline == tr_torrentGetIdleMode(torrent); });

        if (is_uniform)
        {
            idle_combo_tag_.block();
            gtr_combo_box_set_active_enum(*idle_combo_, baseline);
            gtr_widget_set_visible(*idle_spin_, baseline == TR_IDLELIMIT_SINGLE);
            idle_combo_tag_.unblock();
        }
    }

    /* idle_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetIdleLimit(torrents.front());
        set_int_spin_if_different(idle_spin_, idle_spin_tag_, baseline);
    }

    /* max_peers_spin */
    if (!torrents.empty())
    {
        auto const baseline = tr_torrentGetPeerLimit(torrents.front());
        set_int_spin_if_different(max_peers_spin_, max_peers_spin_tag_, baseline);
    }
}

void DetailsDialog::Impl::options_page_init(Glib::RefPtr<Gtk::Builder> const& /*builder*/)
{
    auto const speed_units_kbyps_str = Speed::units().display_name(Speed::Units::KByps);

    honor_limits_check_tag_ = honor_limits_check_->signal_toggled().connect(
        [this]() { torrent_set_field(TR_KEY_honors_session_limits, honor_limits_check_->get_active()); });

    down_limited_check_->set_label(
        fmt::format(fmt::runtime(down_limited_check_->get_label().raw()), fmt::arg("speed_units", speed_units_kbyps_str)));
    down_limited_check_tag_ = down_limited_check_->signal_toggled().connect(
        [this]() { torrent_set_field(TR_KEY_download_limited, down_limited_check_->get_active()); });

    down_limit_spin_->set_adjustment(Gtk::Adjustment::create(0, 0, std::numeric_limits<int>::max(), 5));
    down_limit_spin_tag_ = down_limit_spin_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_download_limit, down_limit_spin_->get_value_as_int()); });

    up_limited_check_->set_label(
        fmt::format(fmt::runtime(up_limited_check_->get_label().raw()), fmt::arg("speed_units", speed_units_kbyps_str)));
    up_limited_check_tag_ = up_limited_check_->signal_toggled().connect(
        [this]() { torrent_set_field(TR_KEY_upload_limited, up_limited_check_->get_active()); });

    up_limit_sping_->set_adjustment(Gtk::Adjustment::create(0, 0, std::numeric_limits<int>::max(), 5));
    up_limit_spin_tag_ = up_limit_sping_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_upload_limit, up_limit_sping_->get_value_as_int()); });

    gtr_priority_combo_init(*bandwidth_combo_);
    bandwidth_combo_tag_ = bandwidth_combo_->property_selected().signal_changed().connect(
        [this]() { torrent_set_field(TR_KEY_bandwidth_priority, gtr_combo_box_get_active_enum(*bandwidth_combo_)); });

    gtr_combo_box_set_enum(
        *ratio_combo_,
        {
            { _("Use global settings"), TR_RATIOLIMIT_GLOBAL },
            { _("Seed regardless of ratio"), TR_RATIOLIMIT_UNLIMITED },
            { _("Stop seeding at ratio:"), TR_RATIOLIMIT_SINGLE },
        });
    ratio_combo_tag_ = ratio_combo_->property_selected().signal_changed().connect(
        [this]()
        {
            torrent_set_field(TR_KEY_seed_ratio_mode, gtr_combo_box_get_active_enum(*ratio_combo_));
            refresh();
        });
    ratio_spin_->set_adjustment(Gtk::Adjustment::create(0, 0, 1000, .05));
    ratio_spin_->set_width_chars(7);
    ratio_spin_tag_ = ratio_spin_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_seed_ratio_limit, ratio_spin_->get_value()); });

    gtr_combo_box_set_enum(
        *idle_combo_,
        {
            { _("Use global settings"), TR_IDLELIMIT_GLOBAL },
            { _("Seed regardless of activity"), TR_IDLELIMIT_UNLIMITED },
            { _("Stop seeding if idle for N minutes:"), TR_IDLELIMIT_SINGLE },
        });
    idle_combo_tag_ = idle_combo_->property_selected().signal_changed().connect(
        [this]()
        {
            torrent_set_field(TR_KEY_seed_idle_mode, gtr_combo_box_get_active_enum(*idle_combo_));
            refresh();
        });
    idle_spin_->set_adjustment(Gtk::Adjustment::create(1, 1, 40320, 5));
    idle_spin_tag_ = idle_spin_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_seed_idle_limit, idle_spin_->get_value_as_int()); });

    max_peers_spin_->set_adjustment(Gtk::Adjustment::create(1, 1, 3000, 5));
    max_peers_spin_tag_ = max_peers_spin_->signal_value_changed().connect(
        [this]() { torrent_set_field(TR_KEY_peer_limit, max_peers_spin_->get_value_as_int()); });
}

/****
*****
*****  INFO TAB
*****
****/

namespace
{

Glib::ustring activityString(int activity, bool finished)
{
    switch (activity)
    {
    case TR_STATUS_CHECK_WAIT:
        return _("Queued for verification");

    case TR_STATUS_CHECK:
        return _("Verifying local data");

    case TR_STATUS_DOWNLOAD_WAIT:
        return _("Queued for download");

    case TR_STATUS_DOWNLOAD:
        return C_("Verb", "Downloading");

    case TR_STATUS_SEED_WAIT:
        return _("Queued for seeding");

    case TR_STATUS_SEED:
        return C_("Verb", "Seeding");

    case TR_STATUS_STOPPED:
        return finished ? _("Finished") : _("Paused");

    default:
        g_assert_not_reached();
    }

    return {};
}

/* Only call gtk_text_buffer_set_text () if the new text differs from the old.
 * This way if the user has text selected, refreshing won't deselect it */
void gtr_text_buffer_set_text(Glib::RefPtr<Gtk::TextBuffer> const& b, Glib::ustring const& str)
{
    if (b->get_text() != str)
    {
        b->set_text(str);
    }
}

[[nodiscard]] std::string get_date_string(time_t t)
{
    return t == 0 ? _("N/A") : fmt::format("{:%x}", *std::localtime(&t));
}

[[nodiscard]] std::string get_date_time_string(time_t t)
{
    return t == 0 ? _("N/A") : fmt::format("{:%c}", *std::localtime(&t));
}

} // namespace

void DetailsDialog::Impl::refreshInfo(std::vector<tr_torrent*> const& torrents)
{
    auto const now = time(nullptr);
    Glib::ustring str;
    Glib::ustring const mixed = _("Mixed");
    Glib::ustring const no_torrent = _("No Torrents Selected");
    Glib::ustring stateString;
    uint64_t size_when_done = 0;
    auto const stats = tr_torrentStat(std::data(torrents), std::size(torrents));

    std::vector<tr_torrent_view> infos;
    infos.reserve(torrents.size());
    for (auto* const torrent : torrents)
    {
        infos.push_back(tr_torrentView(torrent));
    }

    /* privacy_lb */
    if (infos.empty())
    {
        str = no_torrent;
    }
    else
    {
        bool const baseline = infos.front().is_private;
        bool const is_uniform = std::all_of(
            infos.begin(),
            infos.end(),
            [baseline](auto const& info) { return info.is_private == baseline; });

        if (is_uniform)
        {
            str = baseline ? _("Private to this tracker -- DHT and PEX disabled") : _("Public torrent");
        }
        else
        {
            str = mixed;
        }
    }

    privacy_lb_->set_text(str);

    /* added_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const baseline = stats.front().added_date;
        bool const is_uniform = std::ranges::all_of(
            stats,
            [baseline](auto const& stat) { return stat.added_date == baseline; });

        if (is_uniform)
        {
            str = get_date_time_string(baseline);
        }
        else
        {
            str = mixed;
        }
    }

    added_lb_->set_text(str);

    /* origin_lb */
    if (infos.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const creator = tr_strv_strip(infos.front().creator != nullptr ? infos.front().creator : ""sv);
        auto const date = infos.front().date_created;
        auto const datestr = get_date_string(date);
        bool const mixed_creator = std::ranges::any_of(
            infos,
            [creator](auto const& info)
            {
                return creator != (info.creator != nullptr ? std::string_view{ info.creator } : ""sv);
            });
        bool const mixed_date = std::any_of(
            infos.begin(),
            infos.end(),
            [date](auto const& info) { return date != info.date_created; });

        bool const empty_creator = std::empty(creator);
        bool const empty_date = date == 0;

        if (mixed_creator || mixed_date)
        {
            str = mixed;
        }
        else if (!empty_creator && !empty_date)
        {
            str = fmt::format(
                fmt::runtime(_("Created by {creator} on {date}")),
                fmt::arg("creator", creator),
                fmt::arg("date", datestr));
        }
        else if (!empty_creator)
        {
            str = fmt::format(fmt::runtime(_("Created by {creator}")), fmt::arg("creator", creator));
        }
        else if (!empty_date)
        {
            str = fmt::format(fmt::runtime(_("Created on {date}")), fmt::arg("date", datestr));
        }
        else
        {
            str = _("N/A");
        }
    }

    origin_lb_->set_text(str);

    /* comment_buffer */
    if (infos.empty())
    {
        str.clear();
    }
    else
    {
        auto const baseline = Glib::ustring(infos.front().comment != nullptr ? infos.front().comment : "");
        bool const is_uniform = std::all_of(
            infos.begin(),
            infos.end(),
            [&baseline](auto const& info) { return baseline == (info.comment != nullptr ? info.comment : ""); });

        str = is_uniform ? baseline : mixed;
    }

    gtr_text_buffer_set_text(comment_buffer_, str);

    /* destination_lb */
    if (torrents.empty())
    {
        str = no_torrent;
    }
    else
    {
        std::string_view const baseline = tr_torrentGetDownloadDir(torrents.front());
        bool const is_uniform = std::ranges::all_of(
            torrents,
            [&baseline](auto const* torrent) { return baseline == tr_torrentGetDownloadDir(torrent); });

        str = is_uniform ? Glib::ustring{ baseline.data(), baseline.size() } : mixed;
    }

    destination_lb_->set_text(str);

    /* state_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const activity = stats.front().activity;
        bool const is_uniform = std::ranges::all_of(stats, [activity](auto const& st) { return activity == st.activity; });
        bool const all_finished = std::ranges::all_of(stats, [](auto const& st) { return st.finished; });

        str = is_uniform ? activityString(activity, all_finished) : mixed;
    }

    stateString = str;
    state_lb_->set_text(str);

    /* date started */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        time_t const baseline = stats.front().start_date;
        bool const is_uniform = std::ranges::all_of(stats, [baseline](auto const& st) { return baseline == st.start_date; });

        if (!is_uniform)
        {
            str = mixed;
        }
        else if (baseline <= 0 || stats[0].activity == TR_STATUS_STOPPED)
        {
            str = stateString;
        }
        else
        {
            str = tr_format_time(now - baseline);
        }
    }

    date_started_lb_->set_text(str);

    /* eta */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const baseline = stats.front().eta;
        auto const is_uniform = std::ranges::all_of(stats, [baseline](auto const& st) { return baseline == st.eta; });

        if (!is_uniform)
        {
            str = mixed;
        }
        else if (baseline < 0)
        {
            str = _("Unknown");
        }
        else
        {
            str = tr_format_time_left(baseline);
        }
    }

    eta_lb_->set_text(str);

    /* size_lb */
    {
        auto const piece_count = std::accumulate(
            std::begin(infos),
            std::end(infos),
            uint64_t{},
            [](auto sum, auto const& info) { return sum + info.n_pieces; });

        if (piece_count == 0)
        {
            str.clear();
        }
        else
        {
            auto const total_size = std::accumulate(
                std::begin(infos),
                std::end(infos),
                uint64_t{},
                [](auto sum, auto const& info) { return sum + info.total_size; });

            auto const file_count = std::accumulate(
                std::begin(torrents),
                std::end(torrents),
                std::size_t{},
                [](auto sum, auto const* tor) { return sum + tr_torrentFileCount(tor); });

            str = tr_strlsize(total_size);
            if (file_count > 0)
            {
                str += ' ';
                str += fmt::format(
                    fmt::runtime(ngettext("in {file_count:L} file", "in {file_count:L} files", file_count)),
                    fmt::arg("file_count", file_count));
            }

            auto const piece_size = std::empty(infos) ? uint32_t{} : infos.front().piece_size;
            auto const piece_size_is_uniform = std::all_of(
                std::begin(infos),
                std::end(infos),
                [piece_size](auto const& info) { return info.piece_size == piece_size; });

            if (piece_size_is_uniform)
            {
                str += ' ';
                str += fmt::format(
                    fmt::runtime(ngettext(
                        "({piece_count} BitTorrent piece @ {piece_size})",
                        "({piece_count} BitTorrent pieces @ {piece_size})",
                        piece_count)),
                    fmt::arg("piece_count", piece_count),
                    fmt::arg("piece_size", Memory{ piece_size, Memory::Units::Bytes }.to_string()));
            }
        }

        size_lb_->set_text(str);
    }

    /* have_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        uint64_t left_until_done = 0;
        uint64_t have_unchecked = 0;
        uint64_t have_valid = 0;
        uint64_t available = 0;

        for (auto const& st : stats)
        {
            have_unchecked += st.have_unchecked;
            have_valid += st.have_valid;
            size_when_done += st.size_when_done;
            left_until_done += st.left_until_done;
            available += st.size_when_done - st.left_until_done + st.have_unchecked + st.desired_available;
        }

        {
            double const d = size_when_done != 0 ? (100.0 * available) / size_when_done : 0;
            double const ratio = 100.0 * (size_when_done != 0 ? (have_valid + have_unchecked) / (double)size_when_done : 1);

            auto const avail = tr_strpercent(d);
            auto const buf2 = tr_strpercent(ratio);
            auto const total = tr_strlsize(have_unchecked + have_valid);
            auto const unver = tr_strlsize(have_unchecked);

            if (have_unchecked == 0 && left_until_done == 0)
            {
                str = fmt::format(
                    fmt::runtime(_("{current_size} ({percent_done}%)")),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2));
            }
            else if (have_unchecked == 0)
            {
                str = fmt::format(
                    // xgettext:no-c-format
                    fmt::runtime(_("{current_size} ({percent_done}% of {percent_available}% available)")),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2),
                    fmt::arg("percent_available", avail));
            }
            else
            {
                str = fmt::format(
                    // xgettext:no-c-format
                    fmt::runtime(
                        _("{current_size} ({percent_done}% of {percent_available}% available; {unverified_size} unverified)")),
                    fmt::arg("current_size", total),
                    fmt::arg("percent_done", buf2),
                    fmt::arg("percent_available", avail),
                    fmt::arg("unverified_size", unver));
            }
        }
    }

    have_lb_->set_text(str);

    // dl_lb
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const downloaded_str = tr_strlsize(
            std::accumulate(
                std::begin(stats),
                std::end(stats),
                uint64_t{ 0 },
                [](auto sum, auto const& st) { return sum + st.downloaded_ever; }));

        auto const failed = std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{ 0 },
            [](auto sum, auto const& st) { return sum + st.corrupt_ever; });

        if (failed != 0)
        {
            str = fmt::format(
                fmt::runtime(_("{downloaded_size} (+{discarded_size} discarded after failed checksum)")),
                fmt::arg("downloaded_size", downloaded_str),
                fmt::arg("discarded_size", tr_strlsize(failed)));
        }
        else
        {
            str = downloaded_str;
        }
    }

    dl_lb_->set_text(str);

    /* ul_lb */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const uploaded = std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{},
            [](auto sum, auto const& st) { return sum + st.uploaded_ever; });
        auto const denominator = std::accumulate(
            std::begin(stats),
            std::end(stats),
            uint64_t{},
            [](auto sum, auto const& st) { return sum + st.size_when_done; });
        str = fmt::format(
            fmt::runtime(_("{uploaded_size} (Ratio: {ratio})")),
            fmt::arg("uploaded_size", tr_strlsize(uploaded)),
            fmt::arg("ratio", tr_strlratio(tr_getRatio(uploaded, denominator))));
    }

    ul_lb_->set_text(str);

    /* hash_lb */
    if (infos.empty())
    {
        str = no_torrent;
    }
    else if (infos.size() == 1)
    {
        str = infos.front().hash_string;
    }
    else
    {
        str = mixed;
    }

    hash_lb_->set_text(str);

    /* error */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const& baseline = stats.front().error_string;
        bool const is_uniform = std::ranges::all_of(stats, [&baseline](auto const& st) { return baseline == st.error_string; });

        str = is_uniform ? Glib::ustring{ baseline } : mixed;
    }

    if (str.empty())
    {
        str = _("No errors");
    }

    error_lb_->set_text(str);

    /* activity date */
    if (stats.empty())
    {
        str = no_torrent;
    }
    else
    {
        auto const iter = std::ranges::max_element(
            stats,
            [](auto const& lhs, auto const& rhs) { return lhs.activity_date < rhs.activity_date; });
        time_t const latest = iter->activity_date;

        if (latest <= 0)
        {
            str = _("Never");
        }
        else if ((now - latest) < 5)
        {
            str = _("Active now");
        }
        else
        {
            str = tr_format_time_relative(now, latest);
        }
    }

    last_activity_lb_->set_text(str);
}

void DetailsDialog::Impl::info_page_init(Glib::RefPtr<Gtk::Builder> const& builder)
{
    comment_buffer_ = Gtk::TextBuffer::create();
    auto* tw = gtr_get_widget<Gtk::TextView>(builder, "comment_value_view");
    tw->set_buffer(comment_buffer_);
}

/****
*****
*****  PEERS TAB
*****
****/


namespace
{

template<typename RowT>
void remove_unupdated_rows(
    Glib::RefPtr<Gio::ListStore<RowT>> const& store,
    std::unordered_map<std::string, Glib::RefPtr<RowT>>& hash)
{
    for (guint i = store->get_n_items(); i > 0;)
    {
        --i;
        auto const row = store->get_item(i);
        if (row->get_was_updated())
        {
            continue;
        }

        hash.erase(row->get_key());
        store->remove(i);
    }
}

Glib::RefPtr<Gtk::SignalListItemFactory> make_peer_label_factory(
    std::function<Glib::ustring(Glib::RefPtr<DetailsPeerRow> const&)> const& getter,
    float const xalign = 0.0F)
{
    auto factory = Gtk::SignalListItemFactory::create();
    static auto const LabelKey = Glib::Quark("tr-details-peer-label");

    factory->signal_setup().connect(
        [xalign](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto* const label = Gtk::make_managed<Gtk::Label>();
            label->set_xalign(xalign);
            list_item->set_data(LabelKey, label);
            list_item->set_child(*label);
        });

    factory->signal_bind().connect(
        [getter](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto const row = gtr_ptr_dynamic_cast<DetailsPeerRow>(list_item->get_item());
            auto* const label = static_cast<Gtk::Label*>(list_item->get_data(LabelKey));
            if (row == nullptr || label == nullptr)
            {
                return;
            }

            label->set_label(getter(row));
        });

    return factory;
}

Glib::RefPtr<Gtk::SignalListItemFactory> make_peer_icon_factory(
    std::function<Glib::ustring(Glib::RefPtr<DetailsPeerRow> const&)> const& getter)
{
    auto factory = Gtk::SignalListItemFactory::create();
    static auto const ImageKey = Glib::Quark("tr-details-peer-image");

    factory->signal_setup().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto* const image = Gtk::make_managed<Gtk::Image>();
            image->set_valign(Gtk::Align::CENTER);
            list_item->set_data(ImageKey, image);
            list_item->set_child(*image);
        });

    factory->signal_bind().connect(
        [getter](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto const row = gtr_ptr_dynamic_cast<DetailsPeerRow>(list_item->get_item());
            auto* const image = static_cast<Gtk::Image*>(list_item->get_data(ImageKey));
            if (row == nullptr || image == nullptr)
            {
                return;
            }

            auto const icon = getter(row);
            if (icon.empty())
            {
                image->set_from_icon_name({});
            }
            else
            {
                image->set_from_icon_name(icon);
            }
        });

    return factory;
}

Glib::RefPtr<Gtk::SignalListItemFactory> make_peer_progress_factory()
{
    auto factory = Gtk::SignalListItemFactory::create();
    static auto const ProgressKey = Glib::Quark("tr-details-peer-progress");

    factory->signal_setup().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto* const progress = Gtk::make_managed<Gtk::ProgressBar>();
            progress->set_show_text(true);
            list_item->set_data(ProgressKey, progress);
            list_item->set_child(*progress);
        });

    factory->signal_bind().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto const row = gtr_ptr_dynamic_cast<DetailsPeerRow>(list_item->get_item());
            auto* const progress = static_cast<Gtk::ProgressBar*>(list_item->get_data(ProgressKey));
            if (row == nullptr || progress == nullptr)
            {
                return;
            }

            progress->set_fraction(row->get_progress() / 100.0);
            progress->set_text(fmt::format("{}%", row->get_progress()));
        });

    return factory;
}

void append_peer_column(
    Gtk::ColumnView& view,
    Glib::ustring const& title,
    Glib::RefPtr<Gtk::ListItemFactory> const& factory,
    bool const resizable = false)
{
    auto const column = Gtk::ColumnViewColumn::create(title, factory);
    column->set_resizable(resizable);
    view.append_column(column);
}

Glib::RefPtr<Gtk::SignalListItemFactory> make_webseed_label_factory(
    std::function<Glib::ustring(Glib::RefPtr<DetailsWebseedRow> const&)> const& getter,
    bool const expand = false)
{
    auto factory = Gtk::SignalListItemFactory::create();
    static auto const LabelKey = Glib::Quark("tr-details-webseed-label");

    factory->signal_setup().connect(
        [expand](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto* const label = Gtk::make_managed<Gtk::Label>();
            label->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);
            label->set_hexpand(expand);
            label->set_xalign(expand ? 0.0F : 1.0F);
            list_item->set_data(LabelKey, label);
            list_item->set_child(*label);
        });

    factory->signal_bind().connect(
        [getter](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto const row = gtr_ptr_dynamic_cast<DetailsWebseedRow>(list_item->get_item());
            auto* const label = static_cast<Gtk::Label*>(list_item->get_data(LabelKey));
            if (row == nullptr || label == nullptr)
            {
                return;
            }

            label->set_label(getter(row));
        });

    return factory;
}

} // namespace

void DetailsDialog::Impl::refreshPeers(std::vector<tr_torrent*> const& torrents)
{
    refreshPeerList(torrents);
    refreshWebseedList(torrents);
}

void DetailsDialog::Impl::refreshPeerList(std::vector<tr_torrent*> const& torrents)
{
    auto& hash = peer_hash_;
    auto const& store = peer_store_;

    std::vector<std::vector<tr_peer_stat>> peers;
    peers.reserve(torrents.size());
    for (auto const* const torrent : torrents)
    {
        peers.push_back(tr_torrentPeers(torrent));
    }

    for (guint i = 0, n = store->get_n_items(); i < n; ++i)
    {
        store->get_item(i)->set_was_updated(false);
    }

    auto make_key = [](tr_torrent const* tor, tr_peer_stat const& ps)
    {
        return fmt::format("{:d}.{:s}", tr_torrentId(tor), ps.addr);
    };

    for (size_t i = 0; i < torrents.size(); ++i)
    {
        auto const* tor = torrents.at(i);
        auto const& torrent_peers = peers.at(i);

        for (auto const& peer : torrent_peers)
        {
            auto const key = make_key(tor, peer);

            if (!hash.contains(key))
            {
                auto const row = DetailsPeerRow::create(key, tr_torrentName(tor), peer);
                store->append(row);
                hash.try_emplace(key, row);
            }
        }
    }

    for (size_t i = 0; i < torrents.size(); ++i)
    {
        auto const* tor = torrents.at(i);
        auto const& torrent_peers = peers.at(i);

        for (auto const& peer : torrent_peers)
        {
            auto const key = make_key(tor, peer);
            hash.at(key)->update(peer);
        }
    }

    remove_unupdated_rows(store, hash);
}

void DetailsDialog::Impl::refreshWebseedList(std::vector<tr_torrent*> const& torrents)
{
    auto has_any_webseeds = false;
    auto& hash = webseed_hash_;
    auto const& store = webseed_store_;

    auto make_key = [](tr_torrent const* tor, char const* url)
    {
        return fmt::format("{:d}.{:s}", tr_torrentId(tor), url);
    };

    for (guint i = 0, n = store->get_n_items(); i < n; ++i)
    {
        store->get_item(i)->set_was_updated(false);
    }

    for (auto const* const tor : torrents)
    {
        for (size_t j = 0, n = tr_torrentWebseedCount(tor); j < n; ++j)
        {
            has_any_webseeds = true;

            auto const* const url = tr_torrentWebseed(tor, j).url;
            auto const key = make_key(tor, url);

            if (!hash.contains(key))
            {
                auto const row = DetailsWebseedRow::create(key, url);
                store->append(row);
                hash.try_emplace(key, row);
            }
        }
    }

    for (auto const* const tor : torrents)
    {
        for (size_t j = 0, n = tr_torrentWebseedCount(tor); j < n; ++j)
        {
            auto const webseed = tr_torrentWebseed(tor, j);
            auto const key = make_key(tor, webseed.url);
            hash.at(key)->update(webseed);
        }
    }

    remove_unupdated_rows(store, hash);

    webseeds_scroll_->set_visible(has_any_webseeds);
}

std::optional<guint> get_column_view_row_position(Gtk::ColumnView& view, double view_x, double view_y)
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

    double top_x = 0;
    double top_y = 0;
    gtk_widget_translate_coordinates(child->gobj(), GTK_WIDGET(view.gobj()), 0, 0, &top_x, &top_y);
    auto const row_height = gtk_widget_get_height(child->gobj());
    if (row_height <= 0)
    {
        return {};
    }

    if (auto const adj = view.get_vadjustment())
    {
        return static_cast<guint>((top_y + adj->get_value()) / row_height);
    }

    return static_cast<guint>(top_y / row_height);
}

bool DetailsDialog::Impl::onPeerViewQueryTooltip(int x, int y, bool /*keyboard_tip*/, Glib::RefPtr<Gtk::Tooltip> const& tooltip)
{
    auto const position = get_column_view_row_position(*peer_view_, x, y);
    if (!position.has_value() || position.value() >= peer_sort_model_->get_n_items())
    {
        return false;
    }

    auto const row = gtr_ptr_dynamic_cast<DetailsPeerRow>(peer_sort_model_->get_object(position.value()));
    if (row == nullptr)
    {
        return false;
    }

    auto const name = row->get_torrent_name();
    auto const addr = row->get_address();
    auto const flagstr = row->get_flags();

    std::ostringstream gstr;
    gstr << "<b>" << Glib::Markup::escape_text(name) << "</b>\n" << addr << "\n \n";

    for (char const ch : flagstr)
    {
        char const* s = nullptr;

        switch (ch)
        {
        case 'O':
            s = _("Optimistic unchoke");
            break;

        case 'D':
            s = _("Downloading from this peer");
            break;

        case 'd':
            s = _("We would download from this peer if they would let us");
            break;

        case 'U':
            s = _("Uploading to peer");
            break;

        case 'u':
            s = _("We would upload to this peer if they asked");
            break;

        case 'K':
            s = _("Peer has unchoked us, but we're not interested");
            break;

        case '?':
            s = _("We unchoked this peer, but they're not interested");
            break;

        case 'E':
            s = _("Encrypted connection");
            break;

        case 'X':
            s = _("Peer was found through Peer Exchange (PEX)");
            break;

        case 'H':
            s = _("Peer was found through DHT");
            break;

        case 'I':
            s = _("Peer is an incoming connection");
            break;

        case 'T':
            s = _("Peer is connected over µTP");
            break;

        default:
            g_assert_not_reached();
        }

        if (s != nullptr)
        {
            gstr << ch << ": " << s << '\n';
        }
    }

    auto str = gstr.str();
    if (!str.empty())
    {
        str.resize(str.size() - 1);
    }

    tooltip->set_markup(str);
    return true;
}

void DetailsDialog::Impl::configure_peer_columns()
{
    if (auto const columns = peer_view_->get_columns())
    {
        for (guint i = columns->get_n_items(); i > 0;)
        {
            --i;
            if (auto const column = gtr_ptr_dynamic_cast<Gtk::ColumnViewColumn>(columns->get_object(i)))
            {
                peer_view_->remove_column(column);
            }
        }
    }

    append_peer_column(
        *peer_view_,
        {},
        make_peer_icon_factory([](auto const& row) { return row->get_encryption_stock_id(); }));

    append_peer_column(
        *peer_view_,
        _("Up"),
        make_peer_label_factory([](auto const& row) { return row->get_upload_rate_string(); }, 1.0F));

    bool const more = gtr_pref_flag_get(TR_KEY_show_extra_peer_details);

    if (more)
    {
        append_peer_column(
            *peer_view_,
            _("Up Reqs"),
            make_peer_label_factory([](auto const& row) { return row->get_upload_request_count_string(); }, 1.0F));
    }

    append_peer_column(
        *peer_view_,
        _("Down"),
        make_peer_label_factory([](auto const& row) { return row->get_download_rate_string(); }, 1.0F));

    if (more)
    {
        append_peer_column(
            *peer_view_,
            _("Dn Reqs"),
            make_peer_label_factory([](auto const& row) { return row->get_download_request_count_string(); }, 1.0F));
        append_peer_column(
            *peer_view_,
            _("Dn Blocks"),
            make_peer_label_factory([](auto const& row) { return row->get_blocks_downloaded_count_string(); }, 1.0F));
        append_peer_column(
            *peer_view_,
            _("Up Blocks"),
            make_peer_label_factory([](auto const& row) { return row->get_blocks_uploaded_count_string(); }, 1.0F));
        append_peer_column(
            *peer_view_,
            _("We Cancelled"),
            make_peer_label_factory([](auto const& row) { return row->get_reqs_cancelled_by_client_count_string(); }, 1.0F));
        append_peer_column(
            *peer_view_,
            _("They Cancelled"),
            make_peer_label_factory([](auto const& row) { return row->get_reqs_cancelled_by_peer_count_string(); }, 1.0F));
    }

    append_peer_column(*peer_view_, _("%"), make_peer_progress_factory());
    append_peer_column(
        *peer_view_,
        _("Flags"),
        make_peer_label_factory([](auto const& row) { return row->get_flags(); }));
    append_peer_column(
        *peer_view_,
        _("Address"),
        make_peer_label_factory([](auto const& row) { return row->get_address(); }));
    append_peer_column(
        *peer_view_,
        _("Client"),
        make_peer_label_factory([](auto const& row) { return row->get_client(); }));
}

void DetailsDialog::Impl::onMorePeerInfoToggled()
{
    tr_quark const key = TR_KEY_show_extra_peer_details;
    bool const value = more_peer_details_check_->get_active();
    core_->set_pref(key, value);
    configure_peer_columns();
}

void DetailsDialog::Impl::peer_page_init(Glib::RefPtr<Gtk::Builder> const& /*builder*/)
{
    webseed_store_ = Gio::ListStore<DetailsWebseedRow>::create();
    auto const webseed_model = Gtk::NoSelection::create(webseed_store_);
    webseeds_view_->set_model(webseed_model);

    append_peer_column(
        *webseeds_view_,
        _("Web Seeds"),
        make_webseed_label_factory([](auto const& row) { return row->get_url(); }, true),
        true);
    append_peer_column(
        *webseeds_view_,
        _("Down"),
        make_webseed_label_factory([](auto const& row) { return row->get_download_rate_string(); }));

    setup_item_view_button_event_handling(
        *webseeds_view_,
        {},
        [this](double view_x, double view_y) { return on_item_view_button_released(*webseeds_view_, view_x, view_y); });

    peer_store_ = Gio::ListStore<DetailsPeerRow>::create();
    peer_sorter_ = DetailsPeerProgressSorter::create();
    peer_sort_model_ = Gtk::SortListModel::create(peer_store_, peer_sorter_);
    peer_selection_ = Gtk::SingleSelection::create(peer_sort_model_);
    peer_view_->set_model(peer_selection_);
    peer_view_->set_has_tooltip(true);
    peer_view_->signal_query_tooltip().connect(sigc::mem_fun(*this, &Impl::onPeerViewQueryTooltip), false);
    setup_item_view_button_event_handling(
        *peer_view_,
        {},
        [this](double view_x, double view_y) { return on_item_view_button_released(*peer_view_, view_x, view_y); });

    configure_peer_columns();

    more_peer_details_check_->set_active(gtr_pref_flag_get(TR_KEY_show_extra_peer_details));
    more_peer_details_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onMorePeerInfoToggled));
}
/****
*****
*****  TRACKER
*****
****/

namespace
{

auto constexpr ErrMarkupBegin = "<span color='red'>"sv;
auto constexpr ErrMarkupEnd = "</span>"sv;
auto constexpr TimeoutMarkupBegin = "<span color='#246'>"sv;
auto constexpr TimeoutMarkupEnd = "</span>"sv;
auto constexpr SuccessMarkupBegin = "<span color='#080'>"sv;
auto constexpr SuccessMarkupEnd = "</span>"sv;

std::array<std::string_view, 3> const text_dir_mark = { ""sv, "\u200E"sv, "\u200F"sv };

void appendAnnounceInfo(tr_tracker_view const& tracker, time_t const now, Gtk::TextDirection direction, std::ostream& gstr)
{
    auto const dir_mark = text_dir_mark.at(static_cast<int>(direction));

    if (tracker.hasAnnounced && tracker.announceState != TR_TRACKER_INACTIVE)
    {
        gstr << '\n';
        gstr << dir_mark;
        auto const time_span_ago = tr_format_time_relative(now, tracker.lastAnnounceTime);

        if (tracker.lastAnnounceSucceeded)
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the peer text
                fmt::runtime(ngettext(
                    "Got a list of {markup_begin}{peer_count} peer{markup_end} {time_span_ago}",
                    "Got a list of {markup_begin}{peer_count} peers{markup_end} {time_span_ago}",
                    tracker.lastAnnouncePeerCount)),
                fmt::arg("markup_begin", SuccessMarkupBegin),
                fmt::arg("peer_count", tracker.lastAnnouncePeerCount),
                fmt::arg("markup_end", SuccessMarkupEnd),
                fmt::arg("time_span_ago", time_span_ago));
        }
        else if (tracker.lastAnnounceTimedOut)
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the time_span
                fmt::runtime(_("Peer list request {markup_begin}timed out {time_span_ago}{markup_end}; will retry")),
                fmt::arg("markup_begin", TimeoutMarkupBegin),
                fmt::arg("time_span_ago", time_span_ago),
                fmt::arg("markup_end", TimeoutMarkupEnd));
        }
        else
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the error
                fmt::runtime(_("Got an error '{markup_begin}{error}{markup_end}' {time_span_ago}")),
                fmt::arg("markup_begin", ErrMarkupBegin),
                fmt::arg("error", Glib::Markup::escape_text(std::data(tracker.lastAnnounceResult))),
                fmt::arg("markup_end", ErrMarkupEnd),
                fmt::arg("time_span_ago", time_span_ago));
        }
    }

    switch (tracker.announceState)
    {
    case TR_TRACKER_INACTIVE:
        gstr << '\n';
        gstr << dir_mark;
        gstr << _("No updates scheduled");
        break;

    case TR_TRACKER_WAITING:
        gstr << '\n';
        gstr << dir_mark;
        gstr << fmt::format(
            fmt::runtime(_("Asking for more peers {time_span_from_now}")),
            fmt::arg("time_span_from_now", tr_format_time_relative(now, tracker.nextAnnounceTime)));
        break;

    case TR_TRACKER_QUEUED:
        gstr << '\n';
        gstr << dir_mark;
        gstr << _("Queued to ask for more peers");
        break;

    case TR_TRACKER_ACTIVE:
        gstr << '\n';
        gstr << dir_mark;
        gstr << fmt::format(
            // {markup_begin} and {markup_end} should surround time_span_ago
            fmt::runtime(_("Asked for more peers {markup_begin}{time_span_ago}{markup_end}")),
            fmt::arg("markup_begin", "<small>"),
            fmt::arg("time_span_ago", tr_format_time_relative(now, tracker.lastAnnounceStartTime)),
            fmt::arg("markup_end", "</small>"));
        break;

    default:
        g_assert_not_reached();
    }
}

void appendScrapeInfo(tr_tracker_view const& tracker, time_t const now, Gtk::TextDirection direction, std::ostream& gstr)
{
    auto const dir_mark = text_dir_mark.at(static_cast<int>(direction));

    if (tracker.hasScraped)
    {
        gstr << '\n';
        gstr << dir_mark;
        auto const time_span_ago = tr_format_time_relative(now, tracker.lastScrapeTime);

        if (tracker.lastScrapeSucceeded)
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the seeder/leecher text
                fmt::runtime(_(
                    "Tracker had {markup_begin}{seeder_count} {seeder_or_seeders} and {leecher_count} {leecher_or_leechers}{markup_end} {time_span_ago}")),
                fmt::arg("seeder_count", tracker.seederCount),
                fmt::arg("seeder_or_seeders", ngettext("seeder", "seeders", tracker.seederCount)),
                fmt::arg("leecher_count", tracker.leecherCount),
                fmt::arg("leecher_or_leechers", ngettext("leecher", "leechers", tracker.leecherCount)),
                fmt::arg("time_span_ago", time_span_ago),
                fmt::arg("markup_begin", SuccessMarkupBegin),
                fmt::arg("markup_end", SuccessMarkupEnd));
        }
        else
        {
            gstr << fmt::format(
                // {markup_begin} and {markup_end} should surround the error text
                fmt::runtime(_("Got a scrape error '{markup_begin}{error}{markup_end}' {time_span_ago}")),
                fmt::arg("error", Glib::Markup::escape_text(std::data(tracker.lastScrapeResult))),
                fmt::arg("time_span_ago", time_span_ago),
                fmt::arg("markup_begin", ErrMarkupBegin),
                fmt::arg("markup_end", ErrMarkupEnd));
        }
    }

    switch (tracker.scrapeState)
    {
    case TR_TRACKER_INACTIVE:
        break;

    case TR_TRACKER_WAITING:
        gstr << '\n';
        gstr << dir_mark;
        gstr << fmt::format(
            fmt::runtime(_("Asking for peer counts in {time_span_from_now}")),
            fmt::arg("time_span_from_now", tr_format_time_relative(now, tracker.nextScrapeTime)));
        break;

    case TR_TRACKER_QUEUED:
        gstr << '\n';
        gstr << dir_mark;
        gstr << _("Queued to ask for peer counts");
        break;

    case TR_TRACKER_ACTIVE:
        gstr << '\n';
        gstr << dir_mark;
        gstr << fmt::format(
            fmt::runtime(_("Asked for peer counts {markup_begin}{time_span_ago}{markup_end}")),
            fmt::arg("markup_begin", "<small>"),
            fmt::arg("time_span_ago", tr_format_time_relative(now, tracker.lastScrapeStartTime)),
            fmt::arg("markup_end", "</small>"));
        break;

    default:
        g_assert_not_reached();
    }
}

void buildTrackerSummary(
    std::ostream& gstr,
    std::string const& key,
    tr_tracker_view const& tracker,
    bool showScrape,
    Gtk::TextDirection direction)
{
    // hostname
    gstr << text_dir_mark.at(static_cast<int>(direction));
    gstr << (tracker.isBackup ? "<i>" : "<b>");
    gstr << Glib::Markup::escape_text(
        !key.empty() ? fmt::format("{:s} - {:s}", tracker.host_and_port, key) : tracker.host_and_port);
    gstr << (tracker.isBackup ? "</i>" : "</b>");

    if (!tracker.isBackup)
    {
        time_t const now = time(nullptr);

        appendAnnounceInfo(tracker, now, direction, gstr);

        if (showScrape)
        {
            appendScrapeInfo(tracker, now, direction, gstr);
        }
    }
}

} // namespace

tr_torrent_id_t DetailsDialog::Impl::tracker_list_get_current_torrent_id() const
{
    if (ids_.size() == 1)
    {
        return ids_.front();
    }

    if (auto const row = gtr_ptr_dynamic_cast<DetailsTrackerRow>(tracker_selection_->get_selected_item()))
    {
        return row->get_torrent_id();
    }

    return -1;
}

tr_torrent* DetailsDialog::Impl::tracker_list_get_current_torrent() const
{
    return core_->find_torrent(tracker_list_get_current_torrent_id());
}

namespace
{

void favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const* pixbuf, Glib::RefPtr<DetailsTrackerRow> const& row)
{
    if (pixbuf != nullptr && *pixbuf != nullptr && row != nullptr)
    {
        row->set_favicon(*pixbuf);
    }
}

} // namespace

void DetailsDialog::Impl::refreshTracker(std::vector<tr_torrent*> const& torrents)
{
    std::ostringstream gstr;
    auto& hash = tracker_hash_;
    auto const& store = tracker_store_;
    bool const showScrape = scrape_check_->get_active();

    auto trackers = std::multimap<tr_torrent const*, tr_tracker_view>{};
    for (auto const* tor : torrents)
    {
        for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
        {
            trackers.emplace(tor, tr_torrentTracker(tor, i));
        }
    }

    for (guint i = 0, n = store->get_n_items(); i < n; ++i)
    {
        store->get_item(i)->set_was_updated(false);
    }

    for (auto const& [tor, tracker] : trackers)
    {
        auto const torrent_id = tr_torrentId(tor);

        gstr.str({});
        gstr << torrent_id << '\t' << tracker.tier << '\t' << tracker.announce;
        auto const key = gstr.str();

        if (!hash.contains(key))
        {
            auto const row = DetailsTrackerRow::create(torrent_id, tracker.id, key, tracker.isBackup);
            store->append(row);
            hash.try_emplace(key, row);
            core_->favicon_cache().load(
                tracker.announce,
                [row](auto const* pixbuf_refptr) { favicon_ready_cb(pixbuf_refptr, row); });
        }
    }

    auto const summary_name = std::string(std::size(torrents) == 1 ? tr_torrentName(torrents.front()) : "");
    for (auto const& [tor, tracker] : trackers)
    {
        auto const torrent_id = tr_torrentId(tor);

        gstr.str({});
        gstr << torrent_id << '\t' << tracker.tier << '\t' << tracker.announce;
        auto const key = gstr.str();
        auto const row = hash.at(key);

        gstr.str({});
        buildTrackerSummary(gstr, summary_name, tracker, showScrape, dialog_.get_direction());
        row->set_text(gstr.str());
        row->set_is_backup(tracker.isBackup);
        row->set_tracker_id(tracker.id);
        row->set_was_updated(true);
    }

    remove_unupdated_rows(store, hash);

    edit_trackers_button_->set_sensitive(tracker_list_get_current_torrent_id() > 0);
}
void DetailsDialog::Impl::refreshFiles(std::vector<tr_torrent*> const& torrents)
{
    if (torrents.size() == 1)
    {
        auto* const tor = torrents.front();
        file_list_->set_torrent(tr_torrentId(tor));

        if (tr_torrentFileCount(tor) == 0)
        {
            auto const stats = tr_torrentStat(tor);
            if (stats.metadata_percent_complete < 1.0)
            {
                file_list_->clear();
                gtr_widget_set_visible(*file_list_, false);
                file_label_->set_text(_("Metadata is still downloading…"));
                gtr_widget_set_visible(*file_label_, true);
                return;
            }
        }

        gtr_widget_set_visible(*file_list_, true);
        gtr_widget_set_visible(*file_label_, false);
    }
    else
    {
        file_list_->clear();
        gtr_widget_set_visible(*file_list_, false);
        gtr_widget_set_visible(*file_label_, true);
    }
}

void DetailsDialog::Impl::onScrapeToggled()
{
    tr_quark const key = TR_KEY_show_tracker_scrapes;
    bool const value = scrape_check_->get_active();
    core_->set_pref(key, value);
    refresh();
}

void DetailsDialog::Impl::onBackupToggled()
{
    tr_quark const key = TR_KEY_show_backup_trackers;
    bool const value = all_check_->get_active();
    core_->set_pref(key, value);
    tracker_backup_filter_->set_show_backup(value);
}

namespace
{

class EditTrackersDialog : public Gtk::Dialog
{
public:
    // Local session constructor
    EditTrackersDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent const* torrent);

    // Remote session constructor (torrent_list is newline-separated tracker URLs)
    EditTrackersDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent_id_t torrent_id,
        Glib::ustring const& torrent_name,
        std::string tracker_list);

    EditTrackersDialog(EditTrackersDialog&&) = delete;
    EditTrackersDialog(EditTrackersDialog const&) = delete;
    EditTrackersDialog& operator=(EditTrackersDialog&&) = delete;
    EditTrackersDialog& operator=(EditTrackersDialog const&) = delete;
    ~EditTrackersDialog() override = default;

    static std::unique_ptr<EditTrackersDialog> create(
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent const* tor);

    static std::unique_ptr<EditTrackersDialog> create(
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent_id_t torrent_id,
        Glib::ustring const& torrent_name,
        std::string tracker_list);

private:
    void on_response(int response) override;

    void init_common(Glib::ustring const& title, std::string const& initial_text);

private:
    DetailsDialog& parent_;
    Glib::RefPtr<Session> const core_;
    tr_torrent_id_t const torrent_id_;
    bool const is_remote_;
    Gtk::TextView* const urls_view_;
};

void EditTrackersDialog::init_common(Glib::ustring const& title, std::string const& initial_text)
{
    set_title(title);
    set_transient_for(parent_);
    urls_view_->get_buffer()->set_text(initial_text);
}

EditTrackersDialog::EditTrackersDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent const* torrent)
    : Gtk::Dialog(cast_item)
    , parent_(parent)
    , core_(core)
    , torrent_id_(tr_torrentId(torrent))
    , is_remote_(false)
    , urls_view_(gtr_get_widget<Gtk::TextView>(builder, "urls_view"))
{
    init_common(
        fmt::format(fmt::runtime(_("{torrent_name} - Edit Trackers")), fmt::arg("torrent_name", tr_torrentName(torrent))),
        tr_torrentGetTrackerList(torrent));
}

EditTrackersDialog::EditTrackersDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent_id_t const torrent_id,
    Glib::ustring const& torrent_name,
    std::string tracker_list)
    : Gtk::Dialog(cast_item)
    , parent_(parent)
    , core_(core)
    , torrent_id_(torrent_id)
    , is_remote_(true)
    , urls_view_(gtr_get_widget<Gtk::TextView>(builder, "urls_view"))
{
    init_common(
        fmt::format(fmt::runtime(_("{torrent_name} - Edit Trackers")), fmt::arg("torrent_name", torrent_name)),
        std::move(tracker_list));
}

std::unique_ptr<EditTrackersDialog> EditTrackersDialog::create(
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent const* torrent)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("EditTrackersDialog.ui"));
    return std::unique_ptr<EditTrackersDialog>(
        gtr_get_widget_derived<EditTrackersDialog>(builder, "EditTrackersDialog", parent, core, torrent));
}

std::unique_ptr<EditTrackersDialog> EditTrackersDialog::create(
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent_id_t const torrent_id,
    Glib::ustring const& torrent_name,
    std::string tracker_list)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("EditTrackersDialog.ui"));
    return std::unique_ptr<EditTrackersDialog>(gtr_get_widget_derived<EditTrackersDialog>(
        builder,
        "EditTrackersDialog",
        parent,
        core,
        torrent_id,
        torrent_name,
        std::move(tracker_list)));
}

void EditTrackersDialog::on_response(int response)
{
    bool do_destroy = true;

    if (response == TR_GTK_RESPONSE_TYPE(ACCEPT))
    {
        auto const text = urls_view_->get_buffer()->get_text(false);

        if (is_remote_)
        {
            auto params = tr_variant::Map{ 2U };
            params[TR_KEY_ids] = Session::to_variant(std::vector<tr_torrent_id_t>{ torrent_id_ });
            params[TR_KEY_tracker_list] = text.raw();
            core_->exec(TR_KEY_torrent_set, std::move(params));
            parent_.refresh();
        }
        else if (auto* const tor = core_->find_torrent(torrent_id_); tor != nullptr)
        {
            if (tr_torrentSetTrackerList(tor, text.c_str()))
            {
                parent_.refresh();
            }
            else
            {
                auto dialog = Gtk::AlertDialog::create(_("List contains invalid URLs"));
                dialog->set_detail(_("Please correct the errors and try again."));
                dialog->show(*this); // Gtk::AlertDialog

                do_destroy = false;
            }
        }
    }

    if (do_destroy)
    {
        close();
    }
}

} // namespace

void DetailsDialog::Impl::on_edit_trackers()
{
    auto const torrent_id = tracker_list_get_current_torrent_id();
    if (torrent_id <= 0)
    {
        return;
    }

    if (core_->is_remote())
    {
        auto const it = rpc_tracker_lists_.find(torrent_id);
        auto tracker_list = it != rpc_tracker_lists_.end() ? it->second : std::string{};

        auto const tor_ref = core_->find_torrent_ref(torrent_id);
        Glib::ustring const name = tor_ref ? tor_ref->get_name() : Glib::ustring{};

        auto d = std::shared_ptr<EditTrackersDialog>(
            EditTrackersDialog::create(dialog_, core_, torrent_id, name, std::move(tracker_list)));
        gtr_window_on_close(*d, [d]() mutable { d.reset(); });
        d->present();
        return;
    }

    if (auto const* const tor = tracker_list_get_current_torrent(); tor != nullptr)
    {
        auto d = std::shared_ptr<EditTrackersDialog>(EditTrackersDialog::create(dialog_, core_, tor));
        gtr_window_on_close(*d, [d]() mutable { d.reset(); });
        d->present();
    }
}

void DetailsDialog::Impl::on_tracker_list_selection_changed()
{
    bool const has_selection = tracker_selection_->get_selected() != GTK_INVALID_LIST_POSITION;
    auto const torrent_id = tracker_list_get_current_torrent_id();

    remove_tracker_button_->set_sensitive(has_selection);
    add_tracker_button_->set_sensitive(torrent_id > 0);
    edit_trackers_button_->set_sensitive(torrent_id > 0);
}

namespace
{

class AddTrackerDialog : public Gtk::Dialog
{
public:
    AddTrackerDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent_id_t torrent_id,
        Glib::ustring const& torrent_name);
    AddTrackerDialog(AddTrackerDialog&&) = delete;
    AddTrackerDialog(AddTrackerDialog const&) = delete;
    AddTrackerDialog& operator=(AddTrackerDialog&&) = delete;
    AddTrackerDialog& operator=(AddTrackerDialog const&) = delete;
    ~AddTrackerDialog() override = default;

    static std::unique_ptr<AddTrackerDialog> create(
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent const* tor);

    static std::unique_ptr<AddTrackerDialog> create(
        DetailsDialog& parent,
        Glib::RefPtr<Session> const& core,
        tr_torrent_id_t torrent_id,
        Glib::ustring const& torrent_name);

private:
    void on_response(int response) override;

private:
    DetailsDialog& parent_;
    Glib::RefPtr<Session> const core_;
    tr_torrent_id_t const torrent_id_;
    Gtk::Entry* const url_entry_;
};

AddTrackerDialog::AddTrackerDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent_id_t const torrent_id,
    Glib::ustring const& torrent_name)
    : Gtk::Dialog(cast_item)
    , parent_(parent)
    , core_(core)
    , torrent_id_(torrent_id)
    , url_entry_(gtr_get_widget<Gtk::Entry>(builder, "url_entry"))
{
    set_title(fmt::format(fmt::runtime(_("{torrent_name} - Add Tracker")), fmt::arg("torrent_name", torrent_name)));
    set_transient_for(parent);

    auto* const accept = get_widget_for_response(TR_GTK_RESPONSE_TYPE(ACCEPT));
#if GTKMM_CHECK_VERSION(4, 0, 0)
    set_default_widget(*accept);
#else
    set_default(*accept);
#endif

    gtr_paste_clipboard_url_into_entry(*url_entry_);
}

std::unique_ptr<AddTrackerDialog> AddTrackerDialog::create(
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent const* torrent)
{
    return create(parent, core, tr_torrentId(torrent), tr_torrentName(torrent));
}

std::unique_ptr<AddTrackerDialog> AddTrackerDialog::create(
    DetailsDialog& parent,
    Glib::RefPtr<Session> const& core,
    tr_torrent_id_t const torrent_id,
    Glib::ustring const& torrent_name)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("AddTrackerDialog.ui"));
    return std::unique_ptr<AddTrackerDialog>(gtr_get_widget_derived<AddTrackerDialog>(
        builder,
        "AddTrackerDialog",
        parent,
        core,
        torrent_id,
        torrent_name));
}

void AddTrackerDialog::on_response(int response)
{
    bool destroy = true;

    if (response == TR_GTK_RESPONSE_TYPE(ACCEPT))
    {
        auto const url = gtr_str_strip(url_entry_->get_text());

        if (!url.empty())
        {
            if (tr_urlIsValidTracker(url.c_str()))
            {
                // TODO(ckerr) migrate to `TR_KEY_tracker_list`
                auto params = tr_variant::Map{ 2U };
                params.try_emplace(TR_KEY_ids, Session::to_variant({ torrent_id_ }));
                params.try_emplace(TR_KEY_tracker_add, Session::to_variant({ url.raw() }));
                core_->exec(TR_KEY_torrent_set, std::move(params));
                parent_.refresh();
            }
            else
            {
                gtr_unrecognized_url_dialog(*this, url);
                destroy = false;
            }
        }
    }

    if (destroy)
    {
        close();
    }
}

} // namespace

void DetailsDialog::Impl::on_tracker_list_add_button_clicked()
{
    auto const torrent_id = tracker_list_get_current_torrent_id();
    if (torrent_id <= 0)
    {
        return;
    }

    if (core_->is_remote())
    {
        if (auto const tor = core_->find_torrent_ref(torrent_id); tor != nullptr)
        {
            auto d = std::shared_ptr<AddTrackerDialog>(AddTrackerDialog::create(dialog_, core_, tor->get_id(), tor->get_name()));
            gtr_window_on_close(*d, [d]() mutable { d.reset(); });
            d->present();
        }
        return;
    }

    if (auto const* const tor = tracker_list_get_current_torrent(); tor != nullptr)
    {
        auto d = std::shared_ptr<AddTrackerDialog>(AddTrackerDialog::create(dialog_, core_, tor));
        gtr_window_on_close(*d, [d]() mutable { d.reset(); });
        d->present();
    }
}

void DetailsDialog::Impl::on_tracker_list_remove_button_clicked()
{
    if (auto const row = gtr_ptr_dynamic_cast<DetailsTrackerRow>(tracker_selection_->get_selected_item()))
    {
        auto const torrent_id = row->get_torrent_id();
        auto const tracker_id = row->get_tracker_id();

        // TODO(ckerr): migrate to `TR_KEY_tracker_list`
        auto params = tr_variant::Map{ 2U };
        params.try_emplace(TR_KEY_ids, Session::to_variant({ torrent_id }));
        params.try_emplace(TR_KEY_tracker_remove, Session::to_variant({ tracker_id }));
        core_->exec(TR_KEY_torrent_set, std::move(params));
        refresh();
    }
}

void DetailsDialog::Impl::tracker_page_init(Glib::RefPtr<Gtk::Builder> const& /*builder*/)
{
    int const pad = (GUI_PAD + GUI_PAD_BIG) / 2;

    tracker_store_ = Gio::ListStore<DetailsTrackerRow>::create();
    tracker_backup_filter_ = DetailsTrackerBackupFilter::create();
    trackers_filtered_ = FilterListModel<DetailsTrackerRow>::create(tracker_store_, tracker_backup_filter_);
    tracker_selection_ = Gtk::SingleSelection::create(trackers_filtered_);
    tracker_view_->set_model(tracker_selection_);

    static auto const FaviconKey = Glib::Quark("tr-details-tracker-favicon");
    static auto const TextKey = Glib::Quark("tr-details-tracker-text");

    tracker_item_factory_ = Gtk::SignalListItemFactory::create();
    tracker_item_factory_->signal_setup().connect(
        [pad](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto* const row_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, GUI_PAD_SMALL);
            row_box->set_hexpand(true);

            auto* const favicon = Gtk::make_managed<Gtk::Image>();
            favicon->set_valign(Gtk::Align::START);
            favicon->set_pixel_size(20 + (GUI_PAD_SMALL * 2));

            auto* const text = Gtk::make_managed<Gtk::Label>();
            text->set_hexpand(true);
            text->set_xalign(0);
            text->set_yalign(0);
            text->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);
            text->set_margin_top(pad);
            text->set_margin_bottom(pad);

            row_box->append(*favicon);
            row_box->append(*text);

            list_item->set_data(FaviconKey, favicon);
            list_item->set_data(TextKey, text);
            list_item->set_child(*row_box);
        });

    tracker_item_factory_->signal_bind().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto const tracker_row = gtr_ptr_dynamic_cast<DetailsTrackerRow>(list_item->get_item());
            auto* const favicon = static_cast<Gtk::Image*>(list_item->get_data(FaviconKey));
            auto* const text = static_cast<Gtk::Label*>(list_item->get_data(TextKey));
            if (tracker_row == nullptr || favicon == nullptr || text == nullptr)
            {
                return;
            }

            if (auto const pixbuf = tracker_row->get_favicon(); pixbuf != nullptr)
            {
                favicon->set(pixbuf);
            }
            else
            {
                favicon->set(Glib::RefPtr<Gdk::Pixbuf>{});
            }

            text->set_markup(tracker_row->get_text());
        });

    tracker_view_->set_factory(tracker_item_factory_);

    setup_item_view_button_event_handling(
        *tracker_view_,
        [this](guint /*button*/, TrGdkModifierType /*state*/, double view_x, double view_y, bool context_menu_requested)
        { return on_item_view_button_pressed(*tracker_view_, view_x, view_y, context_menu_requested); },
        [this](double view_x, double view_y) { return on_item_view_button_released(*tracker_view_, view_x, view_y); });

    tracker_selection_->signal_selection_changed().connect(
        [this](guint /*position*/, guint /*n_items*/) { on_tracker_list_selection_changed(); });

    add_tracker_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_add_button_clicked));
    edit_trackers_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_edit_trackers));
    remove_tracker_button_->signal_clicked().connect(sigc::mem_fun(*this, &Impl::on_tracker_list_remove_button_clicked));

    scrape_check_->set_active(gtr_pref_flag_get(TR_KEY_show_tracker_scrapes));
    scrape_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onScrapeToggled));

    all_check_->set_active(gtr_pref_flag_get(TR_KEY_show_backup_trackers));
    tracker_backup_filter_->set_show_backup(all_check_->get_active());
    all_check_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::onBackupToggled));
}

/****
*****  REMOTE (RPC) REFRESH
****/

namespace
{

[[nodiscard]] Glib::ustring activity_string_from_status(int64_t const status)
{
    switch (static_cast<int>(status))
    {
    case TR_STATUS_STOPPED:
        return _("Stopped");
    case TR_STATUS_CHECK_WAIT:
        return _("Queued for verification");
    case TR_STATUS_CHECK:
        return _("Verifying");
    case TR_STATUS_DOWNLOAD_WAIT:
        return _("Queued for download");
    case TR_STATUS_DOWNLOAD:
        return _("Downloading");
    case TR_STATUS_SEED_WAIT:
        return _("Queued for seeding");
    case TR_STATUS_SEED:
        return _("Seeding");
    default:
        return _("Unknown");
    }
}

[[nodiscard]] bool int64_maps_uniform(std::vector<tr_variant::Map const*> const& maps, tr_quark const key)
{
    if (maps.empty())
    {
        return true;
    }

    auto const baseline = maps.front()->value_if<int64_t>(key).value_or(0);
    for (auto it = std::next(maps.begin()); it != maps.end(); ++it)
    {
        if ((*it)->value_if<int64_t>(key).value_or(0) != baseline)
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool bool_maps_uniform(std::vector<tr_variant::Map const*> const& maps, tr_quark const key)
{
    if (maps.empty())
    {
        return true;
    }

    auto const baseline = maps.front()->value_if<bool>(key).value_or(false);
    for (auto it = std::next(maps.begin()); it != maps.end(); ++it)
    {
        if ((*it)->value_if<bool>(key).value_or(false) != baseline)
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool double_maps_uniform(std::vector<tr_variant::Map const*> const& maps, tr_quark const key)
{
    if (maps.empty())
    {
        return true;
    }

    auto const baseline = maps.front()->value_if<double>(key).value_or(0.0);
    for (auto it = std::next(maps.begin()); it != maps.end(); ++it)
    {
        if ((*it)->value_if<double>(key).value_or(0.0) != baseline)
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] Glib::ustring sv_to_ustring(std::string_view const sv)
{
    return Glib::ustring{ std::string{ sv } };
}

} // namespace

[[nodiscard]] bool torrent_get_response_is_table_format(tr_variant::Vector const& torrents)
{
    if (torrents.empty())
    {
        return false;
    }

    auto* const first_row = torrents.front().get_if<tr_variant::Vector>();
    if (first_row == nullptr || first_row->empty())
    {
        return false;
    }

    return first_row->front().value_if<std::string_view>().has_value();
}

[[nodiscard]] std::vector<tr_variant::Map const*> torrent_maps_from_response(
    tr_variant const& result,
    std::vector<tr_variant::Map>& table_maps_storage)
{
    auto maps = std::vector<tr_variant::Map const*>{};
    auto* const result_map = result.get_if<tr_variant::Map>();
    if (result_map == nullptr)
    {
        return maps;
    }

    auto* const torrents = result_map->find_if<tr_variant::Vector>(TR_KEY_torrents);
    if (torrents == nullptr || torrents->empty())
    {
        return maps;
    }

    if (torrent_get_response_is_table_format(*torrents))
    {
        auto* const field_names = torrents->front().get_if<tr_variant::Vector>();
        if (field_names == nullptr)
        {
            return maps;
        }

        auto keys = std::vector<tr_quark>{};
        keys.reserve(field_names->size());
        for (auto const& field : *field_names)
        {
            if (auto const name = field.value_if<std::string_view>())
            {
                if (auto const key = tr_quark_lookup(*name))
                {
                    keys.emplace_back(*key);
                }
            }
        }

        table_maps_storage.clear();
        table_maps_storage.reserve(torrents->size() - 1U);
        for (size_t i = 1U; i < torrents->size(); ++i)
        {
            auto* const row = (*torrents)[i].get_if<tr_variant::Vector>();
            if (row == nullptr)
            {
                continue;
            }

            auto map = tr_variant::Map{};
            for (size_t j = 0U; j < keys.size() && j < row->size(); ++j)
            {
                map.try_emplace(keys[j], (*row)[j].clone());
            }

            table_maps_storage.emplace_back(std::move(map));
        }

        for (auto const& map : table_maps_storage)
        {
            maps.push_back(&map);
        }

        return maps;
    }

    maps.reserve(torrents->size());
    for (auto const& entry : *torrents)
    {
        if (auto* const map = entry.get_if<tr_variant::Map>())
        {
            maps.push_back(map);
        }
    }

    return maps;
}

void DetailsDialog::Impl::refresh_from_rpc_maps(std::vector<tr_variant::Map const*> const& maps)
{
    if (maps.empty())
    {
        return;
    }

    refreshInfoRpc(maps);
    refreshOptionsRpc(maps);
    refreshPeersRpc(maps);
    refreshTrackerRpc(maps);

    // Cache tracker_list strings for the edit-trackers dialog
    rpc_tracker_lists_.clear();
    for (auto* const map : maps)
    {
        auto const id = static_cast<tr_torrent_id_t>(map->value_if<int64_t>(TR_KEY_id).value_or(-1));
        if (id > 0)
        {
            rpc_tracker_lists_[id] = map->value_if<std::string_view>(TR_KEY_tracker_list).value_or(""sv);
        }
    }

    auto const can_edit_trackers = maps.size() == 1;
    add_tracker_button_->set_sensitive(can_edit_trackers);
    edit_trackers_button_->set_sensitive(can_edit_trackers);
    on_tracker_list_selection_changed();
}

void DetailsDialog::Impl::refreshInfoRpc(std::vector<tr_variant::Map const*> const& maps)
{
    Glib::ustring str;
    Glib::ustring const mixed = _("Mixed");
    Glib::ustring const no_torrent = _("No Torrents Selected");

    if (maps.empty())
    {
        str = no_torrent;
        privacy_lb_->set_text(str);
        added_lb_->set_text(str);
        origin_lb_->set_text(str);
        destination_lb_->set_text(str);
        size_lb_->set_text(str);
        state_lb_->set_text(str);
        have_lb_->set_text(str);
        dl_lb_->set_text(str);
        ul_lb_->set_text(str);
        hash_lb_->set_text(str);
        error_lb_->set_text(str);
        date_started_lb_->set_text(str);
        eta_lb_->set_text(str);
        last_activity_lb_->set_text(str);
        gtr_text_buffer_set_text(comment_buffer_, {});
        return;
    }

    if (bool_maps_uniform(maps, TR_KEY_is_private))
    {
        auto const is_private = maps.front()->value_if<bool>(TR_KEY_is_private).value_or(false);
        privacy_lb_->set_text(is_private ? _("Private to this tracker -- DHT and PEX disabled") : _("Public torrent"));
    }
    else
    {
        privacy_lb_->set_text(mixed);
    }

    if (int64_maps_uniform(maps, TR_KEY_added_date))
    {
        added_lb_->set_text(get_date_time_string(maps.front()->value_if<int64_t>(TR_KEY_added_date).value_or(0)));
    }
    else
    {
        added_lb_->set_text(mixed);
    }

    if (maps.size() == 1)
    {
        auto* const map = maps.front();
        auto const creator = map->value_if<std::string_view>(TR_KEY_creator).value_or(""sv);
        auto const created = map->value_if<int64_t>(TR_KEY_date_created).value_or(0);
        if (creator.empty() && created == 0)
        {
            origin_lb_->set_text(_("N/A"));
        }
        else if (creator.empty())
        {
            origin_lb_->set_text(get_date_string(created));
        }
        else if (created == 0)
        {
            origin_lb_->set_text(sv_to_ustring(creator));
        }
        else
        {
            origin_lb_->set_text(fmt::format(
                fmt::runtime(_("{creator} on {date}")),
                fmt::arg("creator", creator),
                fmt::arg("date", get_date_string(created))));
        }

        destination_lb_->set_text(sv_to_ustring(map->value_if<std::string_view>(TR_KEY_download_dir).value_or(""sv)));
        hash_lb_->set_text(sv_to_ustring(map->value_if<std::string_view>(TR_KEY_hash_string).value_or(""sv)));
        gtr_text_buffer_set_text(comment_buffer_, sv_to_ustring(map->value_if<std::string_view>(TR_KEY_comment).value_or(""sv)));
    }
    else
    {
        origin_lb_->set_text(mixed);
        destination_lb_->set_text(mixed);
        hash_lb_->set_text(mixed);
        gtr_text_buffer_set_text(comment_buffer_, mixed);
    }

    if (int64_maps_uniform(maps, TR_KEY_total_size))
    {
        size_lb_->set_text(tr_strlsize(maps.front()->value_if<int64_t>(TR_KEY_total_size).value_or(0)));
    }
    else
    {
        size_lb_->set_text(mixed);
    }

    if (int64_maps_uniform(maps, TR_KEY_status))
    {
        state_lb_->set_text(activity_string_from_status(maps.front()->value_if<int64_t>(TR_KEY_status).value_or(0)));
    }
    else
    {
        state_lb_->set_text(mixed);
    }

    if (maps.size() == 1)
    {
        auto* const map = maps.front();
        auto const have = map->value_if<int64_t>(TR_KEY_have_valid).value_or(0);
        auto const unverified = map->value_if<int64_t>(TR_KEY_have_unchecked).value_or(0);
        auto const total = map->value_if<int64_t>(TR_KEY_size_when_done).value_or(0);
        auto const percent = static_cast<int>(100.0 * map->value_if<double>(TR_KEY_percent_done).value_or(0.0));
        if (unverified == 0)
        {
            have_lb_->set_text(fmt::format(
                fmt::runtime(_("{current_size} ({percent_done}% of {total_size})")),
                fmt::arg("current_size", tr_strlsize(have)),
                fmt::arg("percent_done", percent),
                fmt::arg("total_size", tr_strlsize(total))));
        }
        else
        {
            have_lb_->set_text(fmt::format(
                fmt::runtime(_("{current_size} ({percent_done}% of {total_size}; {unverified_size} unverified)")),
                fmt::arg("current_size", tr_strlsize(have)),
                fmt::arg("percent_done", percent),
                fmt::arg("total_size", tr_strlsize(total)),
                fmt::arg("unverified_size", tr_strlsize(unverified))));
        }
    }
    else
    {
        have_lb_->set_text(mixed);
    }

    auto downloaded = uint64_t{ 0 };
    auto corrupt = uint64_t{ 0 };
    for (auto* const map : maps)
    {
        downloaded += map->value_if<int64_t>(TR_KEY_downloaded_ever).value_or(0);
        corrupt += map->value_if<int64_t>(TR_KEY_corrupt_ever).value_or(0);
    }

    if (corrupt != 0)
    {
        dl_lb_->set_text(fmt::format(
            fmt::runtime(_("{downloaded_size} (+{discarded_size} discarded after failed checksum)")),
            fmt::arg("downloaded_size", tr_strlsize(downloaded)),
            fmt::arg("discarded_size", tr_strlsize(corrupt))));
    }
    else
    {
        dl_lb_->set_text(tr_strlsize(downloaded));
    }

    auto uploaded = uint64_t{ 0 };
    auto denominator = uint64_t{ 0 };
    for (auto* const map : maps)
    {
        uploaded += map->value_if<int64_t>(TR_KEY_uploaded_ever).value_or(0);
        denominator += map->value_if<int64_t>(TR_KEY_size_when_done).value_or(0);
    }

    ul_lb_->set_text(fmt::format(
        fmt::runtime(_("{uploaded_size} (Ratio: {ratio})")),
        fmt::arg("uploaded_size", tr_strlsize(uploaded)),
        fmt::arg("ratio", tr_strlratio(tr_getRatio(uploaded, denominator)))));

    if (maps.size() == 1)
    {
        error_lb_->set_text(sv_to_ustring(maps.front()->value_if<std::string_view>(TR_KEY_error_string).value_or(""sv)));
    }
    else
    {
        error_lb_->set_text(mixed);
    }

    if (int64_maps_uniform(maps, TR_KEY_start_date))
    {
        date_started_lb_->set_text(get_date_time_string(maps.front()->value_if<int64_t>(TR_KEY_start_date).value_or(0)));
    }
    else
    {
        date_started_lb_->set_text(mixed);
    }

    if (int64_maps_uniform(maps, TR_KEY_eta))
    {
        eta_lb_->set_text(tr_format_time(maps.front()->value_if<int64_t>(TR_KEY_eta).value_or(0)));
    }
    else
    {
        eta_lb_->set_text(mixed);
    }

    if (int64_maps_uniform(maps, TR_KEY_activity_date))
    {
        last_activity_lb_->set_text(get_date_time_string(maps.front()->value_if<int64_t>(TR_KEY_activity_date).value_or(0)));
    }
    else
    {
        last_activity_lb_->set_text(mixed);
    }
}

void DetailsDialog::Impl::refreshOptionsRpc(std::vector<tr_variant::Map const*> const& maps)
{
    if (maps.empty())
    {
        return;
    }

    if (bool_maps_uniform(maps, TR_KEY_honors_session_limits))
    {
        set_togglebutton_if_different(
            honor_limits_check_,
            honor_limits_check_tag_,
            maps.front()->value_if<bool>(TR_KEY_honors_session_limits).value_or(false));
    }

    if (bool_maps_uniform(maps, TR_KEY_download_limited))
    {
        set_togglebutton_if_different(
            down_limited_check_,
            down_limited_check_tag_,
            maps.front()->value_if<bool>(TR_KEY_download_limited).value_or(false));
    }

    if (int64_maps_uniform(maps, TR_KEY_download_limit))
    {
        set_int_spin_if_different(
            down_limit_spin_,
            down_limit_spin_tag_,
            static_cast<int>(maps.front()->value_if<int64_t>(TR_KEY_download_limit).value_or(0)));
    }

    if (bool_maps_uniform(maps, TR_KEY_upload_limited))
    {
        set_togglebutton_if_different(
            up_limited_check_,
            up_limited_check_tag_,
            maps.front()->value_if<bool>(TR_KEY_upload_limited).value_or(false));
    }

    if (int64_maps_uniform(maps, TR_KEY_upload_limit))
    {
        set_int_spin_if_different(
            up_limit_sping_,
            up_limit_spin_tag_,
            static_cast<int>(maps.front()->value_if<int64_t>(TR_KEY_upload_limit).value_or(0)));
    }

    if (int64_maps_uniform(maps, TR_KEY_bandwidth_priority))
    {
        bandwidth_combo_tag_.block();
        gtr_combo_box_set_active_enum(
            *bandwidth_combo_,
            static_cast<int>(maps.front()->value_if<int64_t>(TR_KEY_bandwidth_priority).value_or(0)));
        bandwidth_combo_tag_.unblock();
    }

    if (int64_maps_uniform(maps, TR_KEY_seed_ratio_mode))
    {
        auto const mode = static_cast<int>(maps.front()->value_if<int64_t>(TR_KEY_seed_ratio_mode).value_or(0));
        ratio_combo_tag_.block();
        gtr_combo_box_set_active_enum(*ratio_combo_, mode);
        gtr_widget_set_visible(*ratio_spin_, mode == TR_RATIOLIMIT_SINGLE);
        ratio_combo_tag_.unblock();
    }

    if (double_maps_uniform(maps, TR_KEY_seed_ratio_limit))
    {
        set_double_spin_if_different(
            ratio_spin_,
            ratio_spin_tag_,
            maps.front()->value_if<double>(TR_KEY_seed_ratio_limit).value_or(0.0));
    }

    if (int64_maps_uniform(maps, TR_KEY_seed_idle_mode))
    {
        auto const mode = static_cast<int>(maps.front()->value_if<int64_t>(TR_KEY_seed_idle_mode).value_or(0));
        idle_combo_tag_.block();
        gtr_combo_box_set_active_enum(*idle_combo_, mode);
        gtr_widget_set_visible(*idle_spin_, mode == TR_IDLELIMIT_SINGLE);
        idle_combo_tag_.unblock();
    }

    if (int64_maps_uniform(maps, TR_KEY_seed_idle_limit))
    {
        set_int_spin_if_different(
            idle_spin_,
            idle_spin_tag_,
            static_cast<int>(maps.front()->value_if<int64_t>(TR_KEY_seed_idle_limit).value_or(0)));
    }

    if (int64_maps_uniform(maps, TR_KEY_peer_limit))
    {
        set_int_spin_if_different(
            max_peers_spin_,
            max_peers_spin_tag_,
            static_cast<int>(maps.front()->value_if<int64_t>(TR_KEY_peer_limit).value_or(0)));
    }
}

void DetailsDialog::Impl::refreshPeersRpc(std::vector<tr_variant::Map const*> const& maps)
{
    peer_hash_.clear();
    peer_store_->remove_all();
    webseed_hash_.clear();
    webseed_store_->remove_all();

    for (auto* const map : maps)
    {
        auto const torrent_name = sv_to_ustring(map->value_if<std::string_view>(TR_KEY_name).value_or(""sv));
        auto* const peers = map->find_if<tr_variant::Vector>(TR_KEY_peers);
        if (peers == nullptr)
        {
            continue;
        }

        for (auto const& entry : *peers)
        {
            auto* const peer = entry.get_if<tr_variant::Map>();
            if (peer == nullptr)
            {
                continue;
            }

            auto const address = sv_to_ustring(peer->value_if<std::string_view>(TR_KEY_address).value_or(""sv));
            auto const key = fmt::format("{:s}\t{:s}", torrent_name.raw(), address.raw());
            auto const down_bps = peer->value_if<int64_t>(TR_KEY_rate_to_client).value_or(0);
            auto const up_bps = peer->value_if<int64_t>(TR_KEY_rate_to_peer).value_or(0);
            auto const down_speed = Speed{ static_cast<double>(down_bps), Speed::Units::Byps };
            auto const up_speed = Speed{ static_cast<double>(up_bps), Speed::Units::Byps };

            auto const row = DetailsPeerRow::create_rpc(
                key,
                torrent_name,
                address,
                sv_to_ustring(peer->value_if<std::string_view>(TR_KEY_client_name).value_or(""sv)),
                sv_to_ustring(peer->value_if<std::string_view>(TR_KEY_flag_str).value_or(""sv)),
                static_cast<int>(100.0 * peer->value_if<double>(TR_KEY_progress).value_or(0.0)),
                down_speed,
                up_speed);
            peer_store_->append(row);
            peer_hash_.try_emplace(key, row);
        }
    }
}

void DetailsDialog::Impl::refreshTrackerRpc(std::vector<tr_variant::Map const*> const& maps)
{
    tracker_hash_.clear();
    tracker_store_->remove_all();

    for (auto* const map : maps)
    {
        auto const torrent_id = static_cast<tr_torrent_id_t>(map->value_if<int64_t>(TR_KEY_id).value_or(-1));
        auto* const stats = map->find_if<tr_variant::Vector>(TR_KEY_tracker_stats);
        if (stats == nullptr)
        {
            continue;
        }

        for (auto const& entry : *stats)
        {
            auto* const tracker = entry.get_if<tr_variant::Map>();
            if (tracker == nullptr)
            {
                continue;
            }

            auto const host = tracker->value_if<std::string_view>(TR_KEY_host).value_or(""sv);
            auto const announce = tracker->value_if<std::string_view>(TR_KEY_announce).value_or(""sv);
            auto const key = fmt::format("{:d}\t{:s}", torrent_id, host);
            auto const is_backup = tracker->value_if<bool>(TR_KEY_is_backup).value_or(false);
            auto const tracker_id = static_cast<int>(tracker->value_if<int64_t>(TR_KEY_id).value_or(0));

            auto const seeders = tracker->value_if<int64_t>(TR_KEY_seeder_count).value_or(-1);
            auto const leechers = tracker->value_if<int64_t>(TR_KEY_leecher_count).value_or(-1);
            auto const result = tracker->value_if<std::string_view>(TR_KEY_last_announce_result).value_or(""sv);
            auto const text = fmt::format(
                "<b>{}</b>\n{}",
                Glib::Markup::escape_text(std::string{ host.empty() ? announce : host }),
                Glib::Markup::escape_text(fmt::format(
                    fmt::runtime(_("Seeders: {seeders}  Leechers: {leechers}\n{result}")),
                    fmt::arg("seeders", seeders >= 0 ? std::to_string(seeders) : "?"),
                    fmt::arg("leechers", leechers >= 0 ? std::to_string(leechers) : "?"),
                    fmt::arg("result", result))));

            auto const row = DetailsTrackerRow::create(torrent_id, tracker_id, key, is_backup);
            row->set_text(text);
            row->set_was_updated(true);
            tracker_store_->append(row);
            tracker_hash_.try_emplace(key, row);
        }
    }
}

void DetailsDialog::Impl::refreshFilesRpc(std::vector<tr_variant::Map const*> const& maps)
{
    if (maps.size() != 1)
    {
        file_list_->clear();
        gtr_widget_set_visible(*file_list_, false);
        file_label_->set_text(_("Select a single torrent to see its files"));
        gtr_widget_set_visible(*file_label_, true);
        return;
    }

    auto* const map = maps.front();
    auto const torrent_id = static_cast<tr_torrent_id_t>(map->value_if<int64_t>(TR_KEY_id).value_or(-1));
    auto file_count = static_cast<int>(map->value_if<int64_t>(TR_KEY_file_count).value_or(0));
    if (auto const torrent = core_->find_torrent_ref(torrent_id))
    {
        file_count = std::max(file_count, torrent->get_file_count());
    }

    auto* const files = map->find_if<tr_variant::Vector>(TR_KEY_files);
    if (files == nullptr || files->empty())
    {
        file_list_->clear();
        gtr_widget_set_visible(*file_list_, false);
        if (file_count > 0)
        {
            file_label_->set_text(_("Loading file list…"));
        }
        else
        {
            file_label_->set_text(
                fmt::format(fmt::runtime(ngettext("{count:L} file", "{count:L} files", file_count)), fmt::arg("count", file_count)));
        }
        gtr_widget_set_visible(*file_label_, true);
        return;
    }

    auto* const file_stats = map->find_if<tr_variant::Vector>(TR_KEY_file_stats);
    file_list_->load_from_rpc(torrent_id, *files, file_stats);
    gtr_widget_set_visible(*file_label_, false);
    gtr_widget_set_visible(*file_list_, true);
}

/****
*****  DIALOG
****/

void DetailsDialog::Impl::refresh()
{
    if (core_->is_remote())
    {
        if (ids_.empty())
        {
            dialog_.response(TR_GTK_RESPONSE_TYPE(CLOSE));
            return;
        }

        core_->fetch_torrent_properties(
            ids_,
            [this](tr_variant&& result)
            {
                auto const result_holder = std::make_shared<tr_variant>(std::move(result));
                Glib::signal_idle().connect_once(
                    [this, result_holder]() mutable
                    {
                        auto table_maps = std::vector<tr_variant::Map>{};
                        refresh_from_rpc_maps(torrent_maps_from_response(*result_holder, table_maps));
                    });
            });

        core_->fetch_torrent_file_list(
            ids_,
            [this](tr_variant&& result)
            {
                auto const result_holder = std::make_shared<tr_variant>(std::move(result));
                Glib::signal_idle().connect_once(
                    [this, result_holder]() mutable
                    {
                        auto table_maps = std::vector<tr_variant::Map>{};
                        refreshFilesRpc(torrent_maps_from_response(*result_holder, table_maps));
                    });
            });

        return;
    }

    auto const torrents = getTorrents();

    refreshInfo(torrents);
    refreshPeers(torrents);
    refreshTracker(torrents);
    refreshFiles(torrents);
    refreshOptions(torrents);

    if (torrents.empty())
    {
        dialog_.response(TR_GTK_RESPONSE_TYPE(CLOSE));
    }
}

void DetailsDialog::Impl::on_details_window_size_allocated()
{
    int w = 0;
    int h = 0;
#if GTKMM_CHECK_VERSION(4, 0, 0)
    dialog_.get_default_size(w, h);
#else
    dialog_.get_size(w, h);
#endif
    gtr_pref_int_set(TR_KEY_details_window_width, w);
    gtr_pref_int_set(TR_KEY_details_window_height, h);
}

DetailsDialog::Impl::~Impl()
{
    periodic_refresh_tag_.disconnect();
}

std::unique_ptr<DetailsDialog> DetailsDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("DetailsDialog.ui"));
    return std::unique_ptr<DetailsDialog>(gtr_get_widget_derived<DetailsDialog>(builder, "DetailsDialog", parent, core));
}

DetailsDialog::DetailsDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core))
{
    set_transient_for(parent);
}

DetailsDialog::~DetailsDialog() = default;

DetailsDialog::Impl::Impl(DetailsDialog& dialog, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , core_(core)
    , honor_limits_check_(gtr_get_widget<Gtk::CheckButton>(builder, "honor_limits_check"))
    , up_limited_check_(gtr_get_widget<Gtk::CheckButton>(builder, "upload_limit_check"))
    , up_limit_sping_(gtr_get_widget<Gtk::SpinButton>(builder, "upload_limit_spin"))
    , down_limited_check_(gtr_get_widget<Gtk::CheckButton>(builder, "download_limit_check"))
    , down_limit_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "download_limit_spin"))
    , bandwidth_combo_(gtr_get_widget<Gtk::DropDown>(builder, "priority_combo"))
    , ratio_combo_(gtr_get_widget<Gtk::DropDown>(builder, "ratio_limit_combo"))
    , ratio_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "ratio_limit_spin"))
    , idle_combo_(gtr_get_widget<Gtk::DropDown>(builder, "idle_limit_combo"))
    , idle_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "idle_limit_spin"))
    , max_peers_spin_(gtr_get_widget<Gtk::SpinButton>(builder, "max_peers_spin"))
    , added_lb_(gtr_get_widget<Gtk::Label>(builder, "added_value_label"))
    , size_lb_(gtr_get_widget<Gtk::Label>(builder, "torrent_size_value_label"))
    , state_lb_(gtr_get_widget<Gtk::Label>(builder, "state_value_label"))
    , have_lb_(gtr_get_widget<Gtk::Label>(builder, "have_value_label"))
    , dl_lb_(gtr_get_widget<Gtk::Label>(builder, "downloaded_value_label"))
    , ul_lb_(gtr_get_widget<Gtk::Label>(builder, "uploaded_value_label"))
    , error_lb_(gtr_get_widget<Gtk::Label>(builder, "error_value_label"))
    , date_started_lb_(gtr_get_widget<Gtk::Label>(builder, "running_time_value_label"))
    , eta_lb_(gtr_get_widget<Gtk::Label>(builder, "remaining_time_value_label"))
    , last_activity_lb_(gtr_get_widget<Gtk::Label>(builder, "last_activity_value_label"))
    , hash_lb_(gtr_get_widget<Gtk::Label>(builder, "hash_value_label"))
    , privacy_lb_(gtr_get_widget<Gtk::Label>(builder, "privacy_value_label"))
    , origin_lb_(gtr_get_widget<Gtk::Label>(builder, "origin_value_label"))
    , destination_lb_(gtr_get_widget<Gtk::Label>(builder, "location_value_label"))
    , webseeds_scroll_(gtr_get_widget<Gtk::ScrolledWindow>(builder, "webseeds_view_scroll"))
    , webseeds_view_(gtr_get_widget<Gtk::ColumnView>(builder, "webseeds_view"))
    , peer_view_(gtr_get_widget<Gtk::ColumnView>(builder, "peers_view"))
    , more_peer_details_check_(gtr_get_widget<Gtk::CheckButton>(builder, "more_peer_details_check"))
    , add_tracker_button_(gtr_get_widget<Gtk::Button>(builder, "add_tracker_button"))
    , edit_trackers_button_(gtr_get_widget<Gtk::Button>(builder, "edit_tracker_button"))
    , remove_tracker_button_(gtr_get_widget<Gtk::Button>(builder, "remove_tracker_button"))
    , tracker_view_(gtr_get_widget<Gtk::ListView>(builder, "trackers_view"))
    , scrape_check_(gtr_get_widget<Gtk::CheckButton>(builder, "more_tracker_details_check"))
    , all_check_(gtr_get_widget<Gtk::CheckButton>(builder, "backup_trackers_check"))
    , file_list_(gtr_get_widget_derived<FileList>(builder, "files_view_scroll", "files_view", core, 0))
    , file_label_(gtr_get_widget<Gtk::Label>(builder, "files_label"))
{
    /* return saved window size */
    auto const width = gtr_pref_int_get<int>(TR_KEY_details_window_width);
    auto const height = gtr_pref_int_get<int>(TR_KEY_details_window_height);
#if GTKMM_CHECK_VERSION(4, 0, 0)
    dialog_.set_default_size(width, height);
    dialog_.property_default_width().signal_changed().connect(sigc::mem_fun(*this, &Impl::on_details_window_size_allocated));
    dialog_.property_default_height().signal_changed().connect(sigc::mem_fun(*this, &Impl::on_details_window_size_allocated));
#else
    dialog_.resize(width, height);
    dialog_.signal_size_allocate().connect(sigc::hide<0>(sigc::mem_fun(*this, &Impl::on_details_window_size_allocated)));
#endif

    dialog_.signal_response().connect(sigc::hide<0>(sigc::mem_fun(dialog_, &DetailsDialog::close)));

    info_page_init(builder);
    peer_page_init(builder);
    tracker_page_init(builder);
    options_page_init(builder);

    periodic_refresh_tag_ = Glib::signal_timeout().connect_seconds(
        [this]() { return refresh(), true; },
        SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);

    auto* const n = gtr_get_widget<Gtk::Notebook>(builder, "dialog_pages");
    n->set_current_page(last_page_);
    n->signal_switch_page().connect([](Gtk::Widget* /*page*/, guint page_number) { last_page_ = page_number; });
}

void DetailsDialog::set_torrents(std::vector<tr_torrent_id_t> const& ids)
{
    impl_->set_torrents(ids);
}

void DetailsDialog::refresh()
{
    impl_->refresh();
}

void DetailsDialog::Impl::set_torrents(std::vector<tr_torrent_id_t> const& ids)
{
    Glib::ustring title;
    auto const len = ids.size();

    ids_ = ids;

    if (len == 1)
    {
        int const id = ids.front();
        if (auto const torrent = core_->find_torrent_ref(id); torrent)
        {
            title = fmt::format(
                fmt::runtime(_("{torrent_name} Properties")),
                fmt::arg("torrent_name", torrent->get_name()));
        }
        else if (auto const* tor = core_->find_torrent(id); tor != nullptr)
        {
            title = fmt::format(fmt::runtime(_("{torrent_name} Properties")), fmt::arg("torrent_name", tr_torrentName(tor)));
        }
    }
    else
    {
        title = fmt::format(
            fmt::runtime(ngettext("Properties - {torrent_count:L} Torrent", "Properties - {torrent_count:L} Torrents", len)),
            fmt::arg("torrent_count", len));
    }

    dialog_.set_title(title);

    refresh();
}
