/*
    Copyright (c) 2011-2014 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2014 Other contributors as noted in the AUTHORS file.

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

#ifndef COCAINE_PREDEFINE_CLUSTER_HPP
#define COCAINE_PREDEFINE_CLUSTER_HPP

#include "cocaine/api/cluster.hpp"

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace cocaine { namespace cluster {

class predefine_cfg_t {
public:
    std::map<std::string, std::vector<boost::asio::ip::tcp::endpoint>> endpoints;
    boost::asio::deadline_timer::duration_type interval;
};

class predefine_t:
    public api::cluster_t
{
    typedef api::cluster_t::interface interface;

    const std::unique_ptr<logging::log_t> m_log;

    // Interoperability with the locator service.
    interface& m_locator;

    // Component config.
    const predefine_cfg_t m_cfg;

    // Simply try linking the whole predefined list every timer tick.
    boost::asio::deadline_timer m_timer;

public:
    predefine_t(context_t& context, interface& locator, const std::string& name, const dynamic_t& args);

    virtual
   ~predefine_t();

private:
    void
    on_announce(const boost::system::error_code& ec);
};

}} // namespace cocaine::cluster

#endif
