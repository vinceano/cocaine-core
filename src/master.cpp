/*
    Copyright (c) 2011-2012 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2012 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>. 
*/

#include <boost/format.hpp>

#include "cocaine/master.hpp"

#include "cocaine/context.hpp"
#include "cocaine/job.hpp"
#include "cocaine/logging.hpp"
#include "cocaine/manifest.hpp"
#include "cocaine/profile.hpp"

using namespace cocaine;
using namespace cocaine::engine;
using namespace cocaine::engine::slave;

master_t::master_t(context_t& context, ev::loop_ref& loop, const manifest_t& manifest, const profile_t& profile):
    m_context(context),
    m_log(context.log(
        (boost::format("app/%1%")
            % manifest.name
        ).str()
    )),
    m_loop(loop),
    m_manifest(manifest),
    m_profile(profile),
    m_heartbeat_timer(loop)
{
    initiate();
    
    // NOTE: Initialization heartbeat can be different.
    m_heartbeat_timer.set<master_t, &master_t::on_timeout>(this);
    m_heartbeat_timer.start(m_profile.startup_timeout);

    api::category_traits<api::isolate_t>::ptr_type isolate = m_context.get<api::isolate_t>(
        m_profile.isolate.type,
        api::category_traits<api::isolate_t>::args_type(
            m_manifest.name,
            m_profile.isolate.args
        )
    );

    std::map<std::string, std::string> args;

    args["--configuration"] = m_context.config.config_path;
    args["--slave:app"] = m_manifest.name;
    args["--slave:profile"] = m_profile.name;
    args["--slave:uuid"] = m_id.string();

    COCAINE_LOG_DEBUG(m_log, "spawning slave %s", m_id.string());

    m_handle = isolate->spawn(m_manifest.slave, args);
}

master_t::~master_t() {
    m_heartbeat_timer.stop();
    
    // TEST: Make sure that the slave is really dead.
    BOOST_ASSERT(state_downcast<const dead*>() != 0);

    terminate();
}

const unique_id_t&
master_t::id() const {
    return m_id;
}

bool master_t::operator==(const master_t& other) const {
    return m_id == other.m_id;
}

void master_t::on_initialize(const events::heartbeat& event) {
#if EV_VERSION_MAJOR == 3 && EV_VERSION_MINOR == 8
    COCAINE_LOG_DEBUG(
        m_log,
        "slave %s came alive in %.03f seconds",
        m_id.string(),
        m_profile.startup_timeout - ev_timer_remaining(
            m_loop,
            static_cast<ev_timer*>(&m_heartbeat_timer)
        )
    );
#endif

    on_heartbeat(event);
}

void master_t::on_heartbeat(const events::heartbeat& event) {
    m_heartbeat_timer.stop();
    
    const busy * state = state_downcast<const busy*>();
    float timeout = m_profile.heartbeat_timeout;

    if(state && state->job()->policy.timeout > 0.0f) {
        timeout = state->job()->policy.timeout;
    }
           
    COCAINE_LOG_DEBUG(
        m_log,
        "resetting slave %s heartbeat timeout to %.02f seconds",
        m_id.string(),
        timeout
    );

    m_heartbeat_timer.start(timeout);
}

void master_t::on_terminate(const events::terminate& event) {
    COCAINE_LOG_DEBUG(m_log, "reaping slave %s", m_id.string());
    m_handle->terminate();
    m_handle.reset();
}

void master_t::on_timeout(ev::timer&, int) {
    COCAINE_LOG_ERROR(
        m_log,
        "slave %s didn't respond in a timely fashion",
        m_id.string()
    );
    
    const busy * state = state_downcast<const busy*>();

    if(state) {
        COCAINE_LOG_DEBUG(
            m_log,
            "slave %s dropping '%s' job due to a timeout",
            m_id.string(),
            state->job()->event
        );

        state->job()->process(
            events::error(
                timeout_error, 
                "the job has timed out"
            )
        );

        state->job()->process(events::choke());
    }
    
    process_event(events::terminate());
}

alive::~alive() {
    if(job) {
        job->process(
            events::error(
                server_error,
                "the job is being cancelled"
            )
        );

        job->process(events::choke());
        job.reset();
    }
}

void alive::on_invoke(const events::invoke& event) {
    // TEST: Ensure that no job is being lost here.
    BOOST_ASSERT(!job && event.job);

    COCAINE_LOG_DEBUG(
        context<master_t>().m_log,
        "job '%s' assigned to slave %s",
        event.job->event,
        context<master_t>().m_id.string()
    );

    job = event.job;
    job->process(event);    
    
    // Reset the heartbeat timer.    
    post_event(events::heartbeat());
}

void alive::on_choke(const events::choke& event) {
    // TEST: Ensure that the job is in fact here.
    BOOST_ASSERT(job);

    COCAINE_LOG_DEBUG(
        context<master_t>().m_log,
        "job '%s' completed by slave %s",
        job->event,
        context<master_t>().m_id.string()
    );
    
    job->process(event);
    job.reset();
    
    // Reset the heartbeat timer.    
    post_event(events::heartbeat());
}

void busy::on_chunk(const events::chunk& event) {
    job()->process(event);
    
    // Reset the heartbeat timer.    
    post_event(events::heartbeat());
}

void busy::on_error(const events::error& event) {
    job()->process(event);
    
    // Reset the heartbeat timer.    
    post_event(events::heartbeat());
}
