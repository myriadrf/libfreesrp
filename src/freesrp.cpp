#include <freesrp.hpp>

#include <cstring>

using namespace FreeSRP;

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
}

FreeSRP::FreeSRP::~FreeSRP()
{
    if(_freesrp_handle != nullptr)
    {
        libusb_release_interface(_freesrp_handle, 0);
        libusb_close(_freesrp_handle);
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

void FreeSRP::FreeSRP::start_rx()
{
    /*libusb_transfer *transfer;
    std::shared_ptr<rx_tx_buf> rx_buf = std::make_shared<rx_tx_buf>();
    int completed = 0;

    transfer = libusb_alloc_transfer(0);
    libusb_fill_bulk_transfer(transfer, _freesrp_handle, FREESRP_RX_IN, rx_buf->data.data(), (int) rx_buf->size, callback, user_data, FREESRP_USB_TIMEOUT);*/
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