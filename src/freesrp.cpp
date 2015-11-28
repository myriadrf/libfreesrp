#include <freesrp.hpp>

#include <cstring>

using namespace FreeSRP;

boost::circular_buffer<sample> FreeSRP::FreeSRP::_rx_buf(LIB_RX_TX_BUF_SIZE);
std::mutex FreeSRP::FreeSRP::_rx_buf_mutex;

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

    if(_ctx != nullptr)
    {
        libusb_exit(_ctx); //close the session
    }
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

void FreeSRP::FreeSRP::rx_callback(libusb_transfer *transfer)
{
    if(transfer->status == LIBUSB_TRANSFER_COMPLETED)
    {
        // Success
        transfer->buffer;

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

            std::lock_guard<std::mutex> lock(_rx_buf_mutex);
            _rx_buf.push_back(s);
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

void FreeSRP::FreeSRP::run_rx_tx()
{
    while(_run_rx_tx.load())
    {
        libusb_handle_events(_ctx);
    }
}

unsigned long FreeSRP::FreeSRP::available_rx_samples()
{
    return _rx_buf.size();
}

sample FreeSRP::FreeSRP::get_rx_sample()
{
    std::lock_guard<std::mutex> lock(_rx_buf_mutex);
    sample s = _rx_buf.front();
    _rx_buf.pop_front();
    return s;
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