#ifndef LIBFREESRP_FREESRP_HPP
#define LIBFREESRP_FREESRP_HPP

#include <string>
#include <array>
#include <stdexcept>
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
#include <cstdint>

#include <libusb.h>

#include <readerwriterqueue/readerwriterqueue.h>

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

#define FREESRP_RX_TX_BUF_SIZE 1024 * 64
#define FREESRP_TX_BUF_SIZE 1024 * 32
#define FREESRP_RX_TX_TRANSFER_QUEUE_SIZE 128

#define LIB_RX_TX_BUF_SIZE FREESRP_RX_TX_BUF_SIZE * FREESRP_RX_TX_TRANSFER_QUEUE_SIZE

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
        float i;
        float q;
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
    public:
        FreeSRP();
        ~FreeSRP();

        bool fpga_loaded();
        fpga_status load_fpga(std::string filename);

        std::shared_ptr<rx_tx_buf> rx();
        void tx(std::shared_ptr<rx_tx_buf> buf);

        void start_rx();
        void stop_rx();

        void start_tx();
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

        libusb_context *_ctx = nullptr;
        libusb_device_handle *_freesrp_handle = nullptr;

        std::string _fx3_fw_version;

        std::atomic<bool> _run_rx_tx{false};
        std::unique_ptr<std::thread> _rx_tx_worker;

        std::array<libusb_transfer *, FREESRP_RX_TX_TRANSFER_QUEUE_SIZE> _rx_transfers;
        std::array<libusb_transfer *, FREESRP_RX_TX_TRANSFER_QUEUE_SIZE> _tx_transfers;

        static moodycamel::ReaderWriterQueue<sample> _rx_buf;
        static moodycamel::ReaderWriterQueue<sample> _tx_buf;
    };

    namespace Util
    {
        /*
         * This will look for an FX3 in bootloader mode.
         * upload_firmware: If 'false', this function will return 'true' if an unprogrammed FX3 is found.
         *                  If 'true', it will attempt to program the FX3 with the specified firmware.
         * filename: Path to the image file to program the FX3 with.
         */
        bool find_fx3(bool upload_firmware=false, std::string filename="");
    };
}

#endif
