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

#ifndef CODEC_H_
#define CODEC_H_

#include <core/media/player.h>
#include <core/media/track.h>

#include <core/dbus/codec.h>

namespace core
{
namespace dbus
{
namespace helper
{
template<>
struct TypeMapper<core::ubuntu::media::Track::MetaData>
{
    constexpr static ArgumentType type_value()
    {
        return ArgumentType::floating_point;
    }
    constexpr static bool is_basic_type()
    {
        return false;
    }
    constexpr static bool requires_signature()
    {
        return false;
    }

    static std::string signature()
    {
        static const std::string s = TypeMapper<double>::signature();
        return s;
    }
};
}

template<>
struct Codec<core::ubuntu::media::Track::MetaData>
{
    static void encode_argument(core::dbus::Message::Writer& out, const core::ubuntu::media::Track::MetaData& in)
    {
        (void) out;
        (void) in;
    }

    static void decode_argument(core::dbus::Message::Reader& out, core::ubuntu::media::Track::MetaData& in)
    {
        (void) out;
        (void) in;
    }
};

namespace helper
{
template<>
struct TypeMapper<core::ubuntu::media::Player::PlaybackStatus>
{
    constexpr static ArgumentType type_value()
    {
        return core::dbus::ArgumentType::int16;
    }
    constexpr static bool is_basic_type()
    {
        return false;
    }
    constexpr static bool requires_signature()
    {
        return false;
    }

    static std::string signature()
    {
        static const std::string s = TypeMapper<std::int16_t>::signature();
        return s;
    }
};
}

template<>
struct Codec<core::ubuntu::media::Player::PlaybackStatus>
{
    static void encode_argument(core::dbus::Message::Writer& out, const core::ubuntu::media::Player::PlaybackStatus& in)
    {
        out.push_int16(static_cast<std::int16_t>(in));
    }

    static void decode_argument(core::dbus::Message::Reader& out, core::ubuntu::media::Player::PlaybackStatus& in)
    {
        in = static_cast<core::ubuntu::media::Player::PlaybackStatus>(out.pop_int16());
    }
};
namespace helper
{
template<>
struct TypeMapper<core::ubuntu::media::Player::LoopStatus>
{
    constexpr static ArgumentType type_value()
    {
        return core::dbus::ArgumentType::int16;
    }
    constexpr static bool is_basic_type()
    {
        return false;
    }
    constexpr static bool requires_signature()
    {
        return false;
    }

    static std::string signature()
    {
        static const std::string s = TypeMapper<std::int16_t>::signature();
        return s;
    }
};
}

template<>
struct Codec<core::ubuntu::media::Player::LoopStatus>
{
    static void encode_argument(core::dbus::Message::Writer& out, const core::ubuntu::media::Player::LoopStatus& in)
    {
        out.push_int16(static_cast<std::int16_t>(in));
    }

    static void decode_argument(core::dbus::Message::Reader& out, core::ubuntu::media::Player::LoopStatus& in)
    {
        in = static_cast<core::ubuntu::media::Player::LoopStatus>(out.pop_int16());
    }
};

}
}

#endif // CODEC_H_