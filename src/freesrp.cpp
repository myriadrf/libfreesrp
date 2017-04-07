/*
 * Copyright 2017 by Lukas Lao Beyer <lukas@electronics.kitchen>
 *
 * This file is part of libfreesrp.
 *
 * libfreesrp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * libfreesrp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with libfreesrp.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <freesrp.hpp>
#include "freesrp_impl.hpp"

namespace FreeSRP {

FreeSRP::FreeSRP::FreeSRP()
{
    _impl.reset(new impl());
}

FreeSRP::~FreeSRP() = default;

bool FreeSRP::fpga_loaded() { return _impl->fpga_loaded(); }
fpga_status FreeSRP::load_fpga(std::string filename) { return _impl->load_fpga(filename); }

void FreeSRP::start_rx(std::function<void(const std::vector<sample> &)> rx_callback) { _impl->start_rx(rx_callback); }
void FreeSRP::stop_rx() { _impl->stop_rx(); }

void FreeSRP::start_tx(std::function<void(std::vector<sample> &)> tx_callback) { _impl->start_tx(tx_callback); }
void FreeSRP::stop_tx() { _impl->stop_tx(); }

unsigned long FreeSRP::available_rx_samples() {return _impl->available_rx_samples(); }
bool FreeSRP::get_rx_sample(sample &s) { return _impl->get_rx_sample(s); }

bool FreeSRP::submit_tx_sample(sample &s) { return _impl->submit_tx_sample(s); }

command FreeSRP::make_command(command_id id, double param) const { return _impl->make_command(id, param); }
response FreeSRP::send_cmd(command c) const { return _impl->send_cmd(c); }

freesrp_version FreeSRP::version() { return _impl->version(); }

}
