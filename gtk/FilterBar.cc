// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FilterBar.h"

#include "FilterBarRows.h"
#include "FilterListModel.hh"
#include "HigWorkarea.h" // GUI_PAD
#include "Session.h"
#include "Torrent.h"
#include "TorrentFilter.h"
#include "Utils.h"

#include <libtransmission-app/display-modes.h>

#include <libtransmission/tr-macros.h>

#include <giomm/liststore.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/unicode.h>
#include <glibmm/ustring.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listitem.h>
#include <gtkmm/separator.h>
#include <gtkmm/signallistitemfactory.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>

using namespace tr::app;

namespace
{

constexpr auto ShowModeSeparator = static_cast<ShowMode>(-1);

Glib::RefPtr<Gtk::SignalListItemFactory> make_show_mode_dropdown_factory()
{
    static auto const IconKey = Glib::Quark("tr-filter-show-mode-icon");
    static auto const NameKey = Glib::Quark("tr-filter-show-mode-name");
    static auto const CountKey = Glib::Quark("tr-filter-show-mode-count");
    static auto const SeparatorKey = Glib::Quark("tr-filter-show-mode-separator");
    static auto const RowKey = Glib::Quark("tr-filter-show-mode-row");

    auto factory = Gtk::SignalListItemFactory::create();

    factory->signal_setup().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto* const separator = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);

            auto* const row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, GUI_PAD);
            row->set_hexpand(true);

            auto* const icon = Gtk::make_managed<Gtk::Image>();

            auto* const name_label = Gtk::make_managed<Gtk::Label>();
            name_label->set_hexpand(true);
            name_label->set_xalign(0);

            auto* const count_label = Gtk::make_managed<Gtk::Label>();
            count_label->set_xalign(1);
            count_label->add_css_class("dim-label");

            row->append(*icon);
            row->append(*name_label);
            row->append(*count_label);

            list_item->set_data(SeparatorKey, separator);
            list_item->set_data(RowKey, row);
            list_item->set_data(IconKey, icon);
            list_item->set_data(NameKey, name_label);
            list_item->set_data(CountKey, count_label);
        });

    factory->signal_bind().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto const row = gtr_ptr_dynamic_cast<FilterShowModeRow>(list_item->get_item());
            auto* const separator = static_cast<Gtk::Separator*>(list_item->get_data(SeparatorKey));
            auto* const row_box = static_cast<Gtk::Box*>(list_item->get_data(RowKey));
            auto* const icon = static_cast<Gtk::Image*>(list_item->get_data(IconKey));
            auto* const name_label = static_cast<Gtk::Label*>(list_item->get_data(NameKey));
            auto* const count_label = static_cast<Gtk::Label*>(list_item->get_data(CountKey));

            if (row == nullptr || separator == nullptr || row_box == nullptr || icon == nullptr || name_label == nullptr ||
                count_label == nullptr)
            {
                return;
            }

            if (row->is_separator())
            {
                list_item->set_selectable(false);
                list_item->set_child(*separator);
                return;
            }

            list_item->set_selectable(true);

            if (list_item->get_child() != row_box)
            {
                list_item->set_child(*row_box);
            }

            auto const show_mode = row->get_show_mode();
            auto const icon_name = row->get_icon_name();

            if (show_mode == ShowMode::ShowAll || icon_name.empty())
            {
                icon->set_size_request(0, -1);
                icon->set_from_icon_name({});
            }
            else
            {
                icon->set_size_request(20, -1);
                icon->set_from_icon_name(icon_name);
                icon->set_margin_top(2);
                icon->set_margin_bottom(2);
            }

            name_label->set_label(row->get_name());

            auto const count = row->get_count();
            count_label->set_label(count >= 0 ? Glib::ustring(fmt::format("{:L}", count)) : Glib::ustring{});
        });

    return factory;
}

Glib::RefPtr<Gtk::SignalListItemFactory> make_tracker_dropdown_factory()
{
    static auto const IconKey = Glib::Quark("tr-filter-tracker-icon");
    static auto const NameKey = Glib::Quark("tr-filter-tracker-name");
    static auto const CountKey = Glib::Quark("tr-filter-tracker-count");
    static auto const SeparatorKey = Glib::Quark("tr-filter-tracker-separator");
    static auto const RowKey = Glib::Quark("tr-filter-tracker-row");

    auto factory = Gtk::SignalListItemFactory::create();

    factory->signal_setup().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto* const separator = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);

            auto* const row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, GUI_PAD);
            row->set_hexpand(true);

            auto* const icon = Gtk::make_managed<Gtk::Image>();

            auto* const name_label = Gtk::make_managed<Gtk::Label>();
            name_label->set_hexpand(true);
            name_label->set_xalign(0);

            auto* const count_label = Gtk::make_managed<Gtk::Label>();
            count_label->set_xalign(1);
            count_label->add_css_class("dim-label");

            row->append(*icon);
            row->append(*name_label);
            row->append(*count_label);

            list_item->set_data(SeparatorKey, separator);
            list_item->set_data(RowKey, row);
            list_item->set_data(IconKey, icon);
            list_item->set_data(NameKey, name_label);
            list_item->set_data(CountKey, count_label);
        });

    factory->signal_bind().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto const tracker_row = gtr_ptr_dynamic_cast<FilterTrackerRow>(list_item->get_item());
            auto* const separator = static_cast<Gtk::Separator*>(list_item->get_data(SeparatorKey));
            auto* const row_box = static_cast<Gtk::Box*>(list_item->get_data(RowKey));
            auto* const icon = static_cast<Gtk::Image*>(list_item->get_data(IconKey));
            auto* const name_label = static_cast<Gtk::Label*>(list_item->get_data(NameKey));
            auto* const count_label = static_cast<Gtk::Label*>(list_item->get_data(CountKey));

            if (tracker_row == nullptr || separator == nullptr || row_box == nullptr || icon == nullptr ||
                name_label == nullptr || count_label == nullptr)
            {
                return;
            }

            if (tracker_row->is_separator())
            {
                list_item->set_selectable(false);
                list_item->set_child(*separator);
                return;
            }

            list_item->set_selectable(true);

            if (list_item->get_child() != row_box)
            {
                list_item->set_child(*row_box);
            }

            if (tracker_row->get_type() == FilterTrackerRow::TrackerType::HOST)
            {
                icon->set_size_request(20, -1);
                if (auto const pixbuf = tracker_row->get_pixbuf(); pixbuf != nullptr)
                {
                    icon->set(pixbuf);
                }
                else
                {
                    icon->set(Glib::RefPtr<Gdk::Pixbuf>{});
                }
            }
            else
            {
                icon->set_size_request(0, -1);
                icon->set(Glib::RefPtr<Gdk::Pixbuf>{});
            }

            name_label->set_label(tracker_row->get_displayname());

            auto const count = tracker_row->get_count();
            count_label->set_label(count >= 0 ? Glib::ustring(fmt::format("{:L}", count)) : Glib::ustring{});
        });

    return factory;
}

} // namespace

class FilterBar::Impl
{
public:
    Impl(FilterBar& widget, Glib::RefPtr<Session> const& core);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

    [[nodiscard]] Glib::RefPtr<Gtk::FilterListModel> get_filter_model() const;

private:
    template<typename T>
    T* get_template_child(char const* name) const;

    void show_mode_dropdown_init(Gtk::DropDown& dropdown);
    void tracker_dropdown_init(Gtk::DropDown& dropdown);

    void update_filter_show_mode();
    void update_filter_tracker();
    void update_filter_text();

    bool show_mode_filter_model_update();

    bool tracker_filter_model_update();
    void favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const* pixbuf, guint position);

    void update_filter_models(Torrent::ChangeFlags changes);
    void update_filter_models_idle(Torrent::ChangeFlags changes);

    void update_count_label_idle();
    bool update_count_label();

    static Glib::RefPtr<Gio::ListStore<FilterShowModeRow>> show_mode_filter_model_new();
    static Glib::RefPtr<Gio::ListStore<FilterTrackerRow>> tracker_filter_model_new();

    static Glib::ustring get_name_from_host(std::string const& host);

    [[nodiscard]] FilterShowModeRow* get_selected_show_mode_row() const;
    [[nodiscard]] FilterTrackerRow* get_selected_tracker_row() const;

private:
    FilterBar& widget_;
    Glib::RefPtr<Session> const core_;

    Glib::RefPtr<Gio::ListStore<FilterShowModeRow>> const show_mode_model_;
    Glib::RefPtr<Gio::ListStore<FilterTrackerRow>> const tracker_model_;

    Gtk::DropDown* show_mode_ = nullptr;
    Gtk::DropDown* tracker_ = nullptr;
    Gtk::Entry* entry_ = nullptr;
    Gtk::Label* show_lb_ = nullptr;
    Glib::RefPtr<TorrentFilter> filter_ = TorrentFilter::create();
    Glib::RefPtr<FilterListModel<Torrent>> filter_model_;

    sigc::connection update_count_label_tag_;
    sigc::connection update_filter_models_tag_;
    sigc::connection update_filter_models_on_add_remove_tag_;
    sigc::connection update_filter_models_on_change_tag_;
};

Glib::ustring FilterBar::Impl::get_name_from_host(std::string const& host)
{
    std::string name = host;

    if (!name.empty())
    {
        name.front() = Glib::Ascii::toupper(name.front());
    }

    return name;
}

FilterShowModeRow* FilterBar::Impl::get_selected_show_mode_row() const
{
    auto const selected = show_mode_->get_selected();
    if (selected == GTK_INVALID_LIST_POSITION)
    {
        return nullptr;
    }

    return gtr_ptr_dynamic_cast<FilterShowModeRow>(show_mode_model_->get_item(selected)).get();
}

FilterTrackerRow* FilterBar::Impl::get_selected_tracker_row() const
{
    auto const selected = tracker_->get_selected();
    if (selected == GTK_INVALID_LIST_POSITION)
    {
        return nullptr;
    }

    return gtr_ptr_dynamic_cast<FilterTrackerRow>(tracker_model_->get_item(selected)).get();
}

void FilterBar::Impl::favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const* pixbuf, guint const position)
{
    if (pixbuf != nullptr && *pixbuf != nullptr && position < tracker_model_->get_n_items())
    {
        if (auto const row = gtr_ptr_dynamic_cast<FilterTrackerRow>(tracker_model_->get_item(position)))
        {
            row->set_pixbuf(*pixbuf);
        }
    }
}

bool FilterBar::Impl::tracker_filter_model_update()
{
    struct site_info
    {
        int count = 0;
        std::string host;
        std::string sitename;
        std::string announce_url;

        TR_CONSTEXPR_STR auto operator<=>(site_info const& that) const noexcept
        {
            return sitename <=> that.sitename;
        }

        TR_CONSTEXPR_STR auto operator==(site_info const& that) const
        {
            return sitename == that.sitename;
        }
    };

    auto const torrents_model = core_->get_model();

    auto n_torrents = 0;
    auto site_infos = std::unordered_map<std::string /*site*/, site_info>{};
    for (auto i = 0U, count = torrents_model->get_n_items(); i < count; ++i)
    {
        auto const torrent = gtr_ptr_dynamic_cast<Torrent>(torrents_model->get_object(i));
        if (torrent == nullptr)
        {
            continue;
        }

        if (torrent->is_remote_view())
        {
            for (auto const& sitename : torrent->get_tracker_sitenames())
            {
                auto& info = site_infos[sitename];
                info.sitename = sitename;
                info.host = sitename;
                ++info.count;
            }
        }
        else
        {
            auto const& raw_torrent = torrent->get_underlying();

            auto site_to_host_and_announce = std::map<std::string, std::pair<std::string, std::string>>{};
            for (size_t j = 0, n = tr_torrentTrackerCount(&raw_torrent); j < n; ++j)
            {
                auto const view = tr_torrentTracker(&raw_torrent, j);
                site_to_host_and_announce.try_emplace(std::data(view.sitename), view.host_and_port, view.announce);
            }

            for (auto const& [sitename, host_and_announce] : site_to_host_and_announce)
            {
                auto& info = site_infos[sitename];
                info.host = host_and_announce.first;
                info.announce_url = host_and_announce.second;
                info.sitename = sitename;
                ++info.count;
            }
        }

        ++n_torrents;
    }

    auto const n_sites = std::size(site_infos);
    auto sites_v = std::vector<site_info>(n_sites);
    std::ranges::transform(site_infos, std::begin(sites_v), [](auto const& it) { return it.second; });
    std::ranges::sort(sites_v);

    if (auto const all_row = gtr_ptr_dynamic_cast<FilterTrackerRow>(tracker_model_->get_item(0)))
    {
        all_row->set_count(n_torrents);
    }

    size_t i = 0;
    guint pos = 2;

    for (;;)
    {
        bool const new_sites_done = i >= n_sites;
        bool const old_sites_done = pos >= tracker_model_->get_n_items();

        if (new_sites_done && old_sites_done)
        {
            break;
        }

        bool remove_row = false;
        bool insert_row = false;

        if (new_sites_done)
        {
            remove_row = true;
        }
        else if (old_sites_done)
        {
            insert_row = true;
        }
        else
        {
            auto const existing = gtr_ptr_dynamic_cast<FilterTrackerRow>(tracker_model_->get_item(pos));
            auto const sitename = existing != nullptr ? existing->get_sitename().raw() : std::string{};
            int const cmp = sitename.compare(sites_v.at(i).sitename);

            if (cmp < 0)
            {
                remove_row = true;
            }
            else if (cmp > 0)
            {
                insert_row = true;
            }
        }

        if (remove_row)
        {
            tracker_model_->remove(pos);
        }
        else if (insert_row)
        {
            auto const& site = sites_v.at(i);
            auto const new_row = FilterTrackerRow::create(
                get_name_from_host(site.sitename),
                site.count,
                FilterTrackerRow::TrackerType::HOST,
                Glib::ustring{ site.sitename });
            tracker_model_->insert(pos, new_row);
            core_->favicon_cache().load(
                site.announce_url,
                [this, pos](auto const* pixbuf) { favicon_ready_cb(pixbuf, pos); });
            ++i;
            ++pos;
        }
        else
        {
            if (auto const existing = gtr_ptr_dynamic_cast<FilterTrackerRow>(tracker_model_->get_item(pos)))
            {
                existing->set_count(sites_v.at(i).count);
            }

            ++i;
            ++pos;
        }
    }

    return false;
}

Glib::RefPtr<Gio::ListStore<FilterTrackerRow>> FilterBar::Impl::tracker_filter_model_new()
{
    auto store = Gio::ListStore<FilterTrackerRow>::create();
    store->append(FilterTrackerRow::create(Glib::ustring(_("All")), 0, FilterTrackerRow::TrackerType::ALL, {}));
    store->append(FilterTrackerRow::create_separator());
    return store;
}

bool FilterBar::Impl::show_mode_filter_model_update()
{
    auto const torrents_model = core_->get_model();

    for (guint pos = 0, n = show_mode_model_->get_n_items(); pos < n; ++pos)
    {
        auto const row = gtr_ptr_dynamic_cast<FilterShowModeRow>(show_mode_model_->get_item(pos));
        if (row == nullptr || row->is_separator())
        {
            continue;
        }

        auto const type = row->get_show_mode();
        auto hits = 0;

        for (auto i = 0U, count = torrents_model->get_n_items(); i < count; ++i)
        {
            auto const torrent = gtr_ptr_dynamic_cast<Torrent>(torrents_model->get_object(i));
            if (torrent != nullptr && TorrentFilter::match_mode(*torrent, type))
            {
                ++hits;
            }
        }

        row->set_count(hits);
    }

    return false;
}

Glib::RefPtr<Gio::ListStore<FilterShowModeRow>> FilterBar::Impl::show_mode_filter_model_new()
{
    struct FilterTypeInfo
    {
        ShowMode show_mode;
        char const* context;
        char const* name;
        char const* icon_name;
    };

    static auto constexpr types = std::array<FilterTypeInfo, 9>({ {
        { .show_mode = ShowMode::ShowAll, .context = nullptr, .name = N_("All"), .icon_name = nullptr },
        { .show_mode = ShowModeSeparator, .context = nullptr, .name = nullptr, .icon_name = nullptr },
        { .show_mode = ShowMode::ShowActive, .context = nullptr, .name = N_("Active"), .icon_name = "system-run" },
        { .show_mode = ShowMode::ShowDownloading,
          .context = "Verb",
          .name = NC_("Verb", "Downloading"),
          .icon_name = "network-receive" },
        { .show_mode = ShowMode::ShowSeeding,
          .context = "Verb",
          .name = NC_("Verb", "Seeding"),
          .icon_name = "network-transmit" },
        { .show_mode = ShowMode::ShowPaused, .context = nullptr, .name = N_("Paused"), .icon_name = "media-playback-pause" },
        { .show_mode = ShowMode::ShowFinished, .context = nullptr, .name = N_("Finished"), .icon_name = "media-playback-stop" },
        { .show_mode = ShowMode::ShowVerifying,
          .context = "Verb",
          .name = NC_("Verb", "Verifying"),
          .icon_name = "view-refresh" },
        { .show_mode = ShowMode::ShowError, .context = nullptr, .name = N_("Error"), .icon_name = "dialog-error" },
    } });

    auto store = Gio::ListStore<FilterShowModeRow>::create();

    for (auto const& type : types)
    {
        if (type.show_mode == ShowModeSeparator)
        {
            store->append(FilterShowModeRow::create_separator());
            continue;
        }

        auto const name = type.name != nullptr ?
            Glib::ustring(type.context != nullptr ? g_dpgettext2(nullptr, type.context, type.name) : _(type.name)) :
            Glib::ustring();
        auto const icon_name = type.icon_name != nullptr ? Glib::ustring(type.icon_name) : Glib::ustring{};

        store->append(FilterShowModeRow::create(name, 0, type.show_mode, icon_name));
    }

    return store;
}

void FilterBar::Impl::show_mode_dropdown_init(Gtk::DropDown& dropdown)
{
    dropdown.set_model(show_mode_model_);
    dropdown.set_factory(make_show_mode_dropdown_factory());
    dropdown.set_selected(0);
}

void FilterBar::Impl::tracker_dropdown_init(Gtk::DropDown& dropdown)
{
    dropdown.set_model(tracker_model_);
    dropdown.set_factory(make_tracker_dropdown_factory());
    dropdown.set_selected(0);
}

void FilterBar::Impl::update_filter_text()
{
    filter_->set_text(entry_->get_text());
}

void FilterBar::Impl::update_filter_show_mode()
{
    if (auto* const row = get_selected_show_mode_row(); row != nullptr && !row->is_separator())
    {
        filter_->set_mode(row->get_show_mode());
    }
    else
    {
        filter_->set_mode(ShowMode::ShowAll);
    }
}

void FilterBar::Impl::update_filter_tracker()
{
    if (auto* const row = get_selected_tracker_row(); row != nullptr && !row->is_separator())
    {
        filter_->set_tracker(row->get_type(), row->get_sitename());
    }
    else
    {
        filter_->set_tracker(FilterTrackerRow::TrackerType::ALL, {});
    }
}

bool FilterBar::Impl::update_count_label()
{
    auto const visibleCount = static_cast<int>(filter_model_->get_n_items());

    int trackerCount = 0;
    if (auto* const row = get_selected_tracker_row(); row != nullptr)
    {
        trackerCount = row->get_count();
    }

    int modeCount = 0;
    if (auto* const row = get_selected_show_mode_row(); row != nullptr)
    {
        modeCount = row->get_count();
    }

    if (auto const new_markup = visibleCount == std::min(modeCount, trackerCount) ?
            _("_Show:") :
            fmt::format(fmt::runtime(_("_Show {count:L} of:")), fmt::arg("count", visibleCount));
        new_markup != show_lb_->get_label().raw())
    {
        show_lb_->set_markup_with_mnemonic(new_markup);
    }

    return false;
}

void FilterBar::Impl::update_count_label_idle()
{
    if (!update_count_label_tag_.connected())
    {
        update_count_label_tag_ = Glib::signal_idle().connect(sigc::mem_fun(*this, &Impl::update_count_label));
    }
}

void FilterBar::Impl::update_filter_models(Torrent::ChangeFlags changes)
{
    static auto TR_CONSTEXPR23 show_mode_flags = Torrent::ChangeFlag::ACTIVE_PEERS_DOWN | Torrent::ChangeFlag::ACTIVE_PEERS_UP |
        Torrent::ChangeFlag::ACTIVE | Torrent::ChangeFlag::ACTIVITY | Torrent::ChangeFlag::ERROR_CODE |
        Torrent::ChangeFlag::FINISHED;
    static auto constexpr tracker_flags = Torrent::ChangeFlag::TRACKERS;

    if (changes.test(show_mode_flags))
    {
        show_mode_filter_model_update();
    }

    if (changes.test(tracker_flags))
    {
        tracker_filter_model_update();
    }

    filter_->update(changes);

    if (changes.test(show_mode_flags | tracker_flags))
    {
        update_count_label_idle();
    }
}

void FilterBar::Impl::update_filter_models_idle(Torrent::ChangeFlags changes)
{
    if (!update_filter_models_tag_.connected())
    {
        update_filter_models_tag_ = Glib::signal_idle().connect(
            [this, changes]()
            {
                update_filter_models(changes);
                return false;
            });
    }
}

FilterBarExtraInit::FilterBarExtraInit()
    : ExtraClassInit(&FilterBarExtraInit::class_init, nullptr, &FilterBarExtraInit::instance_init)
{
}

void FilterBarExtraInit::class_init(void* klass, void* /*user_data*/)
{
    auto* const widget_klass = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(widget_klass, gtr_get_full_resource_path("FilterBar.ui").c_str());

    gtk_widget_class_bind_template_child_full(widget_klass, "show_mode_combo", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "tracker_combo", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "text_entry", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "show_label", FALSE, 0);
}

void FilterBarExtraInit::instance_init(GTypeInstance* instance, void* /*klass*/)
{
    gtk_widget_init_template(GTK_WIDGET(instance));
}

FilterBar::FilterBar()
    : Glib::ObjectBase(typeid(FilterBar))
{
}

FilterBar::FilterBar(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& /*builder*/,
    Glib::RefPtr<Session> const& core)
    : Glib::ObjectBase(typeid(FilterBar))
    , Gtk::Box(cast_item)
    , impl_(std::make_unique<Impl>(*this, core))
{
}

FilterBar::~FilterBar() = default;

FilterBar::Impl::Impl(FilterBar& widget, Glib::RefPtr<Session> const& core)
    : widget_(widget)
    , core_(core)
    , show_mode_model_(show_mode_filter_model_new())
    , tracker_model_(tracker_filter_model_new())
    , show_mode_(get_template_child<Gtk::DropDown>("show_mode_combo"))
    , tracker_(get_template_child<Gtk::DropDown>("tracker_combo"))
    , entry_(get_template_child<Gtk::Entry>("text_entry"))
    , show_lb_(get_template_child<Gtk::Label>("show_label"))
{
    update_filter_models_on_add_remove_tag_ = core_->get_model()->signal_items_changed().connect(
        [this](guint /*position*/, guint /*removed*/, guint /*added*/) { update_filter_models_idle(~Torrent::ChangeFlags()); });
    update_filter_models_on_change_tag_ = core_->signal_torrents_changed().connect(
        sigc::hide<0>(sigc::mem_fun(*this, &Impl::update_filter_models_idle)));

    show_mode_filter_model_update();
    tracker_filter_model_update();

    show_mode_dropdown_init(*show_mode_);
    tracker_dropdown_init(*tracker_);

    filter_->signal_changed().connect([this](auto /*changes*/) { update_count_label_idle(); });

    filter_model_ = FilterListModel<Torrent>::create(core_->get_sorted_model(), filter_);

    tracker_->property_selected().signal_changed().connect(sigc::mem_fun(*this, &Impl::update_filter_tracker));
    show_mode_->property_selected().signal_changed().connect(sigc::mem_fun(*this, &Impl::update_filter_show_mode));

    entry_->signal_icon_release().connect([this](auto /*icon_position*/) { entry_->set_text({}); });
    entry_->signal_changed().connect(sigc::mem_fun(*this, &Impl::update_filter_text));
}

FilterBar::Impl::~Impl()
{
    update_filter_models_on_change_tag_.disconnect();
    update_filter_models_on_add_remove_tag_.disconnect();
    update_filter_models_tag_.disconnect();
    update_count_label_tag_.disconnect();
}

Glib::RefPtr<FilterBar::Model> FilterBar::get_filter_model() const
{
    return impl_->get_filter_model();
}

Glib::RefPtr<Gtk::FilterListModel> FilterBar::Impl::get_filter_model() const
{
    return filter_model_;
}

template<typename T>
T* FilterBar::Impl::get_template_child(char const* name) const
{
    auto full_type_name = std::string("gtkmm__CustomObject_");
    Glib::append_canonical_typename(full_type_name, typeid(FilterBar).name());

    return Glib::wrap(G_TYPE_CHECK_INSTANCE_CAST(
        gtk_widget_get_template_child(GTK_WIDGET(widget_.gobj()), g_type_from_name(full_type_name.c_str()), name),
        T::get_base_type(),
        typename T::BaseObjectType));
}
