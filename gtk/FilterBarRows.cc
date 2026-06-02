// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FilterBarRows.h"

namespace
{

constexpr auto ShowModeSeparator = static_cast<tr::app::ShowMode>(-1);
constexpr auto TrackerSeparator = static_cast<FilterTrackerRow::TrackerType>(-1);

} // namespace

Glib::RefPtr<FilterShowModeRow> FilterShowModeRow::create(
    Glib::ustring name,
    int const count,
    tr::app::ShowMode const show_mode,
    Glib::ustring icon_name)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(
        new FilterShowModeRow(std::move(name), count, show_mode, std::move(icon_name), false));
}

Glib::RefPtr<FilterShowModeRow> FilterShowModeRow::create_separator()
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(
        new FilterShowModeRow({}, -1, ShowModeSeparator, {}, true));
}

FilterShowModeRow::FilterShowModeRow(
    Glib::ustring name,
    int const count,
    tr::app::ShowMode const show_mode,
    Glib::ustring icon_name,
    bool const is_separator)
    : Glib::ObjectBase(typeid(FilterShowModeRow))
    , name_(std::move(name))
    , count_(count)
    , show_mode_(show_mode)
    , icon_name_(std::move(icon_name))
    , is_separator_(is_separator)
{
}

Glib::RefPtr<FilterTrackerRow> FilterTrackerRow::create(
    Glib::ustring displayname,
    int const count,
    TrackerType const type,
    Glib::ustring sitename)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(
        new FilterTrackerRow(std::move(displayname), count, type, std::move(sitename), false));
}

Glib::RefPtr<FilterTrackerRow> FilterTrackerRow::create_separator()
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return Glib::make_refptr_for_instance(new FilterTrackerRow({}, -1, TrackerSeparator, {}, true));
}

FilterTrackerRow::FilterTrackerRow(
    Glib::ustring displayname,
    int const count,
    TrackerType const type,
    Glib::ustring sitename,
    bool const is_separator)
    : Glib::ObjectBase(typeid(FilterTrackerRow))
    , displayname_(std::move(displayname))
    , count_(count)
    , type_(type)
    , sitename_(std::move(sitename))
    , is_separator_(is_separator)
{
}
