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

#ifndef LIBFREESRP_FREESRP_HPP
#define LIBFREESRP_FREESRP_HPP

#include <string>
#include <array>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <cstdint>

#define FREESRP_VENDOR_ID 0xe1ec
#define FREESRP_PRODUCT_ID 0xf5d0
#define FX3_VENDOR_ID 0x04b4
#define FX3_PRODUCT_ID 0x00f3

#define FREESRP_USB_TIMEOUT 4000

#define FREESRP_FPGA_UART_OUT 0x01
#define FREESRP_FPGA_UART_IN 0x81
#define FREESRP_TX_OUT 0x02
#define FREESRP_RX_IN 0x82

#define FREESRP_USB_CTRL_SIZE 64
#define FREESRP_UART_BUF_SIZE 16

#define FREESRP_BYTES_PER_SAMPLE 4

#define FREESRP_RX_TX_BUF_SIZE 1024 * 64
#define FREESRP_TX_BUF_SIZE 1024 * 32
#define FREESRP_RX_TX_TRANSFER_QUEUE_SIZE 128

#define FREESRP_RX_TX_QUEUE_SIZE FREESRP_RX_TX_BUF_SIZE * FREESRP_RX_TX_TRANSFER_QUEUE_SIZE

// FreeSRP vendor commands
#define FREESRP_GET_VERSION_REQ 0
#define FREESRP_FPGA_CONFIG_LOAD 0xB2
#define FREESRP_FPGA_CONFIG_STATUS 0xB1
#define FREESRP_FPGA_CONFIG_FINISH 0xB3


namespace FreeSRP
{
    struct rx_tx_buf
    {
        unsigned int size;
        std::array<unsigned char, FREESRP_RX_TX_BUF_SIZE> data;
    };

    struct sample
    {
        int16_t i;
        int16_t q;
    };

    typedef std::array<unsigned char, FREESRP_UART_BUF_SIZE> cmd_buf;

    class ConnectionError: public std::runtime_error
    {
    public:
        ConnectionError(std::string const& msg) : std::runtime_error(msg)
        {}
    };

    enum command_id
    {
        GET_REGISTER, 			/* 00: [UINT16_T] -> [UINT8_T] */
        GET_TX_LO_FREQ, 		/* 01:            -> [UINT64_T] */
        SET_TX_LO_FREQ, 		/* 02: [UINT64_T] -> [UINT64_T] */
        GET_TX_SAMP_FREQ, 		/* 03:            -> [UINT32_T] */
        SET_TX_SAMP_FREQ, 		/* 04: [UINT32_T] -> [UINT32_T] */
        GET_TX_RF_BANDWIDTH, 	/* 05:            -> [UINT32_T] */
        SET_TX_RF_BANDWIDTH, 	/* 06: [UINT32_T] -> [UINT32_T] */
        GET_TX_ATTENUATION, 	/* 07:            -> [UINT32_T] */
        SET_TX_ATTENUATION, 	/* 08: [UINT32_T] -> [UINT32_T] */
        GET_TX_FIR_EN, 			/* 09:            -> [UINT8_T] */
        SET_TX_FIR_EN, 			/* 10: [UINT8_T]  -> [UINT8_T] */
        GET_RX_LO_FREQ, 		/* 11:            -> [UINT64_T] */
        SET_RX_LO_FREQ, 		/* 12: [UINT64_T] -> [UINT64_T] */
        GET_RX_SAMP_FREQ, 		/* 13:            -> [UINT32_T] */
        SET_RX_SAMP_FREQ, 		/* 14: [UINT32_T] -> [UINT32_T] */
        GET_RX_RF_BANDWIDTH, 	/* 15:            -> [UINT32_T] */
        SET_RX_RF_BANDWIDTH, 	/* 16: [UINT32_T] -> [UINT32_T] */
        GET_RX_GC_MODE, 		/* 17:            -> [UINT8_T] */
        SET_RX_GC_MODE, 		/* 18: [UINT8_T]  -> [UINT8_T] */
        GET_RX_RF_GAIN,			/* 19:            -> [INT32_T] */
        SET_RX_RF_GAIN, 		/* 20: [INT32_T]  -> [INT32_T] */
        GET_RX_FIR_EN, 			/* 21:            -> [UINT8_T] */
        SET_RX_FIR_EN, 			/* 22: [UINT8_T]  -> [UINT8_T] */
        SET_DATAPATH_EN, 		/* 23: [UINT8_T]  -> [UINT8_T] */
        GET_FPGA_VERSION,       /* 24:            -> [UINT64_T] */
        SET_LOOPBACK_EN,        /* 25: [UINT8_T]  -> [UINT8_T] */
    };

    enum command_err
    {
        CMD_OK = 0,
        CMD_INVALID_PARAM,
        CMD_ENSM_ERR
    };

    enum gainctrl_mode
    {
        RF_GAIN_MGC = 0,        // Manual
        RF_GAIN_FASTATTACK_AGC, // AGC: Fast attack
        RF_GAIN_SLOWATTACK_AGC, // AGC: Slow attack
        RF_GAIN_HYBRID_AGC      // AGC: Hybrid
    };

    enum fpga_status
    {
        FPGA_CONFIG_DONE = 0,
        FPGA_CONFIG_ERROR,
        FPGA_CONFIG_SKIPPED
    };

    struct freesrp_version
    {
        std::string fx3;
        std::string fpga;

        friend std::ostream &operator<<(std::ostream &o, const freesrp_version v)
        {
            return o << "FX3 v" << v.fx3 << ", FPGA v" << v.fpga;
        }
    };

    struct command
    {
        command_id cmd;
        uint64_t param;

        friend std::ostream &operator<<(std::ostream &o, const command cmd)
        {
            return o << "command ID: " << cmd.cmd << "; parameter: " << cmd.param;
        }
    };

    struct response
    {
        command_id cmd;
        uint64_t param;
        command_err error;

        friend std::ostream &operator<<(std::ostream &o, const response res)
        {
            if(res.error == CMD_OK)
            {
                o << "command ID: " << res.cmd << "; parameter: " << res.param;
            }
            else
            {
                o << "command ID: " << res.cmd << "; error code: " << res.error;
            }
            return o;
        }
    };

    class FreeSRP
    {
	class impl;
	std::unique_ptr<impl> _impl;
	
    public:

	//! FreeSRP constructor.
	/*!
	 * Will attempt to find and connect to a FreeSRP and throw a ConnectionError if no FreeSRP
         * can be found or there is an error while communicating with it.
	 * \param serial_number: If specified, will try to connect to a FreeSRP that that contains
         *                       serial_number as a substring of or matches its serial number
	 */
        FreeSRP(std::string serial_number = "");

        ~FreeSRP();

	//! List serial numbers of all connected FreeSRPs
	/*!
	 * \return An std::vector containing the serial numbers of all available devices
	 */
	static std::vector<std::string> list_connected();

	//! Check if the FPGA has been loaded.
	/*!
         * \return true if the FPGA is configured, false if configuration is still needed.
         */
        bool fpga_loaded();

	//! Load the FPGA with the specified bitstream.
	/*!
         * \param filename: The filename of the bitstream to load onto the FPGA.
         * \returns An fpga_status value indicating wether the FPGA was successfully configured.
         */
        fpga_status load_fpga(std::string filename);

	//! Start receiving samples.
	/*!
	 * \param rx_callback: Optionally, specify a function to be called once a new sample buffer is available.
         */
        void start_rx(std::function<void(const std::vector<sample> &)> rx_callback = {});

	//! Stop receiving samples.
	/*!
	 *
	 */
        void stop_rx();

	//! Start transmitting samples.
	/*!
	 * \param tx_callback: Optionaly, specify a function to be called once a new sample buffer is available.
         */
        void start_tx(std::function<void(std::vector<sample> &)> tx_callback = {});

	//! Stop transmitting samples.
        void stop_tx();

	//! Check how many received samples are available.
	/*!
	 * Note: samples will not be written to the main buffer if a callback is specified in start_rx.
	 * \returns Number of samples available to read from the buffer.
	 */
        unsigned long available_rx_samples();

	//! Get sample from queue.
	/*
	 * Note: samples will only be available if no callback if specified in start_rx.
	 * \param s: A reference to the sample to be read.
         * \returns: true if a sample was read, false if the queue is empty.
	 */
        bool get_rx_sample(sample &s);

	//! Add a sample to the transmitter queue.
	/*!
	 * \param s: the sample to add to the transmitter queue
         * \returns: true if the sample was successfully added to the queue, false if the queue is full.
	 */
        bool submit_tx_sample(sample &s);

	//! Helper function to generate a FreeSRP::command
	/*!
         * \param command_id: the ID of the desired command
	 * \param param: Value of the parameter. Meaning varies depending on the command associated with this value.
	 * \returns The command.
	 */
        command make_command(command_id id, double param) const;

	//! Send a command to the FreeSRP
	/*!
	 * Note: this call will block until a response from the FreeSRP is received back.
	 * \param c: The command to send (see also make_command)
	 * \returns The response from the FreeSRP
	 */
        response send_cmd(command c) const;

	//! Get version information about the FreeSRP
	/*!
	 * \returns Version information the FreeSRP responded with.
	 */
        freesrp_version version();
    };

    namespace Util
    {
        //! This will look for an FX3 in bootloader mode.
	/*!
         * \param upload_firmware: If 'false', this function will return 'true' if an unprogrammed FX3 is found.
         *                          If 'true', it will attempt to program the FX3 with the specified firmware.
         * \param filename: Path to the image file to program the FX3 with.
         */
        bool find_fx3(bool upload_firmware=false, std::string filename="");
    };
}

#endif
