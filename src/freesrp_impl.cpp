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

#include "freesrp_impl.hpp"
#include <freesrp.hpp>

#include <cstring>
#include <fstream>

#define FREESRP_SERIAL_DSCR_INDEX 3
#define MAX_SERIAL_LENGTH 256


moodycamel::ReaderWriterQueue<FreeSRP::sample> FreeSRP::FreeSRP::impl::_rx_buf(FREESRP_RX_TX_QUEUE_SIZE);
moodycamel::ReaderWriterQueue<FreeSRP::sample> FreeSRP::FreeSRP::impl::_tx_buf(FREESRP_RX_TX_QUEUE_SIZE);
std::vector<FreeSRP::sample> FreeSRP::FreeSRP::impl::_rx_decoder_buf(FREESRP_RX_TX_BUF_SIZE / FREESRP_BYTES_PER_SAMPLE);
std::function<void(const std::vector<FreeSRP::sample> &)> FreeSRP::FreeSRP::impl::_rx_custom_callback;
std::vector<FreeSRP::sample> FreeSRP::FreeSRP::impl::_tx_encoder_buf(FREESRP_RX_TX_BUF_SIZE / FREESRP_BYTES_PER_SAMPLE);
std::function<void(std::vector<FreeSRP::sample> &)> FreeSRP::FreeSRP::impl::_tx_custom_callback;

FreeSRP::FreeSRP::impl::impl(std::string serial_number)
{
    libusb_device **devs;

    int ret = libusb_init(&_ctx);
    if(ret < 0)
    {
        throw ConnectionError("libusb init error: error " + std::to_string(ret));
    }

    // Set verbosity level
    libusb_set_debug(_ctx, 3);

    // Retrieve device list
    int num_devs = (int) libusb_get_device_list(_ctx, &devs);
    if(num_devs < 0)
    {
        throw ConnectionError("libusb device list retrieval error");
    }

    // Find FreeSRP device
    bool no_match = false;
    
    for(int i = 0; i < num_devs; i++)
    {
        libusb_device_descriptor desc;
        int ret = libusb_get_device_descriptor(devs[i], &desc);
        if(ret < 0)
        {
            throw ConnectionError("libusb error getting device descriptor: error " + std::to_string(ret));
        }

        if(desc.idVendor == FREESRP_VENDOR_ID && desc.idProduct == FREESRP_PRODUCT_ID)
        {
            int ret = libusb_open(devs[i], &_freesrp_handle);
            if(ret != 0)
            {
                throw ConnectionError("libusb could not open found FreeSRP USB device: error " + std::to_string(ret));
            }

	    // Check if correct serial number
	    char serial_num_buf[MAX_SERIAL_LENGTH];
	    ret = libusb_get_string_descriptor_ascii(_freesrp_handle, FREESRP_SERIAL_DSCR_INDEX, (unsigned char *) serial_num_buf, MAX_SERIAL_LENGTH);
	    if(ret < 0)
	    {
		libusb_close(_freesrp_handle);
		_freesrp_handle = nullptr;
		throw ConnectionError("libusb could not read FreeSRP serial number: error " + std::to_string(ret));
	    }
	    else
	    {
		std::string dev_serial = std::string(serial_num_buf);

		if(dev_serial.find(serial_number) != std::string::npos)
		{
		    // Found!
		    break;
		}
		else
		{
		    no_match = true;
		    libusb_close(_freesrp_handle);
		    _freesrp_handle = nullptr;
		}
	    }
        }
    }

    if(no_match && _freesrp_handle == nullptr)
    {
	throw ConnectionError("FreeSRP device(s) were found, but did not match specified serial number"); 
    }
    
    if(_freesrp_handle == nullptr)
    {
        throw ConnectionError("no FreeSRP device found");
    }

    // Free the list, unref the devices in it
    libusb_free_device_list(devs, 1);

    // Found a FreeSRP device and opened it. Now claim its interface (ID 0).
    ret = libusb_claim_interface(_freesrp_handle, 0);
    if(ret < 0)
    {
        throw ConnectionError("could not claim FreeSRP interface");
    }

    // Request FreeSRP version number
    std::array<unsigned char, FREESRP_USB_CTRL_SIZE> data{};
    ret = libusb_control_transfer(_freesrp_handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN, FREESRP_GET_VERSION_REQ, 0, 0, data.data(), (uint16_t) data.size(), FREESRP_USB_TIMEOUT);
    if(ret < 0)
    {
        throw ConnectionError("FreeSRP not responding: error " + std::to_string(ret));
    }
    int transferred = ret;
    _fx3_fw_version = std::string(std::begin(data), std::begin(data) + transferred);

    for(int i = 0; i < _rx_transfers.size(); i++)
    {
        _rx_transfers[i] = create_rx_transfer(&FreeSRP::impl::rx_callback);
    }

    for(int i = 0; i < _tx_transfers.size(); i++)
    {
        _tx_transfers[i] = create_tx_transfer(&FreeSRP::impl::tx_callback);
    }

    // Start libusb event handling
    _run_rx_tx.store(true);

    _rx_tx_worker.reset(new std::thread([this]() {
        run_rx_tx();
    }));
}

FreeSRP::FreeSRP::impl::~impl()
{
    // TODO: Properly stop all active transfers
    stop_rx();
    stop_tx();

    if(_freesrp_handle != nullptr)
    {
        libusb_release_interface(_freesrp_handle, 0);

        _run_rx_tx.store(false);

        // This will cause libusb_handle_events() in run_rx_tx() to return once
        libusb_close(_freesrp_handle);

        // libusb_handle_events should have returned and the thread can now be joined
        if(_rx_tx_worker != nullptr)
        {
            _rx_tx_worker->join();
        }
    }

    for(libusb_transfer *transfer : _rx_transfers)
    {
        libusb_free_transfer(transfer);
    }

    for(libusb_transfer *transfer : _tx_transfers)
    {
        libusb_free_transfer(transfer);
    }

    if(_ctx != nullptr)
    {
        libusb_exit(_ctx); // close the session
    }
}

std::vector<std::string> FreeSRP::FreeSRP::impl::list_connected()
{
    libusb_device **devs;
    libusb_context *list_ctx;

    std::vector<std::string> list;
    
    int ret = libusb_init(&list_ctx);
    if(ret < 0)
    {
        throw ConnectionError("libusb init error: error " + std::to_string(ret));
    }

    // Set verbosity level
    libusb_set_debug(list_ctx, 3);

    // Retrieve device list
    int num_devs = (int) libusb_get_device_list(list_ctx, &devs);
    if(num_devs < 0)
    {
        throw ConnectionError("libusb device list retrieval error");
    }

    // Find all FreeSRP devices
    for(int i = 0; i < num_devs; i++)
    {
        libusb_device_descriptor desc;
        int ret = libusb_get_device_descriptor(devs[i], &desc);
        if(ret < 0)
        {
            throw ConnectionError("libusb error getting device descriptor: error " + std::to_string(ret));
        }

        if(desc.idVendor == FREESRP_VENDOR_ID && desc.idProduct == FREESRP_PRODUCT_ID)
        {
	    libusb_device_handle *temp_handle;
            int ret = libusb_open(devs[i], &temp_handle);
            if(ret != 0)
            {
                throw ConnectionError("libusb could not open found FreeSRP USB device: error " + std::to_string(ret));
            }

	    char serial_num_buf[MAX_SERIAL_LENGTH];
	    ret = libusb_get_string_descriptor_ascii(temp_handle, FREESRP_SERIAL_DSCR_INDEX, (unsigned char *) serial_num_buf, MAX_SERIAL_LENGTH);
	    if(ret < 0)
	    {
		throw ConnectionError("libusb could not read FreeSRP serial number: error " + std::to_string(ret));
	    }
	    else
	    {
		list.push_back(std::string(serial_num_buf));
	    }
	    
	    libusb_close(temp_handle);
        }
    }

    libusb_exit(list_ctx);

    return list;
}

bool FreeSRP::FreeSRP::impl::fpga_loaded()
{
    std::array<unsigned char, FREESRP_USB_CTRL_SIZE> stat_buf{};
    int ret = libusb_control_transfer(_freesrp_handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN, FREESRP_FPGA_CONFIG_STATUS, 0, 1, stat_buf.data(), (uint16_t) stat_buf.size(), FREESRP_USB_TIMEOUT);
    if(ret < 0)
    {
        throw ConnectionError("FreeSRP not responding: error " + std::to_string(ret));
    }
    int transferred = ret;
    bool fpga_load_success = (bool) stat_buf[0];
    return fpga_load_success;
}

FreeSRP::fpga_status FreeSRP::FreeSRP::impl::load_fpga(std::string filename)
{
    if(fpga_loaded())
    {
        // FreeSRP does not yet handle reloading the FPGA multiple times
        return FPGA_CONFIG_SKIPPED;
    }

    // Open ifstream for FPGA config file
    std::ifstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(filename, std::ios::binary | std::ios::ate);
    std::streamsize size = stream.tellg();
    stream.seekg(0, std::ios::beg);

    // Read config into vector
    std::vector<char> configfile_buffer((size_t) size);
    if(!stream.read(configfile_buffer.data(), size))
    {
        // Could not load config file into buffer
        throw std::runtime_error("load_fpga error: Could not load FPGA config file into buffer!");
    }

    // Send FPGA configuration command and tell FreeSRP the configuration file length
    std::array<unsigned char, 16> data{};
    uint32_t configfile_length = static_cast<uint32_t>(size);
    memcpy(data.data(), &configfile_length, sizeof(configfile_length));

    int ret = libusb_control_transfer(_freesrp_handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT, FREESRP_FPGA_CONFIG_LOAD, 0, 1, data.data(), (uint16_t) data.size(), FREESRP_USB_TIMEOUT);
    if(ret < 0)
    {
        throw ConnectionError("FreeSRP not responding: error " + std::to_string(ret));
    }

    // Transfer the configuration
    int transferred;
    ret = libusb_bulk_transfer(_freesrp_handle, FREESRP_TX_OUT, (unsigned char *) configfile_buffer.data(), (int) configfile_buffer.size(), &transferred, 12000);
    if(ret < 0)
    {
        throw ConnectionError("BULK OUT transfer of FPGA configuration failed! error " + std::to_string(ret));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Get FreeSRP FPGA configuration status and switch to normal operation
    if(fpga_loaded())
    {
        std::array<unsigned char, FREESRP_USB_CTRL_SIZE> finish_buf{};
        ret = libusb_control_transfer(_freesrp_handle, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN, FREESRP_FPGA_CONFIG_FINISH, 0, 1, finish_buf.data(), (uint16_t) finish_buf.size(), FREESRP_USB_TIMEOUT);
        if(ret < 0)
        {
            throw ConnectionError("FreeSRP not responding: error " + std::to_string(ret));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        if(finish_buf[0])
        {
            return FPGA_CONFIG_DONE;
        }
    }
    else
    {
        return FPGA_CONFIG_ERROR;
    }
}

std::shared_ptr<FreeSRP::rx_tx_buf> FreeSRP::FreeSRP::impl::rx()
{
    int transferred;
    std::shared_ptr<rx_tx_buf> rx_buf = std::make_shared<rx_tx_buf>();
    int ret = libusb_bulk_transfer(_freesrp_handle, FREESRP_RX_IN, rx_buf->data.data(), (int) rx_buf->data.size(), &transferred, FREESRP_USB_TIMEOUT);
    if(ret < 0)
    {
        throw ConnectionError("BULK IN transfer from RX endpoint failed! error " + std::to_string(ret));
    }

    rx_buf->size = (unsigned int) transferred;

    return rx_buf;
}

void FreeSRP::FreeSRP::impl::tx(std::shared_ptr<rx_tx_buf> rx_data)
{
    int transferred;
    int ret = libusb_bulk_transfer(_freesrp_handle, FREESRP_TX_OUT, rx_data->data.data(), (int) rx_data->size, &transferred, FREESRP_USB_TIMEOUT);
    if(ret < 0)
    {
        throw ConnectionError("BULK OUT transfer to TX endpoint failed! error " + std::to_string(ret));
    }
    if(transferred != rx_data->size)
    {
        throw ConnectionError("Wrong amount of data transferred! Available: " + std::to_string(rx_data->size) + "; transferred: " + std::to_string(transferred));
    }
}

libusb_transfer *FreeSRP::FreeSRP::impl::create_rx_transfer(libusb_transfer_cb_fn callback)
{
    libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *buf = new unsigned char[FREESRP_RX_TX_BUF_SIZE];
    libusb_fill_bulk_transfer(transfer, _freesrp_handle, FREESRP_RX_IN, buf, FREESRP_RX_TX_BUF_SIZE, callback, nullptr, FREESRP_USB_TIMEOUT);

    return transfer;
}

libusb_transfer *FreeSRP::FreeSRP::impl::create_tx_transfer(libusb_transfer_cb_fn callback)
{
    libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *buf = new unsigned char[FREESRP_TX_BUF_SIZE];
    libusb_fill_bulk_transfer(transfer, _freesrp_handle, FREESRP_TX_OUT, buf, FREESRP_TX_BUF_SIZE, callback, nullptr, FREESRP_USB_TIMEOUT);
    //TODO: transfer size
    return transfer;
}

void FreeSRP::FreeSRP::impl::rx_callback(libusb_transfer *transfer)
{
    if(transfer->status == LIBUSB_TRANSFER_COMPLETED)
    {
        // Transfer succeeded

        // Decode samples from transfer buffer into _rx_decoder_buf
        decode_rx_transfer(transfer->buffer, transfer->actual_length, _rx_decoder_buf);

        if(_rx_custom_callback)
        {
            // Run the callback function
            _rx_custom_callback(_rx_decoder_buf);
        }
        else
        {
            // No callback function specified, add samples to queue
            for(sample s : _rx_decoder_buf)
            {
                bool success = _rx_buf.try_enqueue(s);
                if(!success)
                {
                    // TODO: overflow! handle this
                }
            }
        }
    }
    else
    {
        // TODO: Handle error
    }

    // Resubmit the transfer
    if(transfer->status != LIBUSB_TRANSFER_CANCELLED)
    {
        int ret = libusb_submit_transfer(transfer);

        if(ret < 0)
        {
            // TODO: Handle error
        }
    }
}

void FreeSRP::FreeSRP::impl::tx_callback(libusb_transfer* transfer)
{
    if(transfer->status == LIBUSB_TRANSFER_COMPLETED)
    {
        // Success
        if(transfer->actual_length != transfer->length)
        {
            std::cout << "actual length != length: " << transfer->actual_length << "; " << transfer->length << std::endl;
        }
    }
    else
    {
        // TODO: Handle error
        if(transfer->status != LIBUSB_TRANSFER_CANCELLED)
        {
            std::cerr << "transfer error with status " << transfer->status << std::endl;
        }
    }

    // Resubmit the transfer with new data
    if(transfer->status != LIBUSB_TRANSFER_CANCELLED)
    {
        fill_tx_transfer(transfer);
        int ret = libusb_submit_transfer(transfer);

        if(ret < 0)
        {
            // TODO: Handle error
            std::cerr << "transfer submission error with status " << transfer->status << std::endl;
        }
    }
}

void FreeSRP::FreeSRP::impl::start_rx(std::function<void(const std::vector<sample> &)> rx_callback)
{
    _rx_custom_callback = rx_callback;

    for(libusb_transfer *transfer: _rx_transfers)
    {
        int ret = libusb_submit_transfer(transfer);

        if(ret < 0)
        {
            throw ConnectionError("Could not submit RX transfer. libusb error: " + std::to_string(ret));
        }
    }
}

void FreeSRP::FreeSRP::impl::stop_rx()
{
    for(libusb_transfer *transfer: _rx_transfers)
    {
        int ret = libusb_cancel_transfer(transfer);
        if(ret == LIBUSB_ERROR_NOT_FOUND || ret == 0)
        {
            // Transfer cancelled
        }
        else
        {
            // Error
            throw ConnectionError("Could not cancel RX transfer. libusb error: " + std::to_string(ret));
        }
    }
}

void FreeSRP::FreeSRP::impl::start_tx(std::function<void(std::vector<sample> &)> tx_callback)
{
    _tx_custom_callback = tx_callback;

    // Fill the tx buffer with empty samples
    sample empty_sample{0, 0};
    while(_tx_buf.try_enqueue(empty_sample)) {}

    for(libusb_transfer *transfer: _tx_transfers)
    {
        fill_tx_transfer(transfer);
        int ret = libusb_submit_transfer(transfer);

        if(ret < 0)
        {
            throw ConnectionError("Could not submit TX transfer. libusb error: " + std::to_string(ret));
        }
    }
}

void FreeSRP::FreeSRP::impl::stop_tx()
{
    for(libusb_transfer *transfer: _tx_transfers)
    {
        int ret = libusb_cancel_transfer(transfer);
        if(ret == LIBUSB_ERROR_NOT_FOUND || ret == 0)
        {
            // Transfer cancelled
        }
        else
        {
            // Error
            throw ConnectionError("Could not cancel TX transfer. libusb error: " + std::to_string(ret));
        }
    }
}

int FreeSRP::FreeSRP::impl::fill_tx_transfer(libusb_transfer* transfer)
{
    // Fill the transfer buffer with available samples
    transfer->length = FREESRP_TX_BUF_SIZE;

    _tx_encoder_buf.resize(transfer->length/FREESRP_BYTES_PER_SAMPLE);

    if(_tx_custom_callback)
    {
        _tx_custom_callback(_tx_encoder_buf);
    }
    else
    {
        for(sample &s : _tx_encoder_buf)
        {
            int success = _tx_buf.try_dequeue(s);
            if(!success)
            {
                // TODO: Notify of this? Do something else?
                // No data available, fill with zeros
                s.i = 0;
                s.q = 0;
            }
        }
    }

    for(int i = 0; i < transfer->length; i+=FREESRP_BYTES_PER_SAMPLE)
    {
        // SKIP THIS: Convert -1.0 to 1.0 float sample value to signed 16-bit int with range -2048 to 2048
        int16_t signed_i = _tx_encoder_buf[i/FREESRP_BYTES_PER_SAMPLE].i;
        int16_t signed_q = _tx_encoder_buf[i/FREESRP_BYTES_PER_SAMPLE].q;

        // Unsigned 16-bit ints holding the two's-complement 12-bit sample values
        uint16_t raw_i;
        uint16_t raw_q;

        if(signed_i >= 0)
        {
            raw_i = (uint16_t) signed_i;
        }
        else
        {
            raw_i = (((uint16_t) (-signed_i)) ^ ((uint16_t) 0xFFF)) + (uint16_t) 1;
        }

        if(signed_q >= 0)
        {
            raw_q = (uint16_t) signed_q;
        }
        else
        {
            raw_q = (((uint16_t) (-signed_q)) ^ ((uint16_t) 0xFFF)) + (uint16_t) 1;
        }

        // Copy raw i/q data into the buffer
        memcpy(transfer->buffer + i, &raw_q, sizeof(raw_q));
        memcpy(transfer->buffer + i + sizeof(raw_q), &raw_i, sizeof(raw_i));
    }

    return transfer->length;
}

void FreeSRP::FreeSRP::impl::decode_rx_transfer(unsigned char *buffer, int actual_length, std::vector<sample> &destination)
{
    destination.resize(actual_length/FREESRP_BYTES_PER_SAMPLE);

    for(int i = 0; i < actual_length; i+=FREESRP_BYTES_PER_SAMPLE)
    {
        uint16_t raw_i;
        uint16_t raw_q;
        memcpy(&raw_q, buffer+i, sizeof(raw_q));
        memcpy(&raw_i, buffer+i+sizeof(raw_q), sizeof(raw_i));

        // Convert the raw I/Q values from 12-bit (two's complement) to 16-bit signed integers
        int16_t signed_i;
        int16_t signed_q;
        // Do sign extension
        bool i_negative = (raw_i & (1 << 11))!=0;
        bool q_negative = (raw_q & (1 << 11))!=0;
        if (i_negative)
        {
            signed_i = (int16_t) (raw_i | ~((1 << 11)-1));
        }
        else
        {
            signed_i = raw_i;
        }
        if (q_negative)
        {
            signed_q = (int16_t) (raw_q | ~((1 << 11)-1));
        }
        else
        {
            signed_q = raw_q;
        }

        destination[i/FREESRP_BYTES_PER_SAMPLE].i = signed_i;
        destination[i/FREESRP_BYTES_PER_SAMPLE].q = signed_q;
    }
}

void FreeSRP::FreeSRP::impl::run_rx_tx()
{
    while(_run_rx_tx.load())
    {
        libusb_handle_events(_ctx);
    }
}

unsigned long FreeSRP::FreeSRP::impl::available_rx_samples()
{
    return _rx_buf.size_approx();
}

bool FreeSRP::FreeSRP::impl::get_rx_sample(sample &s)
{
    return _rx_buf.try_dequeue(s);
}

bool FreeSRP::FreeSRP::impl::submit_tx_sample(sample &s)
{
    return _tx_buf.try_enqueue(s);
}

FreeSRP::command FreeSRP::FreeSRP::impl::make_command(command_id id, double param) const
{
    command cmd;

    cmd.cmd = id;
    switch(cmd.cmd)
    {
    case SET_TX_LO_FREQ:
    {
        uint64_t cast_param = static_cast<uint64_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_TX_SAMP_FREQ:
    {
        uint32_t cast_param = static_cast<uint32_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_TX_RF_BANDWIDTH:
    {
        uint32_t cast_param = static_cast<uint32_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_TX_ATTENUATION:
    {
        uint32_t cast_param = static_cast<uint32_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_TX_FIR_EN:
    {
        uint8_t cast_param = static_cast<uint8_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_RX_LO_FREQ:
    {
        uint64_t cast_param = static_cast<uint64_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_RX_SAMP_FREQ:
    {
        uint32_t cast_param = static_cast<uint32_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_RX_RF_BANDWIDTH:
    {
        uint32_t cast_param = static_cast<uint32_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_RX_GC_MODE:
    {
        uint8_t cast_param = static_cast<uint8_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_RX_RF_GAIN:
    {
        int32_t cast_param = static_cast<int32_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_RX_FIR_EN:
    {
        uint8_t cast_param = static_cast<uint8_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_DATAPATH_EN:
    {
        uint8_t cast_param = static_cast<uint8_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    case SET_LOOPBACK_EN:
    {
        uint8_t cast_param = static_cast<uint8_t>(param);
        memcpy(&cmd.param, &cast_param, sizeof(cast_param));
    }
        break;
    default:
        throw std::runtime_error("make_command error: " + std::to_string(id));
    }

    return cmd;
}

FreeSRP::response FreeSRP::FreeSRP::impl::send_cmd(command cmd) const
{
    cmd_buf tx_buf{static_cast<unsigned char>(cmd.cmd), 1};
    memcpy(tx_buf.data() + 2, &cmd.param, sizeof(cmd.param));

    // Interrupt OUT transfer
    int ret;
    int transferred;
    ret = libusb_interrupt_transfer(_freesrp_handle, FREESRP_FPGA_UART_OUT, (unsigned char *) tx_buf.data(), (int) tx_buf.size(), &transferred, FREESRP_USB_TIMEOUT);
    if(ret < 0)
    {
        throw ConnectionError("INTERRUPT OUT transfer to UART endpoint failed! error " + std::to_string(ret));
    }

    // Interrupt IN transfer
    cmd_buf rx_buf{};
    ret = libusb_interrupt_transfer(_freesrp_handle, FREESRP_FPGA_UART_IN, rx_buf.data(), (int) rx_buf.size(), &transferred, FREESRP_USB_TIMEOUT);
    if(ret < 0)
    {
        throw ConnectionError("INTERRUPT IN transfer from UART endpoint failed! error " + std::to_string(ret));
    }

    response res;
    res.cmd = (command_id)(rx_buf.data()[0]);
    res.error = (command_err)(rx_buf.data()[10]);
    memcpy(&res.param, rx_buf.data() + 2, sizeof(res.param));

    return res;
}

FreeSRP::freesrp_version FreeSRP::FreeSRP::impl::version()
{
    response res = send_cmd({GET_FPGA_VERSION});
    uint8_t fpga_major_version = ((uint8_t*) &res.param)[0];
    uint8_t fpga_minor_version = ((uint8_t*) &res.param)[1];
    uint8_t fpga_patch_version = ((uint8_t*) &res.param)[2];

    freesrp_version v;
    v.fx3 = _fx3_fw_version;
    v.fpga = std::to_string(fpga_major_version) + "." + std::to_string(fpga_minor_version) + "." + std::to_string(fpga_patch_version);

    return v;
}
