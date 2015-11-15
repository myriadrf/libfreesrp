#include <iostream>
#include <chrono>
#include <thread>

#include <freesrp.hpp>
#include <cstring>

using namespace std;
using namespace FreeSRP;

int main()
{
    try
    {
        FreeSRP::FreeSRP srp;
        cout << "firmware version is " << srp.firmware_version() << endl;

        cout << srp.send_cmd({SET_RX_LO_FREQ, 1400}) << endl;
        cout << srp.send_cmd({GET_RX_LO_FREQ}) << endl;
        cout << srp.send_cmd({SET_DATAPATH_EN, 1}) << endl;


        // Test TX
        shared_ptr<rx_tx_buf> tx_buf = make_shared<rx_tx_buf>();
        tx_buf->size = 1024;
        cout << "TX Buffer: " << endl;
        for(int i = 0; i < tx_buf->size; i+=4)
        {
            uint32_t word = (uint32_t) i/4;
            memcpy(tx_buf->data.data() + i, &word, sizeof(word));
            cout << (int) word << endl;
        }
        auto start_tx = chrono::system_clock::now();
        srp.tx(tx_buf);
        auto duration_tx = chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now() - start_tx);

        cout << "Transmitted " << tx_buf->size << " bytes in " << duration_tx.count() << " us" << endl;

        this_thread::sleep_for(chrono::milliseconds(10));

        // Test RX
        auto start = chrono::system_clock::now();
        shared_ptr<rx_tx_buf> rx_buf;
        rx_buf = srp.rx();
        auto duration = chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now() - start);
        cout << "RX Buffer: " << endl;
        for(int i = 0; i < rx_buf->size; i+=4)
        {
            uint32_t word;
            memcpy(&word, rx_buf->data.data() + i, sizeof(word));
            cout << (int) word << endl;
        }

        cout << "Received " << rx_buf->size << " bytes in " << duration.count() << " us (" << (rx_buf->size / duration.count()) << " MBps)" << endl;
    }
    catch(ConnectionError e)
    {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}