// This file Copyright © Transmission authors and contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include <glibmm/ustring.h>
#include <gtkmm/dropdown.h>

#include <utility>
#include <vector>

void enum_dropdown_init(Gtk::DropDown& dropdown, std::vector<std::pair<Glib::ustring, int>> const& items);
[[nodiscard]] int enum_dropdown_get_value(Gtk::DropDown const& dropdown);
void enum_dropdown_set_value(Gtk::DropDown& dropdown, int value);

void priority_dropdown_init(Gtk::DropDown& dropdown);
