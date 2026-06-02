// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "SorterBase.hh"
#include "SortListModel.h"

template<typename ItemT>
SortListModel<ItemT>::SortListModel(Glib::RefPtr<Gio::ListModel> const& model, Glib::RefPtr<SorterType> const& sorter)
    : Gtk::SortListModel(model, sorter)
{
}

template<typename ItemT>
template<typename ModelT>
Glib::RefPtr<SortListModel<ItemT>> SortListModel<ItemT>::create(
    Glib::RefPtr<ModelT> const& model,
    Glib::RefPtr<SorterType> const& sorter)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new SortListModel(model, sorter));
}
