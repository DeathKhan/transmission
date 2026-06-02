// This file Copyright © Transmission authors and contributors.
// SPDX-License-Identifier: MIT

#include "PathButton.h"

#include "Utils.h"

#include <giomm/file.h>
#include <giomm/liststore.h>
#include <glibmm/error.h>
#include <glibmm/i18n.h>
#include <glibmm/property.h>
#include <gtkmm/box.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>

#include <string_view>
#include <vector>

using namespace std::string_view_literals;

class PathButton::Impl
{
public:
    explicit Impl(PathButton& widget);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl() = default;

    [[nodiscard]] std::string const& get_filename() const
    {
        return current_file_;
    }

    void set_filename(std::string const& value);

    void set_shortcut_folders(std::list<std::string> const& value);

    void add_filter(Glib::RefPtr<Gtk::FileFilter> const& value);

    Glib::Property<Glib::ustring>& property_action()
    {
        return action_;
    }

    Glib::Property<Glib::ustring>& property_title()
    {
        return title_;
    }

    sigc::signal<void()>& signal_selection_changed()
    {
        return selection_changed_;
    }

private:
    [[nodiscard]] bool is_folder_action() const;

    void show_dialog();
    void update();
    void update_mode();

    PathButton& widget_;
    Glib::Property<Glib::ustring> action_;
    Glib::Property<Glib::ustring> title_;

    sigc::signal<void()> selection_changed_;

    Gtk::Image* const image_ = nullptr;
    Gtk::Label* const label_ = nullptr;
    Gtk::Image* const mode_ = nullptr;

    std::string current_file_;
    std::list<std::string> shortcut_folders_;
    std::vector<Glib::RefPtr<Gtk::FileFilter>> filters_;
};

PathButton::Impl::Impl(PathButton& widget)
    : widget_(widget)
    , action_(widget, "action", Glib::ustring{ "open" })
    , title_(widget, "title", {})
    , image_(Gtk::make_managed<Gtk::Image>())
    , label_(Gtk::make_managed<Gtk::Label>())
    , mode_(Gtk::make_managed<Gtk::Image>())
{
    action_.get_proxy().signal_changed().connect([this]() { update_mode(); });

    label_->set_ellipsize(Pango::EllipsizeMode::END);
    label_->set_hexpand(true);
    label_->set_halign(Gtk::Align::START);

    auto* const layout = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
    layout->append(*image_);
    layout->append(*label_);
    layout->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    layout->append(*mode_);
    widget_.set_child(*layout);

    widget_.signal_clicked().connect(sigc::mem_fun(*this, &Impl::show_dialog));

    update();
    update_mode();
}

bool PathButton::Impl::is_folder_action() const
{
    return action_.get_value() == "select-folder";
}

void PathButton::Impl::set_filename(std::string const& value)
{
    current_file_ = value;
    update();
    selection_changed_.emit();
}

void PathButton::Impl::set_shortcut_folders(std::list<std::string> const& value)
{
    shortcut_folders_ = value;
}

void PathButton::Impl::add_filter(Glib::RefPtr<Gtk::FileFilter> const& value)
{
    filters_.push_back(value);
}

void PathButton::Impl::show_dialog()
{
    auto dialog = Gtk::FileDialog::create();
    auto const title = title_.get_value();
    dialog->set_title(!title.empty() ? title : _("Select a File"));
    dialog->set_modal(true);

    if (!current_file_.empty())
    {
        dialog->set_initial_file(Gio::File::create_for_path(current_file_));
    }

    if (!filters_.empty())
    {
        auto filter_list = Gio::ListStore<Gtk::FileFilter>::create();
        for (auto const& filter : filters_)
        {
            filter_list->append(filter);
        }

        dialog->set_filters(filter_list);
        dialog->set_default_filter(filters_.front());
    }

    auto on_done = [this, dialog](Glib::RefPtr<Gio::AsyncResult>& result, bool const folder)
    {
        try
        {
            Glib::RefPtr<Gio::File> file;
            if (folder)
            {
                file = dialog->select_folder_finish(result);
            }
            else
            {
                file = dialog->open_finish(result);
            }

            if (file != nullptr)
            {
                set_filename(file->get_path());
                selection_changed_.emit();
            }
        }
        catch (Glib::Error const&)
        {
        }
    };

    auto* const parent = dynamic_cast<Gtk::Window*>(widget_.get_root());
    if (parent == nullptr)
    {
        return;
    }

    if (is_folder_action())
    {
        dialog->select_folder(*parent, [this, dialog, on_done](Glib::RefPtr<Gio::AsyncResult>& result) mutable
                                { on_done(result, true); });
    }
    else
    {
        dialog->open(*parent, [this, dialog, on_done](Glib::RefPtr<Gio::AsyncResult>& result) mutable
                     { on_done(result, false); });
    }
}

void PathButton::Impl::update()
{
    if (!current_file_.empty())
    {
        auto const file = Gio::File::create_for_path(current_file_);

        try
        {
            image_->set(file->query_info()->get_icon());
        }
        catch (Glib::Error const&)
        {
            image_->set_from_icon_name("image-missing");
        }

        label_->set_text(file->get_basename());
    }
    else
    {
        image_->set_from_icon_name("image-missing");
        label_->set_text(_("(None)"));
    }

    widget_.set_tooltip_text(current_file_);
}

void PathButton::Impl::update_mode()
{
    mode_->set_from_icon_name(is_folder_action() ? "folder-open-symbolic" : "document-open-symbolic");
}

PathButton::PathButton()
    : Glib::ObjectBase(typeid(PathButton))
    , impl_(std::make_unique<Impl>(*this))
{
}

PathButton::PathButton(BaseObjectType* const cast_item, Glib::RefPtr<Gtk::Builder> const& /*builder*/)
    : Glib::ObjectBase(typeid(PathButton))
    , Gtk::Button(cast_item)
    , impl_(std::make_unique<Impl>(*this))
{
}

PathButton::~PathButton() = default;

void PathButton::set_shortcut_folders(std::list<std::string> const& value)
{
    impl_->set_shortcut_folders(value);
}

std::string PathButton::get_filename() const
{
    return impl_->get_filename();
}

void PathButton::set_filename(std::string const& value)
{
    impl_->set_filename(value);
}

void PathButton::add_filter(Glib::RefPtr<Gtk::FileFilter> const& value)
{
    impl_->add_filter(value);
}

Glib::PropertyProxy<Glib::ustring> PathButton::property_action()
{
    return impl_->property_action().get_proxy();
}

Glib::PropertyProxy<Glib::ustring> PathButton::property_title()
{
    return impl_->property_title().get_proxy();
}

sigc::signal<void()>& PathButton::signal_selection_changed()
{
    return impl_->signal_selection_changed();
}
