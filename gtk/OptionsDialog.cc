// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "OptionsDialog.h"

#include "FileList.h"
#include "FreeSpaceLabel.h"
#include "GtkCompat.h"
#include "PathButton.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "Utils.h"

#include <libtransmission/transmission.h>
#include <libtransmission/file.h> /* tr_sys_path_is_same() */

#include <giomm/file.h>
#include <giomm/listmodel.h>
#include <giomm/liststore.h>
#include <glibmm/i18n.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>

#include <memory>
#include <vector>
#include <utility>

using namespace std::literals;

/****
*****
****/

namespace
{

auto const ShowOptionsDialogChoice = "show_options_dialog"sv; // TODO(C++20): Use ""s

} // namespace

/****
*****
****/

class OptionsDialog::Impl
{
public:
    Impl(
        OptionsDialog& dialog,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Glib::RefPtr<Session> const& core,
        std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor);
    Impl(Impl&&) = delete;
    Impl(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    ~Impl();

private:
    void sourceChanged(PathButton* b);
    void downloadDirChanged(PathButton* b);

    void removeOldTorrent();
    void updateTorrent();

    void addResponseCB(int response);

private:
    OptionsDialog& dialog_;
    Glib::RefPtr<Session> const core_;
    std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor_;

    std::string filename_;
    std::string downloadDir_;
    tr_torrent* tor_ = nullptr;

    FileList* file_list_ = nullptr;
    Gtk::CheckButton* run_check_ = nullptr;
    Gtk::CheckButton* trash_check_ = nullptr;
    Gtk::DropDown* priority_combo_ = nullptr;
    FreeSpaceLabel* freespace_label_ = nullptr;
};

OptionsDialog::Impl::~Impl()
{
    removeOldTorrent();
}

void OptionsDialog::Impl::removeOldTorrent()
{
    if (tor_ != nullptr)
    {
        file_list_->clear();
        tr_torrentRemove(tor_, false);
        tor_ = nullptr;
    }
}

void OptionsDialog::Impl::addResponseCB(int response)
{
    if (response == TR_GTK_RESPONSE_TYPE(ACCEPT))
    {
        tr_ctorSetPaused(ctor_.get(), TR_FORCE, !run_check_->get_active());
        tr_ctorSetDownloadDir(ctor_.get(), TR_FORCE, downloadDir_.c_str());

        if (core_->is_remote())
        {
            core_->add_torrent_from_ctor(
                ctor_.get(),
                run_check_->get_active(),
                trash_check_->get_active(),
                static_cast<tr_priority_t>(gtr_combo_box_get_active_enum(*priority_combo_)));
            gtr_save_recent_dir("download", core_, downloadDir_);
            dialog_.close();
            return;
        }

        if (tor_ != nullptr)
        {
            tr_torrentSetPriority(tor_, static_cast<tr_priority_t>(gtr_combo_box_get_active_enum(*priority_combo_)));

            if (run_check_->get_active())
            {
                tr_torrentStart(tor_);
            }

            core_->add_torrent(Torrent::create(tor_), false);

            if (trash_check_->get_active())
            {
                gtr_file_trash_or_remove(filename_, nullptr);
            }

            gtr_save_recent_dir("download", core_, downloadDir_);
            tor_ = nullptr;
        }
    }

    dialog_.close();
}

void OptionsDialog::Impl::updateTorrent()
{
    bool const isLocalFile = tr_ctorGetSourceFile(ctor_.get()).has_value();
    trash_check_->set_sensitive(isLocalFile);

    if (core_->is_remote())
    {
        file_list_->clear();
        file_list_->set_sensitive(false);
        return;
    }

    if (tor_ == nullptr)
    {
        file_list_->clear();
        file_list_->set_sensitive(false);
    }
    else
    {
        tr_torrentSetDownloadDir(tor_, downloadDir_);
        file_list_->set_sensitive(tr_torrentHasMetadata(tor_));
        file_list_->set_torrent(tr_torrentId(tor_));
        tr_torrentVerify(tor_);
    }
}

/**
 * When the source torrent file is deleted
 * (such as, if it was a temp file that a web browser passed to us),
 * gtk invokes this callback and `filename' will be nullptr.
 * The `filename' tests here are to prevent us from losing the current
 * metadata when that happens.
 */
void OptionsDialog::Impl::sourceChanged(PathButton* b)
{
    auto const filename = b->get_filename();

    /* maybe instantiate a torrent */
    if (!filename.empty() || tor_ == nullptr)
    {
        bool new_file = false;

        if (!filename.empty() && (filename_.empty() || !tr_sys_path_is_same(filename, filename_)))
        {
            filename_ = filename;
            tr_ctorSetMetainfoFromFile(ctor_.get(), filename_);
            new_file = true;
        }

        tr_ctorSetDownloadDir(ctor_.get(), TR_FORCE, downloadDir_);
        tr_ctorSetPaused(ctor_.get(), TR_FORCE, true);
        tr_ctorSetDeleteSource(ctor_.get(), false);

        if (core_->is_remote())
        {
            updateTorrent();
            return;
        }

        tr_torrent* duplicate_of = nullptr;
        if (tr_torrent* const torrent = tr_torrentNew(ctor_.get(), &duplicate_of); torrent != nullptr)
        {
            removeOldTorrent();
            tor_ = torrent;
        }
        else if (new_file)
        {
            gtr_add_torrent_error_dialog(*b, duplicate_of, filename_);
        }

        updateTorrent();
    }
}

void OptionsDialog::Impl::downloadDirChanged(PathButton* b)
{
    auto const fname = b->get_filename();

    if (!fname.empty() && (downloadDir_.empty() || !tr_sys_path_is_same(fname, downloadDir_)))
    {
        downloadDir_ = fname;
        updateTorrent();

        freespace_label_->set_dir(downloadDir_);
    }
}

namespace
{

void addTorrentFilters(PathButton* chooser)
{
    auto filter = Gtk::FileFilter::create();
    filter->set_name(_("Torrent files"));
    filter->add_pattern("*.torrent");
    chooser->add_filter(filter);

    filter = Gtk::FileFilter::create();
    filter->set_name(_("All files"));
    filter->add_pattern("*");
    chooser->add_filter(filter);
}

void apply_torrent_filters_to_dialog(Glib::RefPtr<Gtk::FileDialog> const& dialog)
{
    auto const torrent_filter = Gtk::FileFilter::create();
    torrent_filter->set_name(_("Torrent files"));
    torrent_filter->add_pattern("*.torrent");

    auto const all_filter = Gtk::FileFilter::create();
    all_filter->set_name(_("All files"));
    all_filter->add_pattern("*");

    auto filter_list = Gio::ListStore<Gtk::FileFilter>::create();
    filter_list->append(torrent_filter);
    filter_list->append(all_filter);
    dialog->set_filters(filter_list);
    dialog->set_default_filter(torrent_filter);
}

} // namespace

/****
*****
****/

OptionsDialog::OptionsDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor)
    : Gtk::Dialog(cast_item)
    , impl_(std::make_unique<Impl>(*this, builder, core, std::move(ctor)))
{
    set_transient_for(parent);
}

OptionsDialog::~OptionsDialog() = default;

std::unique_ptr<OptionsDialog> OptionsDialog::create(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("OptionsDialog.ui"));
    return std::unique_ptr<OptionsDialog>(
        gtr_get_widget_derived<OptionsDialog>(builder, "OptionsDialog", parent, core, std::move(ctor)));
}

OptionsDialog::Impl::Impl(
    OptionsDialog& dialog,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Glib::RefPtr<Session> const& core,
    std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor)
    : dialog_(dialog)
    , core_(core)
    , ctor_(std::move(ctor))
    , filename_{ tr_ctorGetSourceFile(ctor_.get()).value_or(""s) }
    , downloadDir_{ tr_ctorGetDownloadDir(ctor_.get(), TR_FORCE).value_or(""s) }
    , file_list_(gtr_get_widget_derived<FileList>(builder, "files_view_scroll", "files_view", core_, 0))
    , run_check_(gtr_get_widget<Gtk::CheckButton>(builder, "start_check"))
    , trash_check_(gtr_get_widget<Gtk::CheckButton>(builder, "trash_check"))
    , priority_combo_(gtr_get_widget<Gtk::DropDown>(builder, "priority_combo"))
    , freespace_label_(gtr_get_widget_derived<FreeSpaceLabel>(builder, "free_space_label", core_, downloadDir_))
{
    dialog_.set_default_response(TR_GTK_RESPONSE_TYPE(ACCEPT));
    dialog.signal_response().connect(sigc::mem_fun(*this, &Impl::addResponseCB));

    gtr_priority_combo_init(*priority_combo_);
    gtr_combo_box_set_active_enum(*priority_combo_, TR_PRI_NORMAL);

    auto* source_chooser = gtr_get_widget_derived<PathButton>(builder, "source_button");
    addTorrentFilters(source_chooser);
    source_chooser->signal_selection_changed().connect([this, source_chooser]() { sourceChanged(source_chooser); });

    auto* destination_chooser = gtr_get_widget_derived<PathButton>(builder, "destination_button");
    destination_chooser->set_filename(downloadDir_);
    destination_chooser->set_shortcut_folders(gtr_get_recent_dirs("download"));

    destination_chooser->signal_selection_changed().connect([this, destination_chooser]()
                                                            { downloadDirChanged(destination_chooser); });

    bool flag = false;
    if (!tr_ctorGetPaused(ctor_.get(), TR_FORCE, &flag))
    {
        g_assert_not_reached();
    }

    run_check_->set_active(!flag);

    if (!tr_ctorGetDeleteSource(ctor_.get(), &flag))
    {
        g_assert_not_reached();
    }

    trash_check_->set_active(flag);

    /* trigger sourceChanged, either directly or indirectly,
     * so that it creates the tor/gtor objects */
    if (!filename_.empty())
    {
        source_chooser->set_filename(filename_);
    }
    else
    {
        sourceChanged(source_chooser);
    }

    dialog_.get_widget_for_response(TR_GTK_RESPONSE_TYPE(ACCEPT))->grab_focus();
}

/****
*****
****/

void torrent_open_chooser_show(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title(_("Open a Torrent"));
    dialog->set_modal(true);
    apply_torrent_filters_to_dialog(dialog);

    if (auto const folder = gtr_pref_string_get(TR_KEY_open_dialog_dir); !folder.empty())
    {
        dialog->set_initial_folder(Gio::File::create_for_path(folder));
    }

    dialog->open_multiple(
        parent,
        [core, dialog](Glib::RefPtr<Gio::AsyncResult>& result)
        {
            try
            {
                auto const files = dialog->open_multiple_finish(result);
                if (files.empty())
                {
                    return;
                }

                bool const do_start = gtr_pref_flag_get(TR_KEY_start_added_torrents);
                bool const do_prompt = gtr_pref_flag_get(TR_KEY_show_options_window);
                bool const do_notify = false;

                auto paths = files;

                if (!paths.empty())
                {
                    if (auto const parent_dir = paths.front()->get_parent())
                    {
                        gtr_pref_string_set(TR_KEY_open_dialog_dir, parent_dir->get_path());
                    }

                    core->add_files(paths, do_start, do_prompt, do_notify);
                }
            }
            catch (Glib::Error const&)
            {
            }
        });
}

/***
****
***/

void TorrentUrlChooserDialog::onOpenURLResponse(int response, Gtk::Entry const& entry, Glib::RefPtr<Session> const& core)
{
    if (response == TR_GTK_RESPONSE_TYPE(CANCEL))
    {
        close();
    }
    else if (response == TR_GTK_RESPONSE_TYPE(ACCEPT))
    {
        auto const url = gtr_str_strip(entry.get_text());

        if (url.empty())
        {
            return;
        }

        if (core->add_from_url(url))
        {
            close();
        }
        else
        {
            gtr_unrecognized_url_dialog(*this, url);
        }
    }
}

std::unique_ptr<TorrentUrlChooserDialog> TorrentUrlChooserDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    auto const builder = Gtk::Builder::create_from_resource(gtr_get_full_resource_path("TorrentUrlChooserDialog.ui"));
    return std::unique_ptr<TorrentUrlChooserDialog>(
        gtr_get_widget_derived<TorrentUrlChooserDialog>(builder, "TorrentUrlChooserDialog", parent, core));
}

TorrentUrlChooserDialog::TorrentUrlChooserDialog(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& builder,
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(cast_item)
{
    set_transient_for(parent);

    auto* const e = gtr_get_widget<Gtk::Entry>(builder, "url_entry");
    auto* const accept = get_widget_for_response(TR_GTK_RESPONSE_TYPE(ACCEPT));
    gtr_paste_clipboard_url_into_entry(*e);

#if GTKMM_CHECK_VERSION(4, 0, 0)
    set_default_widget(*accept);
#else
    set_default(*accept);
#endif

    signal_response().connect([this, e, core](int response) { onOpenURLResponse(response, *e, core); });
}
