// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FileRowItem.h"

#include "GtkCompat.h"
#include "Utils.h"

#include <libtransmission/utils.h>

#include <fmt/format.h>

#include <algorithm>

enum FileRowValue : int
{
    FileValueNotSet = 1000,
    FileValueMixed = 1001,
};

Glib::RefPtr<FileRowItem> FileRowItem::create(
    Glib::RefPtr<Gio::Icon> icon,
    Glib::ustring label,
    Glib::ustring label_esc,
    int const file_index,
    uint64_t const size,
    int const priority,
    int const enabled,
    uint64_t const have,
    int const progress)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new FileRowItem(
        std::move(icon),
        std::move(label),
        std::move(label_esc),
        file_index,
        size,
        priority,
        enabled,
        have,
        progress));
}

FileRowItem::FileRowItem(
    Glib::RefPtr<Gio::Icon> icon,
    Glib::ustring label,
    Glib::ustring label_esc,
    int const file_index,
    uint64_t const size,
    int const priority,
    int const enabled,
    uint64_t const have,
    int const progress)
    : Glib::ObjectBase(typeid(FileRowItem))
    , icon_(std::move(icon))
    , label_(std::move(label))
    , label_esc_(std::move(label_esc))
    , file_index_(file_index)
    , size_(size)
    , size_string_(size > 0 ? Glib::ustring{ tr_strlsize(size) } : Glib::ustring{})
    , have_(have)
    , progress_(std::clamp(progress, 0, 100))
    , progress_string_(fmt::format("{:d}%", progress_))
    , priority_(priority)
    , enabled_(enabled)
{
    if (!is_leaf())
    {
        children_ = Gio::ListStore<FileRowItem>::create();
    }
}

Glib::RefPtr<Gio::ListStore<FileRowItem>> FileRowItem::get_child_store()
{
    if (children_ == nullptr)
    {
        children_ = Gio::ListStore<FileRowItem>::create();
    }

    return children_;
}

Glib::RefPtr<Gio::ListModel> FileRowItem::get_children_model()
{
    if (is_leaf())
    {
        return {};
    }

    return get_child_store();
}

void FileRowItem::set_progress(int const progress)
{
    progress_ = std::clamp(progress, 0, 100);
    progress_string_ = fmt::format("{:d}%", progress_);
}

void FileRowItem::set_size(uint64_t const size)
{
    size_ = size;
    size_string_ = size > 0 ? Glib::ustring{ tr_strlsize(size) } : Glib::ustring{};
}

void FileRowItem::aggregate_from_children()
{
    if (is_leaf() || children_ == nullptr)
    {
        return;
    }

    auto new_enabled = int{ FileValueNotSet };
    auto new_priority = int{ FileValueNotSet };
    auto new_size = uint64_t{ 0 };
    auto new_have = uint64_t{ 0 };

    auto const n_items = children_->get_n_items();
    for (guint i = 0; i < n_items; ++i)
    {
        auto const child = children_->get_item(i);

        if (child->get_enabled() != static_cast<int>(false) && child->get_enabled() != FileValueNotSet)
        {
            new_size += child->get_size();
            new_have += child->get_have();
        }

        if (new_enabled == FileValueNotSet)
        {
            new_enabled = child->get_enabled();
        }
        else if (new_enabled != child->get_enabled())
        {
            new_enabled = FileValueMixed;
        }

        if (new_priority == FileValueNotSet)
        {
            new_priority = child->get_priority();
        }
        else if (new_priority != child->get_priority())
        {
            new_priority = FileValueMixed;
        }
    }

    auto const new_progress = new_size != 0 ? static_cast<int>(100.0 * static_cast<double>(new_have) / static_cast<double>(new_size)) :
                                            0;

    set_enabled(new_enabled);
    set_priority(new_priority);
    set_size(new_size);
    set_have(new_have);
    set_progress(new_progress);
}

void FileRowItem::collect_file_indices(std::vector<tr_file_index_t>& indices) const
{
    if (is_leaf())
    {
        indices.push_back(static_cast<tr_file_index_t>(file_index_));
        return;
    }

    if (children_ == nullptr)
    {
        return;
    }

    auto const n_items = children_->get_n_items();
    for (guint i = 0; i < n_items; ++i)
    {
        children_->get_item(i)->collect_file_indices(indices);
    }
}

FileRowItem* FileRowItem::find_by_path_key(Glib::ustring const& path_key)
{
    if (path_key_ == path_key)
    {
        return this;
    }

    if (children_ == nullptr)
    {
        return nullptr;
    }

    auto const n_items = children_->get_n_items();
    for (guint i = 0; i < n_items; ++i)
    {
        if (auto* const found = children_->get_item(i)->find_by_path_key(path_key); found != nullptr)
        {
            return found;
        }
    }

    return nullptr;
}

int FileRowItem::compare(FileRowItem const& other, FileRowSortColumn const column) const
{
    switch (column)
    {
    case FileRowSortColumn::Label:
        return label_.compare(other.label_);

    case FileRowSortColumn::Size:
        if (size_ < other.size_)
        {
            return -1;
        }

        if (size_ > other.size_)
        {
            return 1;
        }

        return 0;

    case FileRowSortColumn::Progress:
        if (progress_ < other.progress_)
        {
            return -1;
        }

        if (progress_ > other.progress_)
        {
            return 1;
        }

        return 0;

    case FileRowSortColumn::Enabled:
        if (enabled_ < other.enabled_)
        {
            return -1;
        }

        if (enabled_ > other.enabled_)
        {
            return 1;
        }

        return 0;

    case FileRowSortColumn::Priority:
        if (priority_ < other.priority_)
        {
            return -1;
        }

        if (priority_ > other.priority_)
        {
            return 1;
        }

        return 0;
    }

    return 0;
}

void sort_file_row_store(
    Glib::RefPtr<Gio::ListStore<FileRowItem>> const& store,
    FileRowSortColumn const column,
    Gtk::SortType const order)
{
    store->sort(
        [column, order](Glib::RefPtr<FileRowItem const> const& lhs, Glib::RefPtr<FileRowItem const> const& rhs)
        {
            auto const cmp = lhs->compare(*rhs, column);
            return order == Gtk::SortType::ASCENDING ? cmp : -cmp;
        });
}

void sort_file_row_tree(
    Glib::RefPtr<Gio::ListStore<FileRowItem>> const& store,
    FileRowSortColumn const column,
    Gtk::SortType const order)
{
    sort_file_row_store(store, column, order);

    auto const n_items = store->get_n_items();
    for (guint i = 0; i < n_items; ++i)
    {
        if (auto const child_store = store->get_item(i)->get_child_store(); child_store != nullptr)
        {
            sort_file_row_tree(child_store, column, order);
        }
    }
}
