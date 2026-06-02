// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FileList.h"

#include "FileRowItem.h"
#include "GtkCompat.h"
#include "HigWorkarea.h" // GUI_PAD, GUI_PAD_BIG
#include "IconCache.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission/file-utils.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/utils.h>

#include <giomm/icon.h>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/miscutils.h>
#include <glibmm/nodetree.h>
#include <gtkmm/box.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/editablelabel.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/multiselection.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/treeexpander.h>
#include <gtkmm/treelistmodel.h>
#include <gtkmm/treelistrow.h>

#include <fmt/format.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <queue>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

using namespace std::literals;

namespace
{

enum FileRowValue : int
{
    FileValueNotSet = 1000,
    FileValueMixed = 1001,
};

enum class FileColumn : uint8_t
{
    Name,
    Size,
    Progress,
    Download,
    Priority,
};

auto constexpr SizeColumnWidth = 80;
auto constexpr ProgressColumnWidth = 80;
auto constexpr DownloadColumnWidth = 60;
auto constexpr PriorityColumnWidth = 80;

std::optional<double> widget_local_x(Gtk::Widget const& widget, Gtk::Widget const& ancestor, double x, double y)
{
    if (auto const point = widget.compute_point(ancestor, { static_cast<float>(x), static_cast<float>(y) }); point.has_value())
    {
        return point->get_x();
    }

    return {};
}

struct row_struct
{
    uint64_t length = 0;
    Glib::ustring name;
    int index = 0;
};

using FileRowNode = Glib::NodeTree<row_struct>;

Glib::RefPtr<FileRowItem> get_file_row_item(Glib::RefPtr<Glib::ObjectBase> const& object)
{
    return gtr_ptr_dynamic_cast<FileRowItem>(object);
}

Glib::RefPtr<FileRowItem> get_file_row_item(Gtk::TreeListRow& tree_row)
{
    return get_file_row_item(tree_row.get_item());
}

FileColumn get_column_at_x(Gtk::Widget& row, double const x)
{
    double child_x = 0;

    guint child_index = 0;
    for (auto* child = row.get_first_child(); child != nullptr; child = child->get_next_sibling(), ++child_index)
    {
        if (auto const local_x = widget_local_x(*child, row, 0, 0); local_x.has_value())
        {
            child_x = *local_x;
        }

        if (x < child_x + child->get_width())
        {
            switch (child_index)
            {
            case 0:
                return FileColumn::Name;

            case 1:
                return FileColumn::Size;

            case 2:
                return FileColumn::Progress;

            case 3:
                return FileColumn::Download;

            default:
                return FileColumn::Priority;
            }
        }
    }

    return FileColumn::Name;
}

Glib::ustring priority_to_text(int const priority)
{
    switch (priority)
    {
    case TR_PRI_HIGH:
        return _("High");

    case TR_PRI_NORMAL:
        return _("Normal");

    case TR_PRI_LOW:
        return _("Low");

    default:
        return _("Mixed");
    }
}

void assign_path_keys(Glib::RefPtr<Gio::ListStore<FileRowItem>> const& store, Glib::ustring const& prefix)
{
    auto const n_items = store->get_n_items();
    for (guint i = 0; i < n_items; ++i)
    {
        auto const item = store->get_item(i);
        auto const path_key = prefix.empty() ? fmt::format("{:d}", i) : fmt::format("{:s}:{:d}", prefix.raw(), i);
        item->set_path_key(path_key);

        if (auto const child_store = item->get_child_store(); child_store != nullptr && child_store->get_n_items() > 0)
        {
            assign_path_keys(child_store, path_key);
        }
    }
}

void aggregate_tree(Glib::RefPtr<Gio::ListStore<FileRowItem>> const& store)
{
    auto const n_items = store->get_n_items();
    for (guint i = 0; i < n_items; ++i)
    {
        auto const item = store->get_item(i);

        if (auto const child_store = item->get_child_store(); child_store != nullptr && child_store->get_n_items() > 0)
        {
            aggregate_tree(child_store);
            item->aggregate_from_children();
        }
    }
}

struct build_context
{
    Glib::RefPtr<Gio::ListStore<FileRowItem>> store;
    tr_torrent* tor = nullptr;
    tr_variant::Vector const* file_stats = nullptr;
};

void append_from_node(FileRowNode& node, build_context& build, Glib::ustring const& path_prefix)
{
    auto const& child_data = node.data();
    bool const is_leaf = node.child_count() == 0;

    auto const mime_type = is_leaf ? tr_get_mime_type_for_filename(child_data.name.raw()) : DirectoryMimeType;
    auto const icon = gtr_get_mime_type_icon(mime_type);
    auto name_esc = Glib::Markup::escape_text(child_data.name);
    Glib::ustring relative_path;
    if (path_prefix.empty())
    {
        relative_path = child_data.name;
    }
    else
    {
        relative_path = Glib::ustring{ Glib::build_filename(path_prefix, Glib::ustring{ child_data.name }) };
    }

    int priority = is_leaf ? static_cast<int>(TR_PRI_NORMAL) : FileValueNotSet;
    int enabled = is_leaf ? static_cast<int>(true) : FileValueNotSet;
    uint64_t have = 0;
    uint64_t const length = is_leaf ? child_data.length : 0;

    if (is_leaf && build.tor != nullptr)
    {
        auto const file = tr_torrentFile(build.tor, child_data.index);
        priority = static_cast<int>(file.priority);
        enabled = static_cast<int>(file.wanted);
        have = file.have;
    }
    else if (is_leaf && build.file_stats != nullptr && child_data.index >= 0 &&
             static_cast<size_t>(child_data.index) < build.file_stats->size())
    {
        if (auto* const stat = (*build.file_stats)[static_cast<size_t>(child_data.index)].get_if<tr_variant::Map>())
        {
            priority = static_cast<int>(stat->value_if<int64_t>(TR_KEY_priority).value_or(TR_PRI_NORMAL));
            enabled = static_cast<int>(stat->value_if<bool>(TR_KEY_wanted).value_or(true));
            have = static_cast<uint64_t>(stat->value_if<int64_t>(TR_KEY_bytes_completed).value_or(0));
        }
    }

    auto const prog = length > 0 ? static_cast<int>(100.0 * have / length) : 0;

    auto const item = FileRowItem::create(
        icon,
        child_data.name,
        name_esc,
        is_leaf ? child_data.index : -1,
        length,
        priority,
        enabled,
        have,
        prog);
    item->set_relative_path(relative_path);

    build.store->append(item);

    if (!is_leaf)
    {
        auto child_build = build;
        child_build.store = item->get_child_store();

        node.foreach (
            [&child_build, relative_path](auto& child_node)
            { append_from_node(child_node, child_build, relative_path); },
            TR_GLIB_NODE_TREE_TRAVERSE_FLAGS(FileRowNode, ALL));
    }
}

struct PairHash
{
    template<typename T1, typename T2>
    auto operator()(std::pair<T1, T2> const& pair) const
    {
        return std::hash<T1>{}(pair.first) ^ std::hash<T2>{}(pair.second);
    }
};

FileRowNode build_file_tree_from_torrent(tr_torrent* tor)
{
    auto root = FileRowNode{};
    auto& root_data = root.data();
    root_data.name = tr_torrentName(tor);
    root_data.index = -1;
    root_data.length = 0;

    auto nodes = std::unordered_map<std::pair<FileRowNode* /*parent*/, std::string_view>, FileRowNode*, PairHash>{};

    for (tr_file_index_t i = 0, n_files = tr_torrentFileCount(tor); i < n_files; ++i)
    {
        auto* parent = &root;
        auto const file = tr_torrentFile(tor, i);

        auto path = std::string_view{ file.name };
        auto token = std::string_view{};
        while (tr_strv_sep(&path, &token, '/'))
        {
            auto*& node = nodes[std::make_pair(parent, token)];

            if (node == nullptr)
            {
                auto const is_leaf = std::empty(path);

                node = parent->prepend_data({});
                auto& node_data = node->data();
                node_data.name = std::string{ token };
                node_data.index = is_leaf ? static_cast<int>(i) : -1;
                node_data.length = is_leaf ? file.length : 0;
            }

            parent = node;
        }
    }

    return root;
}

FileRowNode build_file_tree_from_rpc(tr_variant::Vector const& files)
{
    auto root = FileRowNode{};
    auto nodes = std::unordered_map<std::pair<FileRowNode* /*parent*/, std::string_view>, FileRowNode*, PairHash>{};

    for (size_t i = 0; i < files.size(); ++i)
    {
        auto* const file_map = files[i].get_if<tr_variant::Map>();
        if (file_map == nullptr)
        {
            continue;
        }

        auto* parent = &root;
        auto path = file_map->value_if<std::string_view>(TR_KEY_name).value_or(""sv);
        auto const length = static_cast<uint64_t>(file_map->value_if<int64_t>(TR_KEY_length).value_or(0));

        auto token = std::string_view{};
        while (tr_strv_sep(&path, &token, '/'))
        {
            auto*& node = nodes[std::make_pair(parent, token)];

            if (node == nullptr)
            {
                auto const is_leaf = std::empty(path);

                node = parent->append_data({});
                auto& node_data = node->data();
                node_data.name = std::string{ token };
                node_data.index = is_leaf ? static_cast<int>(i) : -1;
                node_data.length = is_leaf ? length : 0;
            }

            parent = node;
        }
    }

    return root;
}

void populate_store_from_root(
    FileRowNode& root,
    Glib::RefPtr<Gio::ListStore<FileRowItem>> const& store,
    tr_torrent* tor,
    tr_variant::Vector const* file_stats)
{
    build_context build;
    build.store = store;
    build.tor = tor;
    build.file_stats = file_stats;

    root.foreach (
        [&build](auto& child_node) { append_from_node(child_node, build, {}); },
        TR_GLIB_NODE_TREE_TRAVERSE_FLAGS(FileRowNode, ALL));
}

std::optional<std::string> get_filename_to_open(tr_torrent const* tor, FileRowItem& item)
{
    auto file = Gio::File::create_for_path(Glib::build_filename(
        std::string{ tr_torrentGetCurrentDir(tor) },
        item.get_relative_path().raw()));

    if (item.get_progress() == 100 && file->query_exists())
    {
        return file->get_path();
    }

    for (;;)
    {
        file = file->get_parent();

        if (!file)
        {
            return {};
        }

        if (file->query_exists())
        {
            return file->get_path();
        }
    }
}

void update_rpc_store(Glib::RefPtr<Gio::ListStore<FileRowItem>> const& store, tr_variant::Vector const* file_stats)
{
    auto const n_items = store->get_n_items();
    for (guint i = 0; i < n_items; ++i)
    {
        auto const item = store->get_item(i);

        if (item->is_leaf())
        {
            auto const index = item->get_file_index();
            if (index < 0 || file_stats == nullptr || static_cast<size_t>(index) >= file_stats->size())
            {
                continue;
            }

            auto* const stat = (*file_stats)[static_cast<size_t>(index)].get_if<tr_variant::Map>();
            if (stat == nullptr)
            {
                continue;
            }

            auto const priority = static_cast<int>(stat->value_if<int64_t>(TR_KEY_priority).value_or(TR_PRI_NORMAL));
            auto const enabled = static_cast<int>(stat->value_if<bool>(TR_KEY_wanted).value_or(true));
            auto const have = static_cast<uint64_t>(stat->value_if<int64_t>(TR_KEY_bytes_completed).value_or(0));
            auto const length = item->get_size();
            auto const prog = length > 0 ? static_cast<int>(100.0 * have / length) : 0;

            item->set_priority(priority);
            item->set_enabled(enabled);
            item->set_have(have);
            item->set_progress(prog);
        }
        else if (auto const child_store = item->get_child_store())
        {
            update_rpc_store(child_store, file_stats);
        }
    }
}

} // namespace

class FileList::Impl
{
public:
    Impl(
        FileList& widget,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::ustring const& view_name,
        Glib::RefPtr<Session> const& core,
        tr_torrent_id_t torrent_id);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

    void set_torrent(tr_torrent_id_t torrent_id);
    void load_from_rpc(tr_torrent_id_t torrent_id, tr_variant::Vector const& files, tr_variant::Vector const* file_stats);
    void update_from_rpc(tr_variant::Vector const& files, tr_variant::Vector const* file_stats);
    void reset_torrent();

private:
    void clearData();
    void refresh();
    void rebuild_view_model(bool auto_expand);
    void sort_tree();
    void expand_first_row();

    [[nodiscard]] Glib::RefPtr<FileRowItem> get_item_at_position(guint position) const;
    [[nodiscard]] std::vector<tr_file_index_t> getActiveFilesForPosition(guint position) const;
    [[nodiscard]] std::vector<tr_file_index_t> getSelectedFilesAndDescendants() const;
    [[nodiscard]] std::vector<tr_file_index_t> getSubtree(Glib::RefPtr<FileRowItem> const& item) const;

    bool getAndSelectEventPosition(double view_x, double view_y, guint& position, FileColumn& column);
    bool onViewPathToggled(FileColumn column, guint position);
    void onRowActivated(guint position);
    void on_name_edited(FileRowItem& item, Glib::ustring const& newname);
    void on_rename_done(Glib::ustring const& path_key, Glib::ustring const& newname, int error);
    void on_rename_done_idle(Glib::ustring const& path_key, Glib::ustring const& newname, int error);

    bool onViewButtonPressed(guint button, TrGdkModifierType state, double view_x, double view_y);
    void setup_list_view(bool allow_rename);

private:
    FileList& widget_;

    Glib::RefPtr<Session> const core_;
    Gtk::ListView* view_ = nullptr;
    Glib::RefPtr<Gio::ListStore<FileRowItem>> root_store_;
    Glib::RefPtr<Gtk::TreeListModel> tree_model_;
    Glib::RefPtr<Gtk::MultiSelection> selection_;
    Glib::RefPtr<Gtk::SignalListItemFactory> item_factory_;
    tr_torrent_id_t torrent_id_ = {};
    bool is_rpc_view_ = false;
    size_t rpc_file_count_ = 0;
    bool allow_rename_ = false;
    FileRowSortColumn sort_column_ = FileRowSortColumn::Label;
    Gtk::SortType sort_order_ = TR_GTK_SORT_TYPE(ASCENDING);
    sigc::connection timeout_tag_;
    std::queue<sigc::connection> rename_done_tags_;
};

void FileList::Impl::clearData()
{
    torrent_id_ = -1;
    is_rpc_view_ = false;
    rpc_file_count_ = 0;

    timeout_tag_.disconnect();
}

FileList::Impl::~Impl()
{
    while (!rename_done_tags_.empty())
    {
        rename_done_tags_.front().disconnect();
        rename_done_tags_.pop();
    }

    clearData();
}

void FileList::Impl::sort_tree()
{
    if (root_store_ != nullptr)
    {
        sort_file_row_tree(root_store_, sort_column_, sort_order_);
    }
}

void FileList::Impl::rebuild_view_model(bool const auto_expand)
{
    tree_model_ = Gtk::TreeListModel::create(
        root_store_,
        [](Glib::RefPtr<Glib::ObjectBase> const& item) { return get_file_row_item(item)->get_children_model(); },
        false,
        auto_expand);

    selection_ = Gtk::MultiSelection::create(tree_model_);
    view_->set_model(selection_);
}

void FileList::Impl::expand_first_row()
{
    if (tree_model_ != nullptr && tree_model_->get_n_items() > 0)
    {
        if (auto const row = tree_model_->get_row(0))
        {
            row->set_expanded(true);
        }
    }
}

namespace
{

void refresh_item_from_torrent(FileRowItem& item, tr_torrent* tor)
{
    if (item.is_leaf())
    {
        auto const file = tr_torrentFile(tor, item.get_file_index());
        item.set_enabled(static_cast<int>(file.wanted));
        item.set_priority(file.priority);
        item.set_have(file.have);
        item.set_size(file.length);
        item.set_progress(static_cast<int>(100 * file.progress));
        return;
    }

    if (auto const child_store = item.get_child_store())
    {
        auto const n_items = child_store->get_n_items();
        for (guint i = 0; i < n_items; ++i)
        {
            refresh_item_from_torrent(*child_store->get_item(i), tor);
        }

        item.aggregate_from_children();
    }
}

void refresh_tree(Glib::RefPtr<Gio::ListStore<FileRowItem>> const& store, tr_torrent* tor, bool& resort_needed, FileRowSortColumn const sort_column)
{
    auto const n_items = store->get_n_items();
    for (guint i = 0; i < n_items; ++i)
    {
        auto const item = store->get_item(i);
        auto const old_priority = item->get_priority();
        auto const old_enabled = item->get_enabled();
        refresh_item_from_torrent(*item, tor);

        if (!resort_needed &&
            ((sort_column == FileRowSortColumn::Priority && item->get_priority() != old_priority) ||
             (sort_column == FileRowSortColumn::Enabled && item->get_enabled() != old_enabled)))
        {
            resort_needed = true;
        }

        if (auto const child_store = item->get_child_store(); child_store != nullptr && child_store->get_n_items() > 0)
        {
            refresh_tree(child_store, tor, resort_needed, sort_column);
        }
    }
}

} // namespace

void FileList::Impl::refresh()
{
    if (tr_torrent* tor = core_->find_torrent(torrent_id_); tor == nullptr)
    {
        widget_.clear();
    }
    else
    {
        bool resort_needed = false;
        refresh_tree(root_store_, tor, resort_needed, sort_column_);

        if (resort_needed)
        {
            sort_tree();
        }
    }
}

Glib::RefPtr<FileRowItem> FileList::Impl::get_item_at_position(guint const position) const
{
    if (tree_model_ == nullptr)
    {
        return {};
    }

    if (auto const row = tree_model_->get_row(position))
    {
        return get_file_row_item(*row);
    }

    return {};
}

std::vector<tr_file_index_t> FileList::Impl::getSubtree(Glib::RefPtr<FileRowItem> const& item) const
{
    std::vector<tr_file_index_t> indices;
    if (item != nullptr)
    {
        item->collect_file_indices(indices);
    }

    return indices;
}

std::vector<tr_file_index_t> FileList::Impl::getSelectedFilesAndDescendants() const
{
    std::vector<tr_file_index_t> indices;

    if (selection_ == nullptr)
    {
        return indices;
    }

    auto const selected_items = selection_->get_selection();
    for (auto const position : *selected_items)
    {
        if (auto const item = get_item_at_position(position); item != nullptr)
        {
            auto sub = getSubtree(item);
            indices.insert(indices.end(), sub.begin(), sub.end());
        }
    }

    std::ranges::sort(indices);
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

std::vector<tr_file_index_t> FileList::Impl::getActiveFilesForPosition(guint const position) const
{
    if (selection_ != nullptr && selection_->is_selected(position))
    {
        return getSelectedFilesAndDescendants();
    }

    return getSubtree(get_item_at_position(position));
}

void FileList::clear()
{
    impl_->reset_torrent();
}

void FileList::set_torrent(tr_torrent_id_t const torrent_id)
{
    impl_->set_torrent(torrent_id);
}

void FileList::load_from_rpc(
    tr_torrent_id_t const torrent_id,
    tr_variant::Vector const& files,
    tr_variant::Vector const* const file_stats)
{
    impl_->load_from_rpc(torrent_id, files, file_stats);
}

void FileList::Impl::set_torrent(tr_torrent_id_t const torrent_id)
{
    if (torrent_id_ == torrent_id && root_store_ != nullptr && root_store_->get_n_items() > 0)
    {
        return;
    }

    clearData();

    root_store_ = Gio::ListStore<FileRowItem>::create();
    torrent_id_ = torrent_id;
    is_rpc_view_ = false;
    allow_rename_ = !core_->is_remote();

    if (torrent_id_ > 0)
    {
        if (auto* const tor = core_->find_torrent(torrent_id_); tor != nullptr)
        {
            auto root = build_file_tree_from_torrent(tor);
            populate_store_from_root(root, root_store_, tor, nullptr);
            assign_path_keys(root_store_, {});
            aggregate_tree(root_store_);
        }

        sort_column_ = FileRowSortColumn::Label;
        sort_order_ = TR_GTK_SORT_TYPE(ASCENDING);
        sort_tree();

        refresh();
        timeout_tag_ = Glib::signal_timeout().connect_seconds(
            [this]() { return refresh(), true; },
            SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);
    }

    rebuild_view_model(false);
    expand_first_row();
}

void FileList::Impl::reset_torrent()
{
    clearData();
    root_store_ = Gio::ListStore<FileRowItem>::create();
    rebuild_view_model(false);
}

void FileList::Impl::update_from_rpc(tr_variant::Vector const& /*files*/, tr_variant::Vector const* const file_stats)
{
    if (!is_rpc_view_ || root_store_ == nullptr)
    {
        return;
    }

    update_rpc_store(root_store_, file_stats);
    aggregate_tree(root_store_);
}

void FileList::Impl::load_from_rpc(
    tr_torrent_id_t const torrent_id,
    tr_variant::Vector const& files,
    tr_variant::Vector const* const file_stats)
{
    if (is_rpc_view_ && torrent_id_ == torrent_id && root_store_ != nullptr && root_store_->get_n_items() > 0 &&
        files.size() == rpc_file_count_)
    {
        update_from_rpc(files, file_stats);
        return;
    }

    clearData();

    root_store_ = Gio::ListStore<FileRowItem>::create();
    torrent_id_ = torrent_id;
    is_rpc_view_ = true;
    rpc_file_count_ = files.size();
    allow_rename_ = false;

    auto root = build_file_tree_from_rpc(files);
    populate_store_from_root(root, root_store_, nullptr, file_stats);
    assign_path_keys(root_store_, {});
    aggregate_tree(root_store_);

    sort_column_ = FileRowSortColumn::Label;
    sort_order_ = TR_GTK_SORT_TYPE(ASCENDING);
    sort_tree();

    rebuild_view_model(true);
}

void FileList::Impl::onRowActivated(guint const position)
{
    auto const* const tor = core_->find_torrent(torrent_id_);
    if (tor == nullptr)
    {
        return;
    }

    auto const item = get_item_at_position(position);
    if (item == nullptr || !item->is_leaf())
    {
        return;
    }

    if (auto const filename = get_filename_to_open(tor, *item); filename)
    {
        gtr_open_file(*filename);
    }
}

bool FileList::Impl::onViewPathToggled(FileColumn const column, guint const position)
{
    if (column != FileColumn::Download && column != FileColumn::Priority)
    {
        return false;
    }

    auto const index_buf = getActiveFilesForPosition(position);
    if (index_buf.empty())
    {
        return false;
    }

    auto const item = get_item_at_position(position);
    if (item == nullptr)
    {
        return false;
    }

    if (is_rpc_view_)
    {
        auto indices = tr_variant::Vector{};
        indices.reserve(index_buf.size());
        for (auto const idx : index_buf)
        {
            indices.emplace_back(static_cast<int64_t>(idx));
        }

        auto params = tr_variant::Map{ 2U };
        params[TR_KEY_ids] = Session::to_variant(std::vector<tr_torrent_id_t>{ torrent_id_ });

        if (column == FileColumn::Priority)
        {
            auto const old_priority = item->get_priority();
            int new_priority = TR_PRI_NORMAL;
            tr_quark rpc_key = TR_KEY_priority_normal;

            switch (old_priority)
            {
            case TR_PRI_NORMAL:
                new_priority = TR_PRI_HIGH;
                rpc_key = TR_KEY_priority_high;
                break;

            case TR_PRI_HIGH:
                new_priority = TR_PRI_LOW;
                rpc_key = TR_KEY_priority_low;
                break;

            default:
                new_priority = TR_PRI_NORMAL;
                rpc_key = TR_KEY_priority_normal;
                break;
            }

            item->set_priority(new_priority);
            params[rpc_key] = std::move(indices);
        }
        else
        {
            auto const old_enabled = item->get_enabled();
            bool const new_enabled = old_enabled == static_cast<int>(false);
            item->set_enabled(static_cast<int>(new_enabled));
            params[new_enabled ? TR_KEY_files_wanted : TR_KEY_files_unwanted] = std::move(indices);
        }

        core_->exec(TR_KEY_torrent_set, std::move(params));
        return true;
    }

    auto* tor = core_->find_torrent(torrent_id_);
    if (tor == nullptr)
    {
        return false;
    }

    if (column == FileColumn::Priority)
    {
        auto const old_priority = item->get_priority();
        auto new_priority = TR_PRI_NORMAL;

        switch (old_priority)
        {
        case TR_PRI_NORMAL:
            new_priority = TR_PRI_HIGH;
            break;

        case TR_PRI_HIGH:
            new_priority = TR_PRI_LOW;
            break;

        default:
            new_priority = TR_PRI_NORMAL;
            break;
        }

        tr_torrentSetFilePriorities(tor, index_buf.data(), index_buf.size(), new_priority);
    }
    else
    {
        auto const enabled = item->get_enabled();
        tr_torrentSetFileDLs(tor, index_buf.data(), index_buf.size(), enabled == static_cast<int>(false));
    }

    refresh();
    return true;
}

bool FileList::Impl::getAndSelectEventPosition(double view_x, double view_y, guint& position, FileColumn& column)
{
    auto* child = view_->pick(view_x, view_y);
    while (child != nullptr && child->get_css_name() != "row")
    {
        child = child->get_parent();
    }

    if (child == nullptr)
    {
        return false;
    }

    double row_x = 0;
    double row_y = 0;
    if (auto const point = child->compute_point(*view_, { 0.0F, 0.0F }); point.has_value())
    {
        row_x = point->get_x();
        row_y = point->get_y();
    }

    auto const row_height = child->get_height();
    if (row_height <= 0)
    {
        return false;
    }

    position = static_cast<guint>((row_y + view_->get_vadjustment()->get_value()) / row_height);
    column = get_column_at_x(*child, view_x - row_x);

    if (selection_ != nullptr && !selection_->is_selected(position))
    {
        selection_->unselect_all();
        selection_->select_item(position, true);
    }

    return true;
}

bool FileList::Impl::onViewButtonPressed(guint const button, TrGdkModifierType const state, double const view_x, double const view_y)
{
    guint position = 0;
    FileColumn column = FileColumn::Name;

    if (button == GDK_BUTTON_PRIMARY &&
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        (state & (TR_GDK_MODIFIED_TYPE(SHIFT_MASK) | TR_GDK_MODIFIED_TYPE(CONTROL_MASK))) == TrGdkModifierType{} &&
        getAndSelectEventPosition(view_x, view_y, position, column))
    {
        return onViewPathToggled(column, position);
    }

    return false;
}

void FileList::Impl::on_rename_done(Glib::ustring const& path_key, Glib::ustring const& newname, int const error)
{
    rename_done_tags_.push(
        Glib::signal_idle().connect(
            [this, path_key, newname, error]()
            {
                rename_done_tags_.pop();
                on_rename_done_idle(path_key, newname, error);
                return false;
            }));
}

void FileList::Impl::on_rename_done_idle(Glib::ustring const& path_key, Glib::ustring const& newname, int const error)
{
    if (error == 0)
    {
        if (root_store_ != nullptr)
        {
            auto const n_items = root_store_->get_n_items();
            for (guint i = 0; i < n_items; ++i)
            {
                if (auto* const item = root_store_->get_item(i)->find_by_path_key(path_key); item != nullptr)
                {
                    auto const mime_type = item->is_leaf() ? tr_get_mime_type_for_filename(newname.raw()) : DirectoryMimeType;
                    item->set_label(newname);
                    item->set_icon(gtr_get_mime_type_icon(mime_type));

                    if (path_key.find(':') == Glib::ustring::npos)
                    {
                        core_->torrent_changed(torrent_id_);
                    }

                    break;
                }
            }
        }
    }
    else
    {
        auto dialog = Gtk::AlertDialog::create(fmt::format(
            fmt::runtime(_("Couldn't rename '{old_path}' as '{path}': {error} ({error_code})")),
            fmt::arg("old_path", path_key),
            fmt::arg("path", newname),
            fmt::arg("error", tr_strerror(error)),
            fmt::arg("error_code", error)));
        dialog->set_detail(_("Please correct the errors and try again."));
        dialog->show(gtr_widget_get_window(widget_));
    }
}

void FileList::Impl::on_name_edited(FileRowItem& item, Glib::ustring const& newname)
{
    tr_torrent* const tor = core_->find_torrent(torrent_id_);

    if (tor == nullptr)
    {
        return;
    }

    tr_torrentRenamePath(
        tor,
        item.get_relative_path().raw(),
        newname.raw(),
        [this, path_key = item.get_path_key(), newname](
            tr_torrent_id_t const /*tor_id*/,
            std::string_view const /*oldpath*/,
            std::string_view const /*newname*/,
            tr_error const& error) { on_rename_done(path_key, newname, error ? error.code() : 0); });
}

void FileList::Impl::setup_list_view(bool const allow_rename)
{
    static auto const RowBoxKey = Glib::Quark("tr-file-list-row-box");
    static auto const ExpanderKey = Glib::Quark("tr-file-list-expander");
    static auto const NameLabelKey = Glib::Quark("tr-file-list-name-label");
    static auto const SizeLabelKey = Glib::Quark("tr-file-list-size-label");
    static auto const ProgressBarKey = Glib::Quark("tr-file-list-progress-bar");
    static auto const DownloadCheckKey = Glib::Quark("tr-file-list-download-check");
    static auto const PriorityLabelKey = Glib::Quark("tr-file-list-priority-label");

    item_factory_ = Gtk::SignalListItemFactory::create();

    item_factory_->signal_setup().connect(
        [this, allow_rename](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto* const row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, GUI_PAD_SMALL);
            row->set_hexpand(true);

            auto* const expander = Gtk::make_managed<Gtk::TreeExpander>();
            auto* const name_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, GUI_PAD_SMALL);
            name_box->set_hexpand(true);

            auto* const icon = Gtk::make_managed<Gtk::Image>();
            icon->property_icon_size() = Gtk::IconSize::NORMAL;

            Gtk::Widget* name_widget = nullptr;
            if (allow_rename)
            {
                auto* const name_label = Gtk::make_managed<Gtk::EditableLabel>();
                name_label->property_width_chars() = 20;
                name_label->property_editing().signal_changed().connect(
                    [this, list_item, name_label]()
                    {
                        if (name_label->get_editing())
                        {
                            return;
                        }

                        auto const tree_row = gtr_ptr_dynamic_cast<Gtk::TreeListRow>(list_item->get_item());
                        if (tree_row == nullptr)
                        {
                            return;
                        }

                        if (auto const row_item = get_file_row_item(*tree_row); row_item != nullptr)
                        {
                            if (auto const newname = name_label->get_text(); newname != row_item->get_label())
                            {
                                on_name_edited(*row_item, newname);
                            }
                        }
                    });
                name_widget = name_label;
                list_item->set_data(NameLabelKey, name_label);
            }
            else
            {
                auto* const name_label = Gtk::make_managed<Gtk::Label>();
                name_label->property_xalign() = 0;
                name_label->property_ellipsize() = TR_PANGO_ELLIPSIZE_MODE(END);
                name_label->property_width_chars() = 20;
                name_widget = name_label;
                list_item->set_data(NameLabelKey, name_label);
            }

            name_box->append(*icon);
            name_box->append(*name_widget);
            expander->set_child(*name_box);

            auto* const size_label = Gtk::make_managed<Gtk::Label>();
            size_label->property_xalign() = 1;
            size_label->set_width_chars(8);
            size_label->set_size_request(SizeColumnWidth, -1);

            auto* const progress_bar = Gtk::make_managed<Gtk::ProgressBar>();
            progress_bar->set_size_request(ProgressColumnWidth, -1);

            auto* const download_check = Gtk::make_managed<Gtk::CheckButton>();
            download_check->set_halign(Gtk::Align::CENTER);
            download_check->set_size_request(DownloadColumnWidth, -1);

            auto* const priority_label = Gtk::make_managed<Gtk::Label>();
            priority_label->property_xalign() = 0.5;
            priority_label->set_size_request(PriorityColumnWidth, -1);

            row->append(*expander);
            row->append(*size_label);
            row->append(*progress_bar);
            row->append(*download_check);
            row->append(*priority_label);

            list_item->set_data(RowBoxKey, row);
            list_item->set_data(ExpanderKey, expander);
            list_item->set_data(SizeLabelKey, size_label);
            list_item->set_data(ProgressBarKey, progress_bar);
            list_item->set_data(DownloadCheckKey, download_check);
            list_item->set_data(PriorityLabelKey, priority_label);
            list_item->set_child(*row);
        });

    item_factory_->signal_bind().connect(
        [this, allow_rename](Glib::RefPtr<Gtk::ListItem> const& list_item)
        {
            auto const tree_row = gtr_ptr_dynamic_cast<Gtk::TreeListRow>(list_item->get_item());
            if (tree_row == nullptr)
            {
                return;
            }

            auto const item = get_file_row_item(*tree_row);
            if (item == nullptr)
            {
                return;
            }

            auto* const row = static_cast<Gtk::Box*>(list_item->get_data(RowBoxKey));
            auto* const expander = static_cast<Gtk::TreeExpander*>(list_item->get_data(ExpanderKey));
            auto* const size_label = static_cast<Gtk::Label*>(list_item->get_data(SizeLabelKey));
            auto* const progress_bar = static_cast<Gtk::ProgressBar*>(list_item->get_data(ProgressBarKey));
            auto* const download_check = static_cast<Gtk::CheckButton*>(list_item->get_data(DownloadCheckKey));
            auto* const priority_label = static_cast<Gtk::Label*>(list_item->get_data(PriorityLabelKey));
            auto* const name_widget = static_cast<Gtk::Widget*>(list_item->get_data(NameLabelKey));

            if (row == nullptr || expander == nullptr || size_label == nullptr || progress_bar == nullptr ||
                download_check == nullptr || priority_label == nullptr || name_widget == nullptr)
            {
                return;
            }

            expander->set_list_row(tree_row);

            if (auto* const icon = dynamic_cast<Gtk::Image*>(expander->get_child()->get_first_child()); icon != nullptr)
            {
                icon->property_gicon() = item->get_icon();
            }

            if (allow_rename)
            {
                auto* const name_label = dynamic_cast<Gtk::EditableLabel*>(name_widget);
                name_label->set_text(item->get_label());
            }
            else if (auto* const name_label = dynamic_cast<Gtk::Label*>(name_widget))
            {
                name_label->set_label(item->get_label());
            }

            size_label->set_label(item->get_size_string());
            progress_bar->set_fraction(static_cast<double>(item->get_progress()) / 100.0);
            progress_bar->set_text(item->get_progress_string());

            download_check->set_inconsistent(item->get_enabled() == FileValueMixed);
            download_check->set_active(item->get_enabled() == static_cast<int>(true));
            priority_label->set_label(priority_to_text(item->get_priority()));

            row->set_tooltip_text(item->get_label_esc());
        });

    view_->set_factory(item_factory_);
    view_->signal_activate().connect([this](guint position) { onRowActivated(position); });
}

FileList::FileList(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::ustring const& view_name,
    Glib::RefPtr<Session> const& core,
    tr_torrent_id_t torrent_id)
    : Gtk::ScrolledWindow(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, view_name, core, torrent_id))
{
}

FileList::Impl::Impl(
    FileList& widget,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::ustring const& view_name,
    Glib::RefPtr<Session> const& core,
    tr_torrent_id_t torrent_id)
    : widget_(widget)
    , core_(core)
    , view_(gtr_get_widget<Gtk::ListView>(builder, view_name))
{
    allow_rename_ = !core_->is_remote();
    setup_list_view(allow_rename_);

    setup_item_view_button_event_handling(
        *view_,
        [this](guint button, TrGdkModifierType state, double view_x, double view_y, bool /*context_menu_requested*/)
        { return onViewButtonPressed(button, state, view_x, view_y); },
        [this](double view_x, double view_y) { return on_item_view_button_released(*view_, view_x, view_y); });

    set_torrent(torrent_id);
}

FileList::~FileList() = default;
