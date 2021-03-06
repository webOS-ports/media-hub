/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY {} without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Thomas Voß <thomas.voss@canonical.com>
 */

#include "track_list_stub.h"

#include <core/media/player.h>
#include <core/media/track_list.h>

#include "property_stub.h"
#include "track_list_traits.h"
#include "the_session_bus.h"

#include "mpris/track_list.h"

#include <core/dbus/property.h>
#include <core/dbus/types/object_path.h>
#include <core/dbus/types/variant.h>
#include <core/dbus/types/stl/map.h>
#include <core/dbus/types/stl/vector.h>

#include <limits>

namespace dbus = core::dbus;
namespace media = core::ubuntu::media;

struct media::TrackListStub::Private
{
    Private(
            TrackListStub* impl,
            const std::shared_ptr<media::Player>& parent,
            const dbus::types::ObjectPath& op)
        : impl(impl),
          parent(parent),
          object(impl->access_service()->object_for_path(op)),
          can_edit_tracks(object->get_property<mpris::TrackList::Properties::CanEditTracks>()),
          tracks(object->get_property<mpris::TrackList::Properties::Tracks>())
    {
    }

    TrackListStub* impl;
    std::shared_ptr<media::Player> parent;
    dbus::Object::Ptr object;

    std::shared_ptr<core::dbus::Property<mpris::TrackList::Properties::CanEditTracks>> can_edit_tracks;
    std::shared_ptr<core::dbus::Property<mpris::TrackList::Properties::Tracks>> tracks;

    core::Signal<void> on_track_list_replaced;
    core::Signal<Track::Id> on_track_added;
    core::Signal<Track::Id> on_track_removed;
    core::Signal<Track::Id> on_track_changed;
};

media::TrackListStub::TrackListStub(
        const std::shared_ptr<media::Player>& parent,
        const core::dbus::types::ObjectPath& op)
    : dbus::Stub<media::TrackList>(the_session_bus()),
      d(new Private(this, parent, op))
{
}

media::TrackListStub::~TrackListStub()
{
}

const core::Property<bool>& media::TrackListStub::can_edit_tracks() const
{
    return *d->can_edit_tracks;
}

const core::Property<media::TrackList::Container>& media::TrackListStub::tracks() const
{
    return *d->tracks;
}

media::Track::MetaData media::TrackListStub::query_meta_data_for_track(const media::Track::Id& id)
{
    auto op
            = d->object->invoke_method_synchronously<
                mpris::TrackList::GetTracksMetadata,
                std::map<std::string, std::string>>(id);

    if (op.is_error())
        throw std::runtime_error("Problem querying meta data for track: " + op.error());

    media::Track::MetaData md;
    for(auto pair : op.value())
    {
        md.set(pair.first, pair.second);
    }
    return md;
}

void media::TrackListStub::add_track_with_uri_at(
        const media::Track::UriType& uri,
        const media::Track::Id& id,
        bool make_current)
{
    auto op = d->object->invoke_method_synchronously<mpris::TrackList::AddTrack, void>(
                uri,
                id,
                make_current);

    if (op.is_error())
        throw std::runtime_error("Problem adding track: " + op.error());
}

void media::TrackListStub::remove_track(const media::Track::Id& track)
{
    auto op = d->object->invoke_method_synchronously<mpris::TrackList::RemoveTrack, void>(
                track);

    if (op.is_error())
        throw std::runtime_error("Problem removing track: " + op.error());
}

void media::TrackListStub::go_to(const media::Track::Id& track)
{
    auto op = d->object->invoke_method_synchronously<mpris::TrackList::GoTo, void>(
                track);

    if (op.is_error())
        throw std::runtime_error("Problem adding track: " + op.error());
}

const core::Signal<void>& media::TrackListStub::on_track_list_replaced() const
{
    return d->on_track_list_replaced;
}

const core::Signal<media::Track::Id>& media::TrackListStub::on_track_added() const
{
    return d->on_track_added;
}

const core::Signal<media::Track::Id>& media::TrackListStub::on_track_removed() const
{
    return d->on_track_removed;
}

const core::Signal<media::Track::Id>& media::TrackListStub::on_track_changed() const
{
    return d->on_track_changed;
}
