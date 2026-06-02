// This file Copyright © Transmission authors and contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include <glibmm/propertyproxy.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/builder.h>
#include <gtkmm/button.h>
#include <gtkmm/filefilter.h>

#include <list>
#include <memory>
#include <string>

class PathButton : public Gtk::Button
{
public:
    PathButton();
    PathButton(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder);
    PathButton(PathButton&&) = delete;
    PathButton(PathButton const&) = delete;
    PathButton& operator=(PathButton&&) = delete;
    PathButton& operator=(PathButton const&) = delete;
    ~PathButton() override;

    void set_shortcut_folders(std::list<std::string> const& value);

    [[nodiscard]] std::string get_filename() const;
    void set_filename(std::string const& value);

    void add_filter(Glib::RefPtr<Gtk::FileFilter> const& value);

    Glib::PropertyProxy<Glib::ustring> property_action();
    Glib::PropertyProxy<Glib::ustring> property_title();

    sigc::signal<void()>& signal_selection_changed();

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
