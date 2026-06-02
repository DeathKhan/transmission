// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>

#include "GtkCompat.h"

#include <giomm/icon.h>
#include <giomm/liststore.h>
#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/enums.h>

#include <cstdint>
#include <vector>

enum class FileRowSortColumn : uint8_t
{
    Label,
    Size,
    Progress,
    Enabled,
    Priority,
};

class FileRowItem : public Glib::Object
{
public:
    static Glib::RefPtr<FileRowItem> create(
        Glib::RefPtr<Gio::Icon> icon,
        Glib::ustring label,
        Glib::ustring label_esc,
        int file_index,
        uint64_t size,
        int priority,
        int enabled,
        uint64_t have,
        int progress);

    [[nodiscard]] bool is_leaf() const noexcept
    {
        return file_index_ >= 0;
    }

    [[nodiscard]] Glib::RefPtr<Gio::ListStore<FileRowItem>> get_child_store();
    [[nodiscard]] Glib::RefPtr<Gio::ListModel> get_children_model();

    void aggregate_from_children();
    void collect_file_indices(std::vector<tr_file_index_t>& indices) const;

    [[nodiscard]] Glib::RefPtr<Gio::Icon> const& get_icon() const noexcept
    {
        return icon_;
    }

    void set_icon(Glib::RefPtr<Gio::Icon> icon)
    {
        icon_ = std::move(icon);
    }

    [[nodiscard]] Glib::ustring const& get_label() const noexcept
    {
        return label_;
    }

    void set_label(Glib::ustring label)
    {
        label_ = std::move(label);
    }

    [[nodiscard]] Glib::ustring const& get_label_esc() const noexcept
    {
        return label_esc_;
    }

    void set_label_esc(Glib::ustring label_esc)
    {
        label_esc_ = std::move(label_esc);
    }

    [[nodiscard]] int get_progress() const noexcept
    {
        return progress_;
    }

    void set_progress(int progress);

    [[nodiscard]] Glib::ustring const& get_progress_string() const noexcept
    {
        return progress_string_;
    }

    [[nodiscard]] int get_file_index() const noexcept
    {
        return file_index_;
    }

    [[nodiscard]] uint64_t get_size() const noexcept
    {
        return size_;
    }

    void set_size(uint64_t size);

    [[nodiscard]] Glib::ustring const& get_size_string() const noexcept
    {
        return size_string_;
    }

    [[nodiscard]] uint64_t get_have() const noexcept
    {
        return have_;
    }

    void set_have(uint64_t have)
    {
        have_ = have;
    }

    [[nodiscard]] int get_priority() const noexcept
    {
        return priority_;
    }

    void set_priority(int priority)
    {
        priority_ = priority;
    }

    [[nodiscard]] int get_enabled() const noexcept
    {
        return enabled_;
    }

    void set_enabled(int enabled)
    {
        enabled_ = enabled;
    }

    [[nodiscard]] Glib::ustring const& get_path_key() const noexcept
    {
        return path_key_;
    }

    void set_path_key(Glib::ustring path_key)
    {
        path_key_ = std::move(path_key);
    }

    [[nodiscard]] Glib::ustring const& get_relative_path() const noexcept
    {
        return relative_path_;
    }

    void set_relative_path(Glib::ustring relative_path)
    {
        relative_path_ = std::move(relative_path);
    }

    [[nodiscard]] FileRowItem* find_by_path_key(Glib::ustring const& path_key);

    [[nodiscard]] int compare(FileRowItem const& other, FileRowSortColumn column) const;

private:
    FileRowItem(
        Glib::RefPtr<Gio::Icon> icon,
        Glib::ustring label,
        Glib::ustring label_esc,
        int file_index,
        uint64_t size,
        int priority,
        int enabled,
        uint64_t have,
        int progress);

    Glib::RefPtr<Gio::Icon> icon_;
    Glib::ustring label_;
    Glib::ustring label_esc_;
    int file_index_;
    uint64_t size_;
    Glib::ustring size_string_;
    uint64_t have_;
    int progress_;
    Glib::ustring progress_string_;
    int priority_;
    int enabled_;
    Glib::ustring path_key_;
    Glib::ustring relative_path_;
    Glib::RefPtr<Gio::ListStore<FileRowItem>> children_;
};

void sort_file_row_store(
    Glib::RefPtr<Gio::ListStore<FileRowItem>> const& store,
    FileRowSortColumn column,
    Gtk::SortType order);

void sort_file_row_tree(Glib::RefPtr<Gio::ListStore<FileRowItem>> const& store, FileRowSortColumn column, Gtk::SortType order);
