// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "TorrentFilter.h"

#include <libtransmission-app/display-modes.h>

#include <gdkmm/pixbuf.h>
#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

class FilterShowModeRow : public Glib::Object
{
public:
    static Glib::RefPtr<FilterShowModeRow> create(
        Glib::ustring name,
        int count,
        tr::app::ShowMode show_mode,
        Glib::ustring icon_name);

    static Glib::RefPtr<FilterShowModeRow> create_separator();

    [[nodiscard]] Glib::ustring const& get_name() const noexcept
    {
        return name_;
    }

    [[nodiscard]] int get_count() const noexcept
    {
        return count_;
    }

    void set_count(int count)
    {
        count_ = count;
    }

    [[nodiscard]] tr::app::ShowMode get_show_mode() const noexcept
    {
        return show_mode_;
    }

    [[nodiscard]] Glib::ustring const& get_icon_name() const noexcept
    {
        return icon_name_;
    }

    [[nodiscard]] bool is_separator() const noexcept
    {
        return is_separator_;
    }

private:
    FilterShowModeRow(
        Glib::ustring name,
        int count,
        tr::app::ShowMode show_mode,
        Glib::ustring icon_name,
        bool is_separator);

    Glib::ustring name_;
    int count_ = 0;
    tr::app::ShowMode show_mode_;
    Glib::ustring icon_name_;
    bool is_separator_ = false;
};

class FilterTrackerRow : public Glib::Object
{
public:
    using TrackerType = TorrentFilter::Tracker;

    static Glib::RefPtr<FilterTrackerRow> create(
        Glib::ustring displayname,
        int count,
        TrackerType type,
        Glib::ustring sitename);

    static Glib::RefPtr<FilterTrackerRow> create_separator();

    [[nodiscard]] Glib::ustring const& get_displayname() const noexcept
    {
        return displayname_;
    }

    [[nodiscard]] int get_count() const noexcept
    {
        return count_;
    }

    void set_count(int count)
    {
        count_ = count;
    }

    [[nodiscard]] TrackerType get_type() const noexcept
    {
        return type_;
    }

    [[nodiscard]] Glib::ustring const& get_sitename() const noexcept
    {
        return sitename_;
    }

    [[nodiscard]] Glib::RefPtr<Gdk::Pixbuf> const& get_pixbuf() const noexcept
    {
        return pixbuf_;
    }

    void set_pixbuf(Glib::RefPtr<Gdk::Pixbuf> const& pixbuf)
    {
        pixbuf_ = pixbuf;
    }

    [[nodiscard]] bool is_separator() const noexcept
    {
        return is_separator_;
    }

private:
    FilterTrackerRow(
        Glib::ustring displayname,
        int count,
        TrackerType type,
        Glib::ustring sitename,
        bool is_separator);

    Glib::ustring displayname_;
    int count_ = 0;
    TrackerType type_;
    Glib::ustring sitename_;
    Glib::RefPtr<Gdk::Pixbuf> pixbuf_;
    bool is_separator_ = false;
};
