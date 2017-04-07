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

#ifndef LIBFREESRP_FREESRP_IMPL_HPP
#define LIBFREESRP_FREESRP_IMPL_HPP

#include <freesrp.hpp>
#include "readerwriterqueue/readerwriterqueue.h"

#include <libusb.h>

namespace FreeSRP
{
    class FreeSRP::impl
    {
    public:
        impl();
        ~impl();

        bool fpga_loaded();
        fpga_status load_fpga(std::string filename);

        std::shared_ptr<rx_tx_buf> rx();
        void tx(std::shared_ptr<rx_tx_buf> buf);

        void start_rx(std::function<void(const std::vector<sample> &)> rx_callback = {});
        void stop_rx();

        void start_tx(std::function<void(std::vector<sample> &)> tx_callback = {});
        void stop_tx();

        unsigned long available_rx_samples();
        bool get_rx_sample(sample &s);

        bool submit_tx_sample(sample &s);

        command make_command(command_id id, double param) const;
        response send_cmd(command c) const;

        freesrp_version version();
    private:
        void run_rx_tx();

        libusb_transfer *create_rx_transfer(libusb_transfer_cb_fn callback);
        libusb_transfer *create_tx_transfer(libusb_transfer_cb_fn callback);

        static void rx_callback(libusb_transfer *transfer);
        static void tx_callback(libusb_transfer *transfer);

        static int fill_tx_transfer(libusb_transfer *transfer);

        static void decode_rx_transfer(unsigned char *buffer, int actual_length, std::vector<sample> &destination);

        libusb_context *_ctx = nullptr;
        libusb_device_handle *_freesrp_handle = nullptr;

        std::string _fx3_fw_version;

        std::atomic<bool> _run_rx_tx{false};
        std::unique_ptr<std::thread> _rx_tx_worker;

        std::array<libusb_transfer *, FREESRP_RX_TX_TRANSFER_QUEUE_SIZE> _rx_transfers;
        std::array<libusb_transfer *, FREESRP_RX_TX_TRANSFER_QUEUE_SIZE> _tx_transfers;

        static std::function<void(const std::vector<sample> &)> _rx_custom_callback;
        static std::function<void(std::vector<sample> &)> _tx_custom_callback;

        static std::vector<sample> _rx_decoder_buf;
        static std::vector<sample> _tx_encoder_buf;

        static moodycamel::ReaderWriterQueue<sample> _rx_buf;
        static moodycamel::ReaderWriterQueue<sample> _tx_buf;
    };
}

#endif
