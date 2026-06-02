// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "FilterBase.h"

#include <libtransmission/log.h>

#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>

class MessageLogRow : public Glib::Object
{
public:
    static Glib::RefPtr<MessageLogRow> create(
        tr_log_message const* tr_msg,
        unsigned int sequence,
        Glib::ustring name,
        Glib::ustring message);

    [[nodiscard]] unsigned int get_sequence() const noexcept
    {
        return sequence_;
    }

    [[nodiscard]] Glib::ustring const& get_name() const noexcept
    {
        return name_;
    }

    [[nodiscard]] Glib::ustring const& get_message() const noexcept
    {
        return message_;
    }

    [[nodiscard]] tr_log_message const* get_tr_msg() const noexcept
    {
        return tr_msg_;
    }

private:
    MessageLogRow(
        tr_log_message const* tr_msg,
        unsigned int sequence,
        Glib::ustring name,
        Glib::ustring message);

    unsigned int sequence_;
    Glib::ustring name_;
    Glib::ustring message_;
    tr_log_message const* tr_msg_;
};

class MessageLogFilter : public FilterBase<MessageLogRow>
{
public:
    void set_max_level(tr_log_level level);

    bool match(MessageLogRow const& row) const override;

    static Glib::RefPtr<MessageLogFilter> create();

private:
    MessageLogFilter();

    tr_log_level max_level_ = TR_LOG_INFO;
};
