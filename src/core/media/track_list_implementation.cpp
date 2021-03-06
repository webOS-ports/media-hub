/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Thomas Voß <thomas.voss@canonical.com>
 */

#include <stdio.h>
#include <stdlib.h>

#include "track_list_implementation.h"

#include "engine.h"

namespace dbus = core::dbus;
namespace media = core::ubuntu::media;

struct media::TrackListImplementation::Private
{
    typedef std::map<Track::Id, std::tuple<Track::UriType, Track::MetaData>> MetaDataCache;

    dbus::types::ObjectPath path;
    MetaDataCache meta_data_cache;
    std::shared_ptr<media::Engine::MetaDataExtractor> extractor;
};

media::TrackListImplementation::TrackListImplementation(
        const dbus::types::ObjectPath& op,
        const std::shared_ptr<media::Engine::MetaDataExtractor>& extractor)
    : media::TrackListSkeleton(op),
      d(new Private{op, Private::MetaDataCache{}, extractor})
{
    can_edit_tracks().set(true);
}

media::TrackListImplementation::~TrackListImplementation()
{
}

media::Track::UriType media::TrackListImplementation::query_uri_for_track(const media::Track::Id& id)
{
    auto it = d->meta_data_cache.find(id);

    if (it == d->meta_data_cache.end())
        return Track::UriType{};

    return std::get<0>(it->second);
}

media::Track::MetaData media::TrackListImplementation::query_meta_data_for_track(const media::Track::Id& id)
{
    auto it = d->meta_data_cache.find(id);

    if (it == d->meta_data_cache.end())
        return Track::MetaData{};

    return std::get<1>(it->second);
}

void media::TrackListImplementation::add_track_with_uri_at(
        const media::Track::UriType& uri,
        const media::Track::Id& position,
        bool make_current)
{
    static size_t track_counter = 0;

    std::stringstream ss; ss << d->path.as_string() << "/" << track_counter++;
    Track::Id id{ss.str()};

    auto result = tracks().update([this, id, position, make_current](TrackList::Container& container)
    {
        auto it = std::find(container.begin(), container.end(), position);
        container.insert(it, id);
        return true;
    });

    if (result)
    {
        if (d->meta_data_cache.count(id) == 0)
        {
            d->meta_data_cache[id] = std::make_tuple(
                        uri,
                        d->extractor->meta_data_for_track_with_uri(uri));
        } else
        {
            std::get<0>(d->meta_data_cache[id]) = uri;
        }

        if (make_current)
            go_to(id);

        on_track_added()(id);
    }
}

void media::TrackListImplementation::remove_track(const media::Track::Id& id)
{
    auto result = tracks().update([id](TrackList::Container& container)
    {
        container.erase(std::find(container.begin(), container.end(), id));
        return true;
    });

    if (result)
    {
        d->meta_data_cache.erase(id);

        on_track_removed()(id);
    }

}

void media::TrackListImplementation::go_to(const media::Track::Id& track)
{
    (void) track;
}
