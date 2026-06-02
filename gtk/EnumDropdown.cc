// This file Copyright © Transmission authors and contributors.
// SPDX-License-Identifier: MIT

#include "EnumDropdown.h"

#include <libtransmission/transmission.h>

#include <glibmm/i18n.h>
#include <gtkmm/stringlist.h>

#include <gtk/gtk.h>

namespace
{

auto constexpr ValuesKey = "tr-enum-values";

using EnumValues = std::vector<int>;

void store_values(Gtk::DropDown& dropdown, EnumValues* values)
{
    g_object_set_data_full(
        G_OBJECT(dropdown.gobj()),
        ValuesKey,
        values,
        [](gpointer ptr) { delete static_cast<EnumValues*>(ptr); });
}

[[nodiscard]] EnumValues const* get_values(Gtk::DropDown const& dropdown)
{
    return static_cast<EnumValues*>(g_object_get_data(G_OBJECT(dropdown.gobj()), ValuesKey));
}

} // namespace

void enum_dropdown_init(Gtk::DropDown& dropdown, std::vector<std::pair<Glib::ustring, int>> const& items)
{
    auto labels = std::vector<Glib::ustring>{};
    auto* const values = new EnumValues{};
    labels.reserve(items.size());
    values->reserve(items.size());

    for (auto const& [label, value] : items)
    {
        labels.push_back(label);
        values->push_back(value);
    }

    dropdown.set_model(Gtk::StringList::create(labels));
    store_values(dropdown, values);
}

int enum_dropdown_get_value(Gtk::DropDown const& dropdown)
{
    auto const values = get_values(dropdown);
    auto const selected = dropdown.get_selected();

    if (values != nullptr && selected != GTK_INVALID_LIST_POSITION && static_cast<size_t>(selected) < values->size())
    {
        return (*values)[static_cast<size_t>(selected)];
    }

    return 0;
}

void enum_dropdown_set_value(Gtk::DropDown& dropdown, int const value)
{
    auto const values = get_values(dropdown);
    if (values == nullptr)
    {
        return;
    }

    for (guint i = 0, n = values->size(); i < n; ++i)
    {
        if (values->at(i) == value)
        {
            dropdown.set_selected(i);
            return;
        }
    }
}

void priority_dropdown_init(Gtk::DropDown& dropdown)
{
    enum_dropdown_init(
        dropdown,
        {
            { _("High"), TR_PRI_HIGH },
            { _("Normal"), TR_PRI_NORMAL },
            { _("Low"), TR_PRI_LOW },
        });
}
