/*
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
 *              Jim Hodapp <jim.hodapp@canonical.com>
 */

#include "player_implementation.h"
#include "util/timeout.h"

#include <unistd.h>

#include "engine.h"
#include "track_list_implementation.h"

#include <hybris/media/media_codec_layer.h>
#include "powerd_service.h"
#include "unity_screen_service.h"
#include "gstreamer/engine.h"

#include <memory>
#include <exception>
#include <iostream>
#include <mutex>

#define UNUSED __attribute__((unused))

namespace media = core::ubuntu::media;
namespace dbus = core::dbus;

using namespace std;

struct media::PlayerImplementation::Private :
        public std::enable_shared_from_this<Private>
{
    enum class wakelock_clear_t
    {
        WAKELOCK_CLEAR_INACTIVE,
        WAKELOCK_CLEAR_DISPLAY,
        WAKELOCK_CLEAR_SYSTEM,
        WAKELOCK_CLEAR_INVALID
    };

    Private(PlayerImplementation* parent,
            const dbus::types::ObjectPath& session_path,
            const std::shared_ptr<media::Service>& service,
            PlayerImplementation::PlayerKey key)
        : parent(parent),
          service(service),
          engine(std::make_shared<gstreamer::Engine>()),
          session_path(session_path),
          track_list(
              new media::TrackListImplementation(
                  session_path.as_string() + "/TrackList",
                  engine->meta_data_extractor())),
          sys_lock_name("media-hub-music-playback"),
          disp_cookie(-1),
          system_wakelock_count(0),
          display_wakelock_count(0),
          previous_state(Engine::State::stopped),
          key(key),
          engine_state_change_connection(engine->state().changed().connect(make_state_change_handler()))
    {
        auto bus = std::shared_ptr<dbus::Bus>(new dbus::Bus(core::dbus::WellKnownBus::system));
        bus->install_executor(dbus::asio::make_executor(bus));

        auto stub_service = dbus::Service::use_service(bus, dbus::traits::Service<core::Powerd>::interface_name());
        powerd_session = stub_service->object_for_path(dbus::types::ObjectPath("/com/canonical/powerd"));

        auto uscreen_stub_service = dbus::Service::use_service(bus, dbus::traits::Service<core::UScreen>::interface_name());
        uscreen_session = uscreen_stub_service->object_for_path(dbus::types::ObjectPath("/com/canonical/Unity/Screen"));

        decoding_service_set_client_death_cb(&Private::on_client_died_cb, key, static_cast<void*>(this));
    }

    ~Private()
    {
        // Make sure that we don't hold on to the wakelocks if media-hub-server
        // ever gets restarted manually or automatically
        clear_wakelocks();

        // The engine destructor can lead to a stop change state which will
        // trigger the state change handler. Ensure the handler is not called
        // by disconnecting the state change signal
        engine_state_change_connection.disconnect();
    }

    std::function<void(const Engine::State& state)> make_state_change_handler()
    {
        /*
         * Wakelock state logic:
         * PLAYING->READY or PLAYING->PAUSED or PLAYING->STOPPED: delay 4 seconds and try to clear current wakelock type
         * ANY STATE->PLAYING: request a new wakelock (system or display)
         */
        return [this](const Engine::State& state)
        {
            switch(state)
            {
            case Engine::State::ready:
            {
                parent->playback_status().set(media::Player::ready);
                if (previous_state == Engine::State::playing)
                {
                    timeout(4000, true, make_clear_wakelock_functor());
                }
                break;
            }
            case Engine::State::playing:
            {
                // We update the track meta data prior to updating the playback status.
                // Some MPRIS clients expect this order of events.
                parent->meta_data_for_current_track().set(std::get<1>(engine->track_meta_data().get()));
                // And update our playback status.
                parent->playback_status().set(media::Player::playing);
                request_power_state();
                break;
            }
            case Engine::State::stopped:
            {
                parent->playback_status().set(media::Player::stopped);
                if (previous_state ==  Engine::State::playing)
                {
                    timeout(4000, true, make_clear_wakelock_functor());
                }
                break;
            }
            case Engine::State::paused:
            {
                parent->playback_status().set(media::Player::paused);
                if (previous_state == Engine::State::playing)
                {
                    timeout(4000, true, make_clear_wakelock_functor());
                }
                break;
            }
            default:
                break;
            };

            // Keep track of the previous Engine playback state:
            previous_state = state;
        };
    }

    void request_power_state()
    {
        try
        {
            if (parent->is_video_source())
            {
                if (++display_wakelock_count == 1)
                {
                    auto result = uscreen_session->invoke_method_synchronously<core::UScreen::keepDisplayOn, int>();
                    if (result.is_error())
                        throw std::runtime_error(result.error().print());
                    disp_cookie = result.value();
                    cout << "Requested new display wakelock" << endl;
                }
            }
            else
            {
                if (++system_wakelock_count == 1)
                {
                    auto result = powerd_session->invoke_method_synchronously<core::Powerd::requestSysState, std::string>(sys_lock_name, static_cast<int>(1));
                    if (result.is_error())
                        throw std::runtime_error(result.error().print());
                    sys_cookie = result.value();
                    cout << "Requested new system wakelock" << endl;
                }
            }
        }
        catch(const std::exception& e)
        {
            std::cerr << "Warning: failed to request power state: ";
            std::cerr << e.what() << std::endl;
        }
    }

    void clear_wakelock(const wakelock_clear_t &wakelock)
    {
        cout << __PRETTY_FUNCTION__ << endl;
        try
        {
            switch (wakelock)
            {
                case wakelock_clear_t::WAKELOCK_CLEAR_INACTIVE:
                    break;
                case wakelock_clear_t::WAKELOCK_CLEAR_SYSTEM:
                    // Only actually clear the system wakelock once the count reaches zero
                    if (--system_wakelock_count == 0)
                    {
                        cout << "Clearing system wakelock" << endl;
                        powerd_session->invoke_method_synchronously<core::Powerd::clearSysState, void>(sys_cookie);
                        sys_cookie.clear();
                    }
                    break;
                case wakelock_clear_t::WAKELOCK_CLEAR_DISPLAY:
                    // Only actually clear the display wakelock once the count reaches zero
                    if (--display_wakelock_count == 0)
                    {
                        cout << "Clearing display wakelock" << endl;
                        uscreen_session->invoke_method_synchronously<core::UScreen::removeDisplayOnRequest, void>(disp_cookie);
                        disp_cookie = -1;
                    }
                    break;
                case wakelock_clear_t::WAKELOCK_CLEAR_INVALID:
                default:
                    cerr << "Can't clear invalid wakelock type" << endl;
            }
        }
        catch(const std::exception& e)
        {
            std::cerr << "Warning: failed to clear power state: ";
            std::cerr << e.what() << std::endl;
        }
    }

    wakelock_clear_t current_wakelock_type() const
    {
        return (parent->is_video_source()) ?
            wakelock_clear_t::WAKELOCK_CLEAR_DISPLAY : wakelock_clear_t::WAKELOCK_CLEAR_SYSTEM;
    }

    void clear_wakelocks()
    {
        // Clear both types of wakelocks (display and system)
        if (system_wakelock_count.load() > 0)
        {
            system_wakelock_count = 1;
            clear_wakelock(wakelock_clear_t::WAKELOCK_CLEAR_SYSTEM);
        }
        if (display_wakelock_count.load() > 0)
        {
            display_wakelock_count = 1;
            clear_wakelock(wakelock_clear_t::WAKELOCK_CLEAR_DISPLAY);
        }
    }

    std::function<void()> make_clear_wakelock_functor()
    {
        // Since this functor will be executed on a separate detached thread
        // the execution of the functor may surpass the lifetime of this Private
        // object instance. By keeping a weak_ptr to the private object instance
        // we can check if the object is dead before calling methods on it
        std::weak_ptr<Private> weak_self{shared_from_this()};
        auto wakelock_type = current_wakelock_type();
        return [weak_self, wakelock_type] {
            if (auto self = weak_self.lock())
                self->clear_wakelock(wakelock_type);
        };
    }

    static void on_client_died_cb(void *context)
    {
        if (context)
        {
            Private *p = static_cast<Private*>(context);
            p->on_client_died();
        }
    }

    void on_client_died()
    {
        engine->reset();
    }

    PlayerImplementation* parent;
    std::shared_ptr<Service> service;
    std::shared_ptr<Engine> engine;
    dbus::types::ObjectPath session_path;
    std::shared_ptr<TrackListImplementation> track_list;
    std::shared_ptr<dbus::Object> powerd_session;
    std::shared_ptr<dbus::Object> uscreen_session;
    std::string sys_lock_name;
    int disp_cookie;
    std::string sys_cookie;
    std::atomic<int> system_wakelock_count;
    std::atomic<int> display_wakelock_count;
    Engine::State previous_state;
    PlayerImplementation::PlayerKey key;
    core::Signal<> on_client_disconnected;
    core::Connection engine_state_change_connection;
};

media::PlayerImplementation::PlayerImplementation(
        const std::string& identity,
        const std::shared_ptr<core::dbus::Bus>& bus,
        const std::shared_ptr<core::dbus::Object>& session,
        const std::shared_ptr<Service>& service,
        PlayerKey key)
    : media::PlayerSkeleton
      {
          media::PlayerSkeleton::Configuration
          {
              bus,
              session,
              identity
          }
      },
      d(make_shared<Private>(
            this,
            session->path(),
            service,
            key))
{
    // Initialize default values for Player interface properties
    can_play().set(true);
    can_pause().set(true);
    can_seek().set(true);
    can_go_previous().set(true);
    can_go_next().set(true);
    is_video_source().set(false);
    is_audio_source().set(false);
    is_shuffle().set(true);
    playback_rate().set(1.f);
    playback_status().set(Player::PlaybackStatus::null);
    loop_status().set(Player::LoopStatus::none);
    position().set(0);
    duration().set(0);
    audio_stream_role().set(Player::AudioStreamRole::multimedia);
    d->engine->audio_stream_role().set(Player::AudioStreamRole::multimedia);
    orientation().set(Player::Orientation::rotate0);
    lifetime().set(Player::Lifetime::normal);
    d->engine->lifetime().set(Player::Lifetime::normal);

    // Make sure that the Position property gets updated from the Engine
    // every time the client requests position
    std::function<uint64_t()> position_getter = [this]()
    {
        return d->engine->position().get();
    };
    position().install(position_getter);

    // Make sure that the Duration property gets updated from the Engine
    // every time the client requests duration
    std::function<uint64_t()> duration_getter = [this]()
    {
        return d->engine->duration().get();
    };
    duration().install(duration_getter);

    std::function<bool()> video_type_getter = [this]()
    {
        return d->engine->is_video_source().get();
    };
    is_video_source().install(video_type_getter);

    std::function<bool()> audio_type_getter = [this]()
    {
        return d->engine->is_audio_source().get();
    };
    is_audio_source().install(audio_type_getter);

    // Make sure that the audio_stream_role property gets updated on the Engine side
    // whenever the client side sets the role
    audio_stream_role().changed().connect([this](media::Player::AudioStreamRole new_role)
    {
        d->engine->audio_stream_role().set(new_role);
    });

    // When the value of the orientation Property is changed in the Engine by playbin,
    // update the Player's cached value
    d->engine->orientation().changed().connect([this](const Player::Orientation& o)
    {
        orientation().set(o);
    });

    lifetime().changed().connect([this](media::Player::Lifetime lifetime)
    {
        d->engine->lifetime().set(lifetime);
    });

    d->engine->about_to_finish_signal().connect([this]()
    {
        if (d->track_list->has_next())
        {
            Track::UriType uri = d->track_list->query_uri_for_track(d->track_list->next());
            if (!uri.empty())
                d->parent->open_uri(uri);
        }
    });

    d->engine->client_disconnected_signal().connect([this]()
    {
        // If the client disconnects, make sure both wakelock types
        // are cleared
        d->clear_wakelocks();
        // And tell the outside world that the client has gone away
        d->on_client_disconnected();
    });

    d->engine->seeked_to_signal().connect([this](uint64_t value)
    {
        seeked_to()(value);
    });

    d->engine->end_of_stream_signal().connect([this]()
    {
        end_of_stream()();
    });

    d->engine->playback_status_changed_signal().connect([this](const Player::PlaybackStatus& status)
    {
        playback_status_changed()(status);
    });

    d->engine->video_dimension_changed_signal().connect([this](uint32_t height, uint32_t width)
    {
        uint64_t mask = 0;
        // Left most 32 bits are for height, right most 32 bits are for width
        mask = (static_cast<uint64_t>(height) << 32) | static_cast<uint64_t>(width);
        video_dimension_changed()(mask);
    });

    d->engine->error_signal().connect([this](const Player::Error& e)
    {
        error()(e);
    });
}

media::PlayerImplementation::~PlayerImplementation()
{
    // Install null getters as these properties may be destroyed
    // after the engine has been destroyed since they are owned by the
    // base class.
    std::function<uint64_t()> position_getter = [this]()
    {
        return static_cast<uint64_t>(0);
    };
    position().install(position_getter);

    std::function<uint64_t()> duration_getter = [this]()
    {
        return static_cast<uint64_t>(0);
    };
    duration().install(duration_getter);

    std::function<bool()> video_type_getter = [this]()
    {
        return false;
    };
    is_video_source().install(video_type_getter);

    std::function<bool()> audio_type_getter = [this]()
    {
        return false;
    };
    is_audio_source().install(audio_type_getter);
}

std::shared_ptr<media::TrackList> media::PlayerImplementation::track_list()
{
    return d->track_list;
}

// TODO: Convert this to be a property instead of sync call
media::Player::PlayerKey media::PlayerImplementation::key() const
{
    return d->key;
}

bool media::PlayerImplementation::open_uri(const Track::UriType& uri)
{
    return d->engine->open_resource_for_uri(uri);
}

bool media::PlayerImplementation::open_uri(const Track::UriType& uri, const Player::HeadersType& headers)
{
    return d->engine->open_resource_for_uri(uri, headers);
}

void media::PlayerImplementation::create_video_sink(uint32_t texture_id)
{
    d->engine->create_video_sink(texture_id);
}

media::Player::GLConsumerWrapperHybris media::PlayerImplementation::gl_consumer() const
{
    // This method is client-side only and is simply a no-op for the service side
    return NULL;
}

void media::PlayerImplementation::next()
{
}

void media::PlayerImplementation::previous()
{
}

void media::PlayerImplementation::play()
{
    d->engine->play();
}

void media::PlayerImplementation::pause()
{
    d->engine->pause();
}

void media::PlayerImplementation::stop()
{
    std::cout << __PRETTY_FUNCTION__ << std::endl;
    d->engine->stop();
}

void media::PlayerImplementation::set_frame_available_callback(
    UNUSED FrameAvailableCb cb, UNUSED void *context)
{
    // This method is client-side only and is simply a no-op for the service side
}

void media::PlayerImplementation::set_playback_complete_callback(
    UNUSED PlaybackCompleteCb cb, UNUSED void *context)
{
    // This method is client-side only and is simply a no-op for the service side
}

void media::PlayerImplementation::seek_to(const std::chrono::microseconds& ms)
{
    d->engine->seek_to(ms);
}

const core::Signal<>& media::PlayerImplementation::on_client_disconnected() const
{
    return d->on_client_disconnected;
}
