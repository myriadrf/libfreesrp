#ifndef LIBFREESRP_FREESRP_HPP
#define LIBFREESRP_FREESRP_HPP

#include <string>
#include <array>
#include <stdexcept>
#include <iostream>
#include <memory>

#include <libusb.h>

//#define FREESRP_VENDOR_ID 0xe1ec
//#define FREESRP_PRODUCT_ID 0xf5d0
#define FREESRP_VENDOR_ID 0x04b4
#define FREESRP_PRODUCT_ID 0x00f0

#define FREESRP_USB_TIMEOUT 250

#define FREESRP_FPGA_UART_OUT 0x01
#define FREESRP_FPGA_UART_IN 0x81
#define FREESRP_TX_OUT 0x02
#define FREESRP_RX_IN 0x82

#define FREESRP_USB_CTRL_SIZE 64
#define FREESRP_UART_BUF_SIZE 16

#define FREESRP_RX_TX_BUF_SIZE 1024 * 64

// FreeSRP vendor commands
#define FREESRP_GET_VERSION_REQ 0

namespace FreeSRP
{
    struct rx_tx_buf
    {
        unsigned int size;
        std::array<unsigned char, FREESRP_RX_TX_BUF_SIZE> data;
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
        GET_REGISTER = 0,
        GET_TX_LO_FREQ,
        SET_TX_LO_FREQ,
        GET_TX_SAMP_FREQ,
        SET_TX_SAMP_FREQ,
        GET_TX_RF_BANDWIDTH,
        SET_TX_RF_BANDWIDTH,
        GET_TX_ATTENUATION,
        SET_TX_ATTENUATION,
        GET_TX_FIR_EN,
        SET_TX_FIR_EN,
        GET_RX_LO_FREQ,
        SET_RX_LO_FREQ,
        GET_RX_SAMP_FREQ,
        SET_RX_SAMP_FREQ,
        GET_RX_RF_BANDWIDTH,
        SET_RX_RF_BANDWIDTH,
        GET_RX_GC_MODE,
        SET_RX_GC_MODE,
        GET_RX_RF_GAIN,
        SET_RX_RF_GAIN,
        GET_RX_FIR_EN,
        SET_RX_FIR_EN,
        SET_DATAPATH_EN,
    };

    enum command_err
    {
        CMD_OK = 0,
        CMD_INVALID_PARAM
    };

    struct command
    {
        command_id cmd;
        double param;

        friend std::ostream &operator<<(std::ostream &o, const command cmd)
        {
            return o << "command ID: " << cmd.cmd << "; parameter: " << cmd.param;
        }
    };

    struct response
    {
        command_id cmd;
        double param;
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
    public:
        FreeSRP();
        ~FreeSRP();

        std::shared_ptr<rx_tx_buf> rx();
        void tx(std::shared_ptr<rx_tx_buf> buf);

        void start_rx();

        response send_cmd(command c) const;

        std::string firmware_version();
    private:
        libusb_context *_ctx = nullptr;
        libusb_device_handle *_freesrp_handle = nullptr;

        std::string _freesrp_fw_version;
    };
}

#endif
