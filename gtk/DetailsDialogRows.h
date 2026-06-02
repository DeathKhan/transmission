// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "FilterBase.h"
#include "SorterBase.h"

#include <libtransmission/transmission.h>
#include <libtransmission/values.h>

#include <gdkmm/pixbuf.h>
#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

#include <string>
#include <string_view>

using Speed = tr::Values::Speed;

class DetailsPeerRow : public Glib::Object
{
public:
    static Glib::RefPtr<DetailsPeerRow> create(
        std::string key,
        std::string_view torrent_name,
        tr_peer_stat const& peer);

    static Glib::RefPtr<DetailsPeerRow> create_rpc(
        std::string key,
        Glib::ustring torrent_name,
        Glib::ustring address,
        Glib::ustring client,
        Glib::ustring flags,
        int progress,
        Speed download_rate_speed,
        Speed upload_rate_speed);

    void update(tr_peer_stat const& peer);

    void set_was_updated(bool value) noexcept
    {
        was_updated_ = value;
    }

    [[nodiscard]] bool get_was_updated() const noexcept
    {
        return was_updated_;
    }

    [[nodiscard]] std::string const& get_key() const noexcept
    {
        return key_;
    }

    [[nodiscard]] Glib::ustring const& get_address() const noexcept
    {
        return address_;
    }

    [[nodiscard]] Glib::ustring const& get_address_collated() const noexcept
    {
        return address_collated_;
    }

    [[nodiscard]] Glib::ustring const& get_client() const noexcept
    {
        return client_;
    }

    [[nodiscard]] int get_progress() const noexcept
    {
        return progress_;
    }

    [[nodiscard]] Glib::ustring const& get_download_rate_string() const noexcept
    {
        return download_rate_string_;
    }

    [[nodiscard]] Glib::ustring const& get_upload_rate_string() const noexcept
    {
        return upload_rate_string_;
    }

    [[nodiscard]] Speed get_download_rate_speed() const noexcept
    {
        return download_rate_speed_;
    }

    [[nodiscard]] Speed get_upload_rate_speed() const noexcept
    {
        return upload_rate_speed_;
    }

    [[nodiscard]] Glib::ustring const& get_upload_request_count_string() const noexcept
    {
        return upload_request_count_string_;
    }

    [[nodiscard]] Glib::ustring const& get_download_request_count_string() const noexcept
    {
        return download_request_count_string_;
    }

    [[nodiscard]] Glib::ustring const& get_blocks_downloaded_count_string() const noexcept
    {
        return blocks_downloaded_count_string_;
    }

    [[nodiscard]] Glib::ustring const& get_blocks_uploaded_count_string() const noexcept
    {
        return blocks_uploaded_count_string_;
    }

    [[nodiscard]] Glib::ustring const& get_reqs_cancelled_by_client_count_string() const noexcept
    {
        return reqs_cancelled_by_client_count_string_;
    }

    [[nodiscard]] Glib::ustring const& get_reqs_cancelled_by_peer_count_string() const noexcept
    {
        return reqs_cancelled_by_peer_count_string_;
    }

    [[nodiscard]] int get_upload_request_count_number() const noexcept
    {
        return upload_request_count_number_;
    }

    [[nodiscard]] int get_download_request_count_number() const noexcept
    {
        return download_request_count_number_;
    }

    [[nodiscard]] int get_blocks_downloaded_count_number() const noexcept
    {
        return blocks_downloaded_count_number_;
    }

    [[nodiscard]] int get_blocks_uploaded_count_number() const noexcept
    {
        return blocks_uploaded_count_number_;
    }

    [[nodiscard]] int get_reqs_cancelled_by_client_count_number() const noexcept
    {
        return reqs_cancelled_by_client_count_number_;
    }

    [[nodiscard]] int get_reqs_cancelled_by_peer_count_number() const noexcept
    {
        return reqs_cancelled_by_peer_count_number_;
    }

    [[nodiscard]] Glib::ustring const& get_encryption_stock_id() const noexcept
    {
        return encryption_stock_id_;
    }

    [[nodiscard]] Glib::ustring const& get_flags() const noexcept
    {
        return flags_;
    }

    [[nodiscard]] Glib::ustring const& get_torrent_name() const noexcept
    {
        return torrent_name_;
    }

private:
    DetailsPeerRow();
    DetailsPeerRow(std::string key, std::string_view torrent_name, tr_peer_stat const& peer);

    void init_from_peer(std::string_view torrent_name, tr_peer_stat const& peer);
    void refresh_rates(tr_peer_stat const& peer);

    std::string key_;
    bool was_updated_ = false;
    Glib::ustring address_;
    Glib::ustring address_collated_;
    Glib::ustring client_;
    int progress_ = 0;
    Glib::ustring download_rate_string_;
    Glib::ustring upload_rate_string_;
    Speed download_rate_speed_;
    Speed upload_rate_speed_;
    Glib::ustring upload_request_count_string_;
    Glib::ustring download_request_count_string_;
    Glib::ustring blocks_downloaded_count_string_;
    Glib::ustring blocks_uploaded_count_string_;
    Glib::ustring reqs_cancelled_by_client_count_string_;
    Glib::ustring reqs_cancelled_by_peer_count_string_;
    int upload_request_count_number_ = 0;
    int download_request_count_number_ = 0;
    int blocks_downloaded_count_number_ = 0;
    int blocks_uploaded_count_number_ = 0;
    int reqs_cancelled_by_client_count_number_ = 0;
    int reqs_cancelled_by_peer_count_number_ = 0;
    Glib::ustring encryption_stock_id_;
    Glib::ustring flags_;
    Glib::ustring torrent_name_;
};

class DetailsWebseedRow : public Glib::Object
{
public:
    static Glib::RefPtr<DetailsWebseedRow> create(std::string key, Glib::ustring url);

    void update(tr_webseed_view const& webseed);

    void set_was_updated(bool value) noexcept
    {
        was_updated_ = value;
    }

    [[nodiscard]] bool get_was_updated() const noexcept
    {
        return was_updated_;
    }

    [[nodiscard]] std::string const& get_key() const noexcept
    {
        return key_;
    }

    [[nodiscard]] Glib::ustring const& get_url() const noexcept
    {
        return url_;
    }

    [[nodiscard]] Glib::ustring const& get_download_rate_string() const noexcept
    {
        return download_rate_string_;
    }

    [[nodiscard]] Speed get_download_rate_speed() const noexcept
    {
        return download_rate_speed_;
    }

private:
    DetailsWebseedRow(std::string key, Glib::ustring url);

    std::string key_;
    bool was_updated_ = false;
    Glib::ustring url_;
    Glib::ustring download_rate_string_;
    Speed download_rate_speed_;
};

class DetailsTrackerRow : public Glib::Object
{
public:
    static Glib::RefPtr<DetailsTrackerRow> create(
        tr_torrent_id_t torrent_id,
        int tracker_id,
        std::string key,
        bool is_backup);

    void set_text(Glib::ustring text);
    void set_favicon(Glib::RefPtr<Gdk::Pixbuf> const& favicon);
    void set_tracker_id(int tracker_id) noexcept
    {
        tracker_id_ = tracker_id;
    }

    void set_is_backup(bool is_backup) noexcept
    {
        is_backup_ = is_backup;
    }

    void set_was_updated(bool value) noexcept
    {
        was_updated_ = value;
    }

    [[nodiscard]] bool get_was_updated() const noexcept
    {
        return was_updated_;
    }

    [[nodiscard]] std::string const& get_key() const noexcept
    {
        return key_;
    }

    [[nodiscard]] tr_torrent_id_t get_torrent_id() const noexcept
    {
        return torrent_id_;
    }

    [[nodiscard]] int get_tracker_id() const noexcept
    {
        return tracker_id_;
    }

    [[nodiscard]] bool get_is_backup() const noexcept
    {
        return is_backup_;
    }

    [[nodiscard]] Glib::ustring const& get_text() const noexcept
    {
        return text_;
    }

    [[nodiscard]] Glib::RefPtr<Gdk::Pixbuf> const& get_favicon() const noexcept
    {
        return favicon_;
    }

private:
    DetailsTrackerRow(tr_torrent_id_t torrent_id, int tracker_id, std::string key, bool is_backup);

    tr_torrent_id_t torrent_id_;
    int tracker_id_;
    std::string key_;
    bool is_backup_ = false;
    bool was_updated_ = false;
    Glib::ustring text_;
    Glib::RefPtr<Gdk::Pixbuf> favicon_;
};

class DetailsTrackerBackupFilter : public FilterBase<DetailsTrackerRow>
{
public:
    void set_show_backup(bool show_backup);

    bool match(DetailsTrackerRow const& row) const override;

    static Glib::RefPtr<DetailsTrackerBackupFilter> create();

private:
    DetailsTrackerBackupFilter();

    bool show_backup_ = false;
};

class DetailsPeerProgressSorter : public SorterBase<DetailsPeerRow>
{
public:
    int compare(DetailsPeerRow const& lhs, DetailsPeerRow const& rhs) const override;

    static Glib::RefPtr<DetailsPeerProgressSorter> create();

private:
    DetailsPeerProgressSorter() = default;
};
