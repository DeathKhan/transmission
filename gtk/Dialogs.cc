// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "Dialogs.h"

#include "GtkCompat.h"
#include "Session.h"
#include "Utils.h"

#include <glibmm/i18n.h>
#include <glibmm/ustring.h>
#include <gtkmm/alertdialog.h>

#include <fmt/format.h>

#include <memory>
#include <vector>

/***
****
***/

void gtr_confirm_remove(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids,
    bool delete_files)
{
    int connected = 0;
    int incomplete = 0;
    int const count = torrent_ids.size();

    if (count == 0)
    {
        return;
    }

    for (auto const id : torrent_ids)
    {
        if (auto const torrent = core->find_torrent_ref(id); torrent)
        {
            if (torrent->has_incomplete_data())
            {
                ++incomplete;
            }

            if (torrent->get_active_peer_count() > 0)
            {
                ++connected;
            }
        }
    }

    auto const primary_text = fmt::format(
        fmt::runtime(
            !delete_files ? ngettext("Remove torrent?", "Remove {count:L} torrents?", count) :
                            ngettext(
                                "Delete this torrent's downloaded files?",
                                "Delete these {count:L} torrents' downloaded files?",
                                count)),
        fmt::arg("count", count));

    Glib::ustring secondary_text;
    if (incomplete == 0 && connected == 0)
    {
        secondary_text = ngettext(
            "Once removed, continuing the transfer will require the torrent file or magnet link.",
            "Once removed, continuing the transfers will require the torrent files or magnet links.",
            count);
    }
    else if (count == incomplete)
    {
        secondary_text = ngettext(
            "This torrent has not finished downloading.",
            "These torrents have not finished downloading.",
            count);
    }
    else if (count == connected)
    {
        secondary_text = ngettext("This torrent is connected to peers.", "These torrents are connected to peers.", count);
    }
    else
    {
        if (connected != 0)
        {
            secondary_text += ngettext(
                "One of these torrents is connected to peers.",
                "Some of these torrents are connected to peers.",
                connected);
        }

        if (connected != 0 && incomplete != 0)
        {
            secondary_text += "\n";
        }

        if (incomplete != 0)
        {
            secondary_text += ngettext(
                "One of these torrents has not finished downloading.",
                "Some of these torrents have not finished downloading.",
                incomplete);
        }
    }

    auto dialog = Gtk::AlertDialog::create(primary_text);
    dialog->set_detail(secondary_text);
    dialog->set_buttons({ _("_Cancel"), delete_files ? _("_Delete") : _("_Remove") });
    dialog->set_cancel_button(0);
    dialog->set_default_button(0);

    dialog->choose(
        parent,
        [core, torrent_ids, delete_files, dialog](Glib::RefPtr<Gio::AsyncResult>& result)
        {
            try
            {
                if (dialog->choose_finish(result) == 1)
                {
                    for (auto const id : torrent_ids)
                    {
                        core->remove_torrent(id, delete_files);
                    }
                }
            }
            catch (Glib::Error const&)
            {
            }
        });
}
