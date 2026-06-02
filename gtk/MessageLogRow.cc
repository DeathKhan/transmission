// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "MessageLogRow.h"

#include "FilterBase.hh"

#include <gtkmm/filter.h>

Glib::RefPtr<MessageLogRow> MessageLogRow::create(
    tr_log_message const* tr_msg,
    unsigned int const sequence,
    Glib::ustring name,
    Glib::ustring message)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new MessageLogRow(tr_msg, sequence, std::move(name), std::move(message)));
}

MessageLogRow::MessageLogRow(
    tr_log_message const* tr_msg,
    unsigned int const sequence,
    Glib::ustring name,
    Glib::ustring message)
    : Glib::ObjectBase(typeid(MessageLogRow))
    , sequence_(sequence)
    , name_(std::move(name))
    , message_(std::move(message))
    , tr_msg_(tr_msg)
{
}

void MessageLogFilter::set_max_level(tr_log_level const level)
{
    if (max_level_ == level)
    {
        return;
    }

    max_level_ = level;
    changed(Gtk::Filter::Change::DIFFERENT);
}

bool MessageLogFilter::match(MessageLogRow const& row) const
{
    auto const* const node = row.get_tr_msg();
    return node != nullptr && node->level <= max_level_;
}

MessageLogFilter::MessageLogFilter()
    : Glib::ObjectBase(typeid(MessageLogFilter))
{
}

Glib::RefPtr<MessageLogFilter> MessageLogFilter::create()
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new MessageLogFilter());
}
