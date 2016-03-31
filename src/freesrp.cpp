#include <freesrp.hpp>

#include <cstring>
#include <fstream>
#include <vector>

using namespace FreeSRP;

moodycamel::ReaderWriterQueue<sample> FreeSRP::FreeSRP::_rx_buf(LIB_RX_TX_BUF_SIZE);
moodycamel::ReaderWriterQueue<sample> FreeSRP::FreeSRP::_tx_buf(LIB_RX_TX_BUF_SIZE);

FreeSRP::FreeSRP::FreeSRP()
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
    // TODO: Handle multiple FreeSRPs
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
        }
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
    _freesrp_fw_version = std::string(std::begin(data), std::begin(data) + transferred);

    for(int i = 0; i < _rx_transfers.size(); i++)
    {
        _rx_transfers[i] = create_rx_transfer(&FreeSRP::rx_callback);
    }

    for(int i = 0; i < _tx_transfers.size(); i++)
    {
        _tx_transfers[i] = create_tx_transfer(&FreeSRP::tx_callback);
    }

    // Start libusb event handling
    _run_rx_tx.store(true);

    _rx_tx_worker.reset(new std::thread([this]() {
        run_rx_tx();
    }));
}

FreeSRP::FreeSRP::~FreeSRP()
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

bool FreeSRP::FreeSRP::fpga_loaded()
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

bool FreeSRP::FreeSRP::load_fpga(std::string filename)
{
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
    ret = libusb_bulk_transfer(_freesrp_handle, FREESRP_FPGA_UART_OUT, (unsigned char *) configfile_buffer.data(), (int) configfile_buffer.size(), &transferred, FREESRP_USB_TIMEOUT);
    if(ret < 0)
    {
        throw ConnectionError("BULK OUT transfer of FPGA configuration failed! error " + std::to_string(ret));
    }

    // Get FreeSRP FPGA configuration status
    return fpga_loaded();
}

std::shared_ptr<rx_tx_buf> FreeSRP::FreeSRP::rx()
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

void FreeSRP::FreeSRP::tx(std::shared_ptr<rx_tx_buf> rx_data)
{
    int transferred;
    int ret = libusb_bulk_transfer(_freesrp_handle, FREESRP_TX_OUT, rx_data->data.data(), (int) rx_data->size, &transferred, FREESRP_USB_TIMEOUT);
    if(ret < 0)
    {
        throw ConnectionError("BULK OUT transfer to TX endpoint failed! error " + std::to_string(ret));
    }
    if(transferred != rx_data->size)
    {
        throw ConnectionError("Wrong amount of data tranferred! Available: " + std::to_string(rx_data->size) + "; transferred: " + std::to_string(transferred));
    }
}

libusb_transfer *FreeSRP::FreeSRP::create_rx_transfer(libusb_transfer_cb_fn callback)
{
    libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *buf = new unsigned char[FREESRP_RX_TX_BUF_SIZE];
    libusb_fill_bulk_transfer(transfer, _freesrp_handle, FREESRP_RX_IN, buf, FREESRP_RX_TX_BUF_SIZE, callback, nullptr, FREESRP_USB_TIMEOUT);

    return transfer;
}

libusb_transfer *FreeSRP::FreeSRP::create_tx_transfer(libusb_transfer_cb_fn callback)
{
    libusb_transfer *transfer = libusb_alloc_transfer(0);
    unsigned char *buf = new unsigned char[FREESRP_RX_TX_BUF_SIZE];
    libusb_fill_bulk_transfer(transfer, _freesrp_handle, FREESRP_TX_OUT, buf, FREESRP_RX_TX_BUF_SIZE, callback, nullptr, FREESRP_USB_TIMEOUT);

    return transfer;
}

void FreeSRP::FreeSRP::rx_callback(libusb_transfer *transfer)
{
    if(transfer->status == LIBUSB_TRANSFER_COMPLETED)
    {
        // Success

        for(int i = 0; i < transfer->actual_length; i+=4)
        {
            uint16_t raw_i;
            uint16_t raw_q;
            memcpy(&raw_q, transfer->buffer + i, sizeof(raw_q));
            memcpy(&raw_i, transfer->buffer + i + sizeof(raw_q), sizeof(raw_i));

            // Convert the raw I/Q values from 12-bit (two's complement) to 16-bit signed integers
            int16_t signed_i;
            int16_t signed_q;
            // Do sign extension
            bool i_negative = (raw_i & (1 << 11)) != 0;
            bool q_negative = (raw_q & (1 << 11)) != 0;
            if(i_negative)
            {
                signed_i = (int16_t) (raw_i | ~((1 << 11) - 1));
            }
            else
            {
                signed_i = raw_i;
            }
            if(q_negative)
            {
                signed_q = (int16_t) (raw_q | ~((1 << 11) - 1));
            }
            else
            {
                signed_q = raw_q;
            }

            // Convert the signed integers (range -2048 to 2047) to floats (range -1 to 1)
            sample s;
            s.i = (float) signed_i / 2048.0f;
            s.q = (float) signed_q / 2048.0f;

            bool success = _rx_buf.try_enqueue(s);
            if(!success)
            {
                // TODO: overflow! handle this
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

void FreeSRP::FreeSRP::tx_callback(libusb_transfer* transfer)
{
    if(transfer->status == LIBUSB_TRANSFER_COMPLETED)
    {
        // Success
    }
    else
    {
        // TODO: Handle error
    }

    // Resubmit the transfer with new data
    if(transfer->status != LIBUSB_TRANSFER_CANCELLED)
    {
        fill_tx_transfer(transfer);
        int ret = libusb_submit_transfer(transfer);

        if(ret < 0)
        {
            // TODO: Handle error
        }
    }
}

void FreeSRP::FreeSRP::start_rx()
{
    for(libusb_transfer *transfer: _rx_transfers)
    {
        int ret = libusb_submit_transfer(transfer);

        if(ret < 0)
        {
            throw ConnectionError("Could not submit RX transfer. libusb error: " + std::to_string(ret));
        }
    }
}

void FreeSRP::FreeSRP::stop_rx()
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

void FreeSRP::FreeSRP::start_tx()
{
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

void FreeSRP::FreeSRP::stop_tx()
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

int FreeSRP::FreeSRP::fill_tx_transfer(libusb_transfer* transfer)
{
    // Fill the transfer buffer with available samples
    transfer->length = FREESRP_RX_TX_BUF_SIZE;
    for(int i = 0; i < transfer->length; i++)
    {
        sample s;
        int success = _tx_buf.try_dequeue(s);
        if(!success)
        {
            transfer->length = i;
            break;
        }

        // Convert -1.0 to 1.0 float sample value to signed 16-bit int with range -2048 to 2048
        int16_t signed_i = (int16_t) (s.i * 2048.0f);
        int16_t signed_q = (int16_t) (s.q * 2048.0f);

        // Unsigned 16-bit ints holding the two's-complement 12-bit sample values
        uint16_t raw_i;
        uint16_t raw_q;

        if(signed_i >= 0)
        {
            raw_i = (uint16_t) signed_i;
        }
        else
        {
            raw_i = (uint16_t) (1 << 11) + signed_i;
        }

        if(signed_q >= 0)
        {
            raw_q = (uint16_t) signed_q;
        }
        else
        {
            raw_q = (uint16_t) (1 << 11) + signed_q;
        }

        // Copy raw i/q data into the buffer
        memcpy(transfer->buffer + i, &raw_q, sizeof(raw_q));
        memcpy(transfer->buffer + i + sizeof(raw_q), &raw_i, sizeof(raw_i));
    }

    return transfer->length;
}

void FreeSRP::FreeSRP::run_rx_tx()
{
    while(_run_rx_tx.load())
    {
        libusb_handle_events(_ctx);
    }
}

unsigned long FreeSRP::FreeSRP::available_rx_samples()
{
    return _rx_buf.size_approx();
}

bool FreeSRP::FreeSRP::get_rx_sample(sample &s)
{
    return _rx_buf.try_dequeue(s);
}

bool FreeSRP::FreeSRP::submit_tx_sample(sample &s)
{
    return _tx_buf.try_enqueue(s);
}

command FreeSRP::FreeSRP::make_command(command_id id, double param) const
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
    default:
        throw std::runtime_error("make_command error: " + std::to_string(id));
    }

    return cmd;
}

response FreeSRP::FreeSRP::send_cmd(command cmd) const
{
    cmd_buf tx_buf{cmd.cmd, 1};
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

std::string FreeSRP::FreeSRP::firmware_version()
{
    return _freesrp_fw_version;
}