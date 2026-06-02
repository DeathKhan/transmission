// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "DetailsDialogRows.h"

#include "FilterBase.hh"
#include "SorterBase.hh"

#include "SorterBase.hh"

#include <fmt/ranges.h>

#include <gtkmm/filter.h>

#include <fmt/format.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

Glib::RefPtr<DetailsPeerRow> DetailsPeerRow::create(
    std::string key,
    std::string_view const torrent_name,
    tr_peer_stat const& peer)
{
    return Glib::make_refptr_for_instance(new DetailsPeerRow(std::move(key), torrent_name, peer));
}

Glib::RefPtr<DetailsPeerRow> DetailsPeerRow::create_rpc(
    std::string key,
    Glib::ustring torrent_name,
    Glib::ustring address,
    Glib::ustring client,
    Glib::ustring flags,
    int const progress,
    Speed const download_rate_speed,
    Speed const upload_rate_speed)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto const row = Glib::make_refptr_for_instance(new DetailsPeerRow());
    row->key_ = std::move(key);
    row->torrent_name_ = std::move(torrent_name);
    row->address_ = std::move(address);
    row->address_collated_ = row->address_.lowercase();
    row->client_ = std::move(client);
    row->flags_ = std::move(flags);
    row->progress_ = progress;
    row->download_rate_speed_ = download_rate_speed;
    row->upload_rate_speed_ = upload_rate_speed;
    row->download_rate_string_ = download_rate_speed.to_string();
    row->upload_rate_string_ = upload_rate_speed.to_string();
    row->was_updated_ = true;
    return row;
}

DetailsPeerRow::DetailsPeerRow()
    : Glib::ObjectBase(typeid(DetailsPeerRow))
{
}

DetailsPeerRow::DetailsPeerRow(std::string key, std::string_view const torrent_name, tr_peer_stat const& peer)
    : Glib::ObjectBase(typeid(DetailsPeerRow))
    , key_(std::move(key))
{
    init_from_peer(torrent_name, peer);
    update(peer);
}

void DetailsPeerRow::init_from_peer(std::string_view const torrent_name, tr_peer_stat const& peer)
{
    auto peer_addr4 = in_addr();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const* const peer_addr4_octets = reinterpret_cast<uint8_t const*>(&peer_addr4.s_addr);
    address_collated_ = inet_pton(AF_INET, peer.addr.c_str(), &peer_addr4) != 1 ?
        Glib::ustring{ peer.addr } :
        Glib::ustring(
            fmt::format(
                "{:03}",
                fmt::join(
                    peer_addr4_octets,
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                    peer_addr4_octets + sizeof(peer_addr4.s_addr),
                    ".")));

    address_ = peer.addr;
    client_ = peer.user_agent;
    encryption_stock_id_ = peer.is_encrypted ? "lock" : "";
    torrent_name_ = Glib::ustring(std::string{ torrent_name });
}

void DetailsPeerRow::refresh_rates(tr_peer_stat const& peer)
{
    auto const down_speed = peer.rate_to_client;
    auto const up_speed = peer.rate_to_peer;

    upload_rate_string_.clear();
    download_rate_string_.clear();
    upload_request_count_string_.clear();
    download_request_count_string_.clear();
    blocks_downloaded_count_string_.clear();
    blocks_uploaded_count_string_.clear();
    reqs_cancelled_by_client_count_string_.clear();
    reqs_cancelled_by_peer_count_string_.clear();

    if (peer.rate_to_peer.base_quantity() > 0U)
    {
        upload_rate_string_ = up_speed.to_string();
    }

    if (peer.rate_to_client.base_quantity() > 0U)
    {
        download_rate_string_ = down_speed.to_string();
    }

    if (peer.active_reqs_to_peer > 0)
    {
        download_request_count_string_ = std::to_string(peer.active_reqs_to_peer);
    }

    if (peer.active_reqs_to_client > 0)
    {
        upload_request_count_string_ = std::to_string(peer.active_reqs_to_client);
    }

    if (peer.blocks_to_peer > 0)
    {
        blocks_uploaded_count_string_ = std::to_string(peer.blocks_to_peer);
    }

    if (peer.blocks_to_client > 0)
    {
        blocks_downloaded_count_string_ = std::to_string(peer.blocks_to_client);
    }

    if (peer.cancels_to_peer > 0)
    {
        reqs_cancelled_by_client_count_string_ = std::to_string(peer.cancels_to_peer);
    }

    if (peer.cancels_to_client > 0)
    {
        reqs_cancelled_by_peer_count_string_ = std::to_string(peer.cancels_to_client);
    }

    progress_ = static_cast<int>(100.0 * peer.progress);
    upload_request_count_number_ = peer.active_reqs_to_client;
    download_request_count_number_ = peer.active_reqs_to_peer;
    download_rate_speed_ = down_speed;
    upload_rate_speed_ = up_speed;
    flags_ = peer.flag_str;
    blocks_downloaded_count_number_ = peer.blocks_to_client;
    blocks_uploaded_count_number_ = peer.blocks_to_peer;
    reqs_cancelled_by_client_count_number_ = peer.cancels_to_peer;
    reqs_cancelled_by_peer_count_number_ = peer.cancels_to_client;
}

void DetailsPeerRow::update(tr_peer_stat const& peer)
{
    refresh_rates(peer);
    was_updated_ = true;
}

Glib::RefPtr<DetailsWebseedRow> DetailsWebseedRow::create(std::string key, Glib::ustring url)
{
    return Glib::make_refptr_for_instance(new DetailsWebseedRow(std::move(key), std::move(url)));
}

DetailsWebseedRow::DetailsWebseedRow(std::string key, Glib::ustring url)
    : Glib::ObjectBase(typeid(DetailsWebseedRow))
    , key_(std::move(key))
    , url_(std::move(url))
{
}

void DetailsWebseedRow::update(tr_webseed_view const& webseed)
{
    download_rate_speed_ = Speed{ webseed.download_bytes_per_second, Speed::Units::Byps };
    download_rate_string_ = webseed.is_downloading ? Glib::ustring(download_rate_speed_.to_string()) : Glib::ustring{};
    was_updated_ = true;
}

Glib::RefPtr<DetailsTrackerRow> DetailsTrackerRow::create(
    tr_torrent_id_t const torrent_id,
    int const tracker_id,
    std::string key,
    bool const is_backup)
{
    return Glib::make_refptr_for_instance(new DetailsTrackerRow(torrent_id, tracker_id, std::move(key), is_backup));
}

DetailsTrackerRow::DetailsTrackerRow(
    tr_torrent_id_t const torrent_id,
    int const tracker_id,
    std::string key,
    bool const is_backup)
    : Glib::ObjectBase(typeid(DetailsTrackerRow))
    , torrent_id_(torrent_id)
    , tracker_id_(tracker_id)
    , key_(std::move(key))
    , is_backup_(is_backup)
{
}

void DetailsTrackerRow::set_text(Glib::ustring text)
{
    text_ = std::move(text);
}

void DetailsTrackerRow::set_favicon(Glib::RefPtr<Gdk::Pixbuf> const& favicon)
{
    favicon_ = favicon;
}

Glib::RefPtr<DetailsTrackerBackupFilter> DetailsTrackerBackupFilter::create()
{
    return Glib::make_refptr_for_instance(new DetailsTrackerBackupFilter());
}

DetailsTrackerBackupFilter::DetailsTrackerBackupFilter() = default;

void DetailsTrackerBackupFilter::set_show_backup(bool const show_backup)
{
    if (show_backup_ == show_backup)
    {
        return;
    }

    show_backup_ = show_backup;
    changed(Gtk::Filter::Change::DIFFERENT);
}

bool DetailsTrackerBackupFilter::match(DetailsTrackerRow const& row) const
{
    return show_backup_ || !row.get_is_backup();
}

Glib::RefPtr<DetailsPeerProgressSorter> DetailsPeerProgressSorter::create()
{
    return Glib::make_refptr_for_instance(new DetailsPeerProgressSorter());
}

int DetailsPeerProgressSorter::compare(DetailsPeerRow const& lhs, DetailsPeerRow const& rhs) const
{
    if (lhs.get_progress() != rhs.get_progress())
    {
        return lhs.get_progress() > rhs.get_progress() ? -1 : 1;
    }

    return g_utf8_collate(lhs.get_address_collated().c_str(), rhs.get_address_collated().c_str());
}
