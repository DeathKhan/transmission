// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the/ folder.

#pragma once

#include "FilterBase.hh"
#include "FilterListModel.h"

template<typename ItemT>
FilterListModel<ItemT>::FilterListModel(Glib::RefPtr<Gio::ListModel> const& model, Glib::RefPtr<FilterType> const& filter)
    : Gtk::FilterListModel(model, filter)
{
}

template<typename ItemT>
template<typename ModelT>
Glib::RefPtr<FilterListModel<ItemT>> FilterListModel<ItemT>::create(
    Glib::RefPtr<ModelT> const& model,
    Glib::RefPtr<FilterType> const& filter)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new FilterListModel(model, filter));
}
