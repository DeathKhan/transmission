// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "MessageLogWindow.h"

#include "Actions.h"
#include "EnumDropdown.h"
#include "FilterListModel.hh"
#include "GtkCompat.h"
#include "MessageLogRow.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission/log.h>

#include <giomm/liststore.h>
#include <giomm/simpleaction.h>
#include <glibmm/convert.h>
#include <glibmm/datetime.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <glibmm/ustring.h>
#include <glibmm/variant.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/box.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/label.h>
#include <gtkmm/listitem.h>
#include <gtkmm/listview.h>
#include <gtkmm/noselection.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/signallistitemfactory.h>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <memory>
#include <ranges>
#include <utility>

namespace
{

tr_log_message* myTail = nullptr;
tr_log_message* myHead = nullptr;

auto constexpr level_names_ = std::array<std::pair<tr_log_level, char const*>, 5U>{ {
    { TR_LOG_CRITICAL, NC_("Logging level", "Critical") },
    { TR_LOG_ERROR, NC_("Logging level", "Error") },
    { TR_LOG_WARN, NC_("Logging level", "Warning") },
    { TR_LOG_INFO, NC_("Logging level", "Information") },
    { TR_LOG_DEBUG, NC_("Logging level", "Debug") },
} };

using std::chrono::system_clock;

Glib::ustring gtr_asctime(system_clock::time_point t)
{
    return Glib::DateTime::create_now_local(system_clock::to_time_t(t)).format("%a %b %e %T %Y");
}

void set_log_level_style(Gtk::Label& label, tr_log_level const level)
{
    switch (level)
    {
    case TR_LOG_CRITICAL:
    case TR_LOG_ERROR:
    case TR_LOG_WARN:
        label.set_markup(fmt::format("<span foreground='red'>{}</span>", label.get_label()));
        break;

    case TR_LOG_DEBUG:
    case TR_LOG_TRACE:
        label.set_markup(fmt::format("<span foreground='forestgreen'>{}</span>", label.get_label()));
        break;

    default:
        label.set_use_markup(false);
        break;
    }
}

} // namespace

class MessageLogWindow::Impl
{
public:
    Impl(MessageLogWindow& window, Glib::RefPtr<Gtk::Builder> const& builder, Glib::RefPtr<Session> const& core);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

private:
    bool onRefresh();

    void onSaveRequest();
    void doSave(std::string const& filename);

    void onClearRequest();
    void onPauseToggled(Gio::SimpleAction& action);

    void scroll_to_bottom();
    void level_dropdown_init(Gtk::DropDown& dropdown);
    void level_dropdown_changed(Gtk::DropDown& dropdown);

    void setup_list_view();

    [[nodiscard]] bool is_pinned_to_new() const;

private:
    MessageLogWindow& window_;
    Glib::RefPtr<Session> const core_;

    Gtk::ListView* view_ = nullptr;
    Gtk::ScrolledWindow* scroll_ = nullptr;
    Glib::RefPtr<Gio::ListStore<MessageLogRow>> store_;
    Glib::RefPtr<MessageLogFilter> filter_;
    Glib::RefPtr<FilterListModel<MessageLogRow>> filter_model_;
    Glib::RefPtr<Gtk::NoSelection> selection_;
    Glib::RefPtr<Gtk::SignalListItemFactory> item_factory_;
    tr_log_level maxLevel_ = TR_LOG_INFO;
    bool isPaused_ = false;
    sigc::connection refresh_tag_;
};

/****
*****
****/

bool MessageLogWindow::Impl::is_pinned_to_new() const
{
    if (scroll_ == nullptr)
    {
        return true;
    }

    if (auto const adj = scroll_->get_vadjustment())
    {
        return adj->get_value() + adj->get_page_size() >= adj->get_upper() - 1.0;
    }

    return true;
}

void MessageLogWindow::Impl::scroll_to_bottom()
{
    auto const n_items = filter_model_->get_n_items();
    if (n_items == 0)
    {
        return;
    }

    if (view_ != nullptr)
    {
        view_->scroll_to(n_items - 1);
    }

    if (scroll_ != nullptr)
    {
        if (auto const adj = scroll_->get_vadjustment())
        {
            adj->set_value(adj->get_upper() - adj->get_page_size());
        }
    }
}

/****
*****
****/

void MessageLogWindow::Impl::level_dropdown_init(Gtk::DropDown& dropdown)
{
    auto const pref_level = gtr_pref_get<tr_log_level>(TR_KEY_message_level);
    auto const default_level = TR_LOG_INFO;

    auto has_pref_level = false;
    auto items = std::vector<std::pair<Glib::ustring, int>>{};
    items.reserve(std::size(level_names_));
    for (auto const& [level, name] : level_names_)
    {
        items.emplace_back(g_dpgettext2(nullptr, "Logging level", name), level);
        has_pref_level |= level == pref_level;
    }

    enum_dropdown_init(dropdown, items);
    enum_dropdown_set_value(dropdown, has_pref_level ? *pref_level : default_level);
}

void MessageLogWindow::Impl::level_dropdown_changed(Gtk::DropDown& dropdown)
{
    auto const level = static_cast<tr_log_level>(enum_dropdown_get_value(dropdown));
    bool const pinned_to_new = is_pinned_to_new();

    tr_logSetLevel(level);
    core_->set_pref(TR_KEY_message_level, level);
    maxLevel_ = level;
    filter_->set_max_level(level);

    if (pinned_to_new)
    {
        scroll_to_bottom();
    }
}

void MessageLogWindow::Impl::doSave(std::string const& filename)
{
    try
    {
        auto stream = std::ofstream();
        stream.exceptions(std::ios_base::failbit | std::ios_base::badbit);
        stream.open(filename, std::ios_base::trunc);

        auto const n_items = store_->get_n_items();
        for (guint i = 0; i < n_items; ++i)
        {
            auto const row = store_->get_item(i);
            auto const* const node = row->get_tr_msg();
            auto const date = gtr_asctime(node->when);

            auto const iter = std::ranges::find_if(
                level_names_,
                [key = node->level](auto const& item) { return item.first == key; });
            auto const level_str = iter != std::ranges::end(level_names_) ?
                Glib::ustring(g_dpgettext2(nullptr, "Logging level", iter->second)) :
                Glib::ustring("???");

            fmt::print(stream, "{}\t{}\t{}\t{}\n", date, level_str, node->name, node->message);
        }
    }
    catch (std::ios_base::failure const& e)
    {
        auto dialog = Gtk::AlertDialog::create(fmt::format(
            fmt::runtime(_("Couldn't save '{path}': {error} ({error_code})")),
            fmt::arg("path", Glib::filename_to_utf8(filename)),
            fmt::arg("error", e.code().message()),
            fmt::arg("error_code", e.code().value())));
        dialog->set_detail(e.code().message());
        dialog->show(window_);
    }
}

void MessageLogWindow::Impl::onSaveRequest()
{
    auto const dialog = Gtk::FileDialog::create();
    dialog->set_title(_("Save Log"));
    dialog->set_modal(true);
    dialog->set_accept_label(_("_Save"));

    dialog->save(
        window_,
        [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result)
        {
            try
            {
                if (auto const file = dialog->save_finish(result); file)
                {
                    doSave(file->get_path());
                }
            }
            catch (Glib::Error const&)
            {
            }
        });
}

void MessageLogWindow::Impl::onClearRequest()
{
    store_->splice(0, store_->get_n_items(), {});
    tr_logFreeQueue(myHead);
    myHead = myTail = nullptr;
}

void MessageLogWindow::Impl::onPauseToggled(Gio::SimpleAction& action)
{
    bool value = false;
    action.get_state(value);

    action.set_state(Glib::Variant<bool>::create(!value));

    isPaused_ = !value;
}

void MessageLogWindow::Impl::setup_list_view()
{
    item_factory_ = Gtk::SignalListItemFactory::create();

    static auto const TimeLabelKey = Glib::Quark("tr-message-log-time-label");
    static auto const NameLabelKey = Glib::Quark("tr-message-log-name-label");
    static auto const MessageLabelKey = Glib::Quark("tr-message-log-message-label");

    item_factory_->signal_setup().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto* const row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            row->set_hexpand(true);

            auto* const time_label = Gtk::make_managed<Gtk::Label>();
            time_label->set_width_chars(10);
            time_label->set_xalign(0);
            time_label->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);

            auto* const name_label = Gtk::make_managed<Gtk::Label>();
            name_label->set_width_chars(24);
            name_label->set_max_width_chars(24);
            name_label->set_xalign(0);
            name_label->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);

            auto* const message_label = Gtk::make_managed<Gtk::Label>();
            message_label->set_hexpand(true);
            message_label->set_xalign(0);
            message_label->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);

            row->append(*time_label);
            row->append(*name_label);
            row->append(*message_label);

            list_item->set_data(TimeLabelKey, time_label);
            list_item->set_data(NameLabelKey, name_label);
            list_item->set_data(MessageLabelKey, message_label);
            list_item->set_child(*row);
        });

    item_factory_->signal_bind().connect(
        [](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto const row = gtr_ptr_dynamic_cast<MessageLogRow>(list_item->get_item());
            if (row == nullptr)
            {
                return;
            }

            auto* time_label = static_cast<Gtk::Label*>(list_item->get_data(TimeLabelKey));
            auto* name_label = static_cast<Gtk::Label*>(list_item->get_data(NameLabelKey));
            auto* message_label = static_cast<Gtk::Label*>(list_item->get_data(MessageLabelKey));
            if (time_label == nullptr || name_label == nullptr || message_label == nullptr)
            {
                return;
            }

            auto const* const node = row->get_tr_msg();
            auto const time_text = Glib::DateTime::create_now_local(std::chrono::system_clock::to_time_t(node->when))
                                       .format("%T");

            time_label->set_use_markup(false);
            time_label->set_label(time_text);
            set_log_level_style(*time_label, node->level);

            name_label->set_use_markup(false);
            name_label->set_label(row->get_name());
            set_log_level_style(*name_label, node->level);

            message_label->set_use_markup(false);
            message_label->set_label(row->get_message());
            set_log_level_style(*message_label, node->level);
        });

    view_->set_factory(item_factory_);
    selection_ = Gtk::NoSelection::create(filter_model_);
    view_->set_model(selection_);
}

MessageLogWindow::Impl::~Impl()
{
    refresh_tag_.disconnect();
}

namespace
{

tr_log_message* addMessages(Glib::RefPtr<Gio::ListStore<MessageLogRow>> const& store, tr_log_message* head)
{
    static unsigned int sequence = 0;
    auto const default_name = Glib::get_application_name();

    while (head != nullptr && head->next != nullptr)
    {
        auto const& message = *head;
        head = head->next;

        char const* name = !std::empty(message.name) ? message.name.c_str() : default_name.c_str();

        store->append(MessageLogRow::create(&message, ++sequence, name, message.message));

        if (message.level == TR_LOG_ERROR)
        {
            auto gstr = fmt::format("{}:{} {}", message.file, message.line, message.message);

            if (!std::empty(message.name))
            {
                gstr += fmt::format(" ({})", message.name.c_str());
            }

            gtr_warning(gstr);
        }
    }

    return head;
}

} // namespace

bool MessageLogWindow::Impl::onRefresh()
{
    bool const pinned_to_new = is_pinned_to_new();

    if (!isPaused_)
    {
        if (auto* msgs = tr_logGetQueue(); msgs != nullptr)
        {
            tr_log_message* tail = addMessages(store_, msgs);

            if (myTail != nullptr)
            {
                myTail->next = msgs;
            }
            else
            {
                myHead = msgs;
            }

            myTail = tail;
        }

        if (pinned_to_new)
        {
            scroll_to_bottom();
        }
    }

    return true;
}

/**
***  Public Functions
**/

std::unique_ptr<MessageLogWindow> MessageLogWindow::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("MessageLogWindow.ui"));
    return std::unique_ptr<MessageLogWindow>(
        gtr_get_widget_derived<MessageLogWindow>(builder, "MessageLogWindow", parent, core));
}

MessageLogWindow::MessageLogWindow(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core)
    : Gtk::Window(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core))
{
    set_transient_for(parent);
}

MessageLogWindow::~MessageLogWindow() = default;

MessageLogWindow::Impl::Impl(
    MessageLogWindow& window,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core)
    : window_(window)
    , core_(core)
    , view_(gtr_get_widget<Gtk::ListView>(builder, "messages_view"))
    , scroll_(gtr_get_widget<Gtk::ScrolledWindow>(builder, "messages_view_scroll"))
    , store_(Gio::ListStore<MessageLogRow>::create())
    , filter_(MessageLogFilter::create())
    , filter_model_(FilterListModel<MessageLogRow>::create(store_, filter_))
    , maxLevel_(gtr_pref_get<tr_log_level>(TR_KEY_message_level).value_or(tr_log_level{}))
    , refresh_tag_(
          Glib::signal_timeout().connect_seconds(
              sigc::mem_fun(*this, &Impl::onRefresh),
              SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS))
{
    auto const action_group = Gio::SimpleActionGroup::create();

    auto const save_action = Gio::SimpleAction::create("save-message-log");
    save_action->signal_activate().connect([this](auto const& /*value*/) { onSaveRequest(); });
    action_group->add_action(save_action);

    auto const clear_action = Gio::SimpleAction::create("clear-message-log");
    clear_action->signal_activate().connect([this](auto const& /*value*/) { onClearRequest(); });
    action_group->add_action(clear_action);

    auto const pause_action = Gio::SimpleAction::create_bool("pause-message-log");
    pause_action->signal_activate().connect([this, &action = *pause_action](auto const& /*value*/) { onPauseToggled(action); });
    action_group->add_action(pause_action);

    auto* const level_dropdown = gtr_get_widget<Gtk::DropDown>(builder, "level_combo");
    level_dropdown_init(*level_dropdown);
    level_dropdown->property_selected().signal_changed().connect(
        [this, level_dropdown]() { level_dropdown_changed(*level_dropdown); });

    window_.insert_action_group("win", action_group);

    setup_list_view();

    addMessages(store_, myHead);
    onRefresh();

    filter_->set_max_level(maxLevel_);
    level_dropdown_changed(*level_dropdown);

    setup_item_view_button_event_handling(
        *view_,
        {},
        [view = view_](double view_x, double view_y) { return on_item_view_button_released(*view, view_x, view_y); });

    scroll_to_bottom();
}
