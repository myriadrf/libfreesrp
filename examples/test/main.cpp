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

        cout << "----TEST CMD----------------------------------------------------------------" << endl;

        cout << srp.send_cmd({SET_RX_LO_FREQ, 1400}) << endl;
        cout << srp.send_cmd({GET_RX_LO_FREQ}) << endl;
        cout << srp.send_cmd({SET_DATAPATH_EN, 1}) << endl;

        // Test RX

        cout << "----TEST RX-----------------------------------------------------------------" << endl;

        srp.start_rx();

        auto start_rx_time = chrono::system_clock::now();

        unsigned long received_samples = 0;
        const unsigned long samples_to_receive = 1024 * 1024 * 40;

        while(received_samples < samples_to_receive)
        {
            sample s;
            bool success = srp.get_rx_sample(s);
            if(success)
            {
                received_samples++;
            }
        }

        auto duration_rx = chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now() - start_rx_time);

        srp.stop_rx();

        cout << "Received " << received_samples << " / " << samples_to_receive << endl;
        cout << "Received " << received_samples << " samples in " << (duration_rx.count() / 1000.0) << " ms (" << ((float) received_samples / (float) duration_rx.count()) << " MSps / " << ((received_samples * 4) / (float) duration_rx.count()) << " MBps)" << endl;

        // Test TX

        cout << "----TEST TX-----------------------------------------------------------------" << endl;

        srp.start_tx();

        auto start_tx_time = chrono::system_clock::now();

        unsigned long sent_samples = 0;
        const unsigned long samples_to_send = 1024 * 1024 * 40;

        while(sent_samples < samples_to_send)
        {
            sample s{0.5f, 0.5f};
            bool success = srp.submit_tx_sample(s);
            if(success)
            {
                sent_samples++;
            }
        }

        auto duration_tx = chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now() - start_tx_time);

        srp.stop_tx();

        cout << "Sent " << sent_samples << " / " << samples_to_send << endl;
        cout << "Sent " << sent_samples << " samples in " << (duration_tx.count() / 1000.0) << " ms (" << ((float) received_samples / (float) duration_tx.count()) << " MSps / " << ((sent_samples * 4) / (float) duration_tx.count()) << " MBps)" << endl;

        /* OLD SYNCHRONOUS INTERFACE
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
        long received_samples = 0;
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
        for(int i = 0; i < rx_buf->size; i+=4)
        {
            uint16_t raw_i;
            uint16_t raw_q;
            memcpy(&raw_q, rx_buf->data.data() + i, sizeof(raw_q));
            memcpy(&raw_i, rx_buf->data.data() + i + sizeof(raw_q), sizeof(raw_i));

            // Convert the raw I/Q values from 12-bit (two's complement) to 16-bit signed integers
            int16_t signed_i;
            int16_t signed_q;
            // Sign extension
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
            float i_value = (float) signed_i / 2048.0f;
            float q_value = (float) signed_q / 2048.0f;

            cout << "I: " << i_value << "\tQ: " << q_value << "\t" << signed_i << "\t" << signed_q << endl;
            received_samples++;
        }

        cout << "Received " << received_samples << " samples in " << duration.count() / 1000.0 << " ms (" << ((float) received_samples / (float) duration.count()) << " MSps / " << (rx_buf->size / (float) duration.count()) << " MBps)" << endl;
        //cout << "Received " << rx_buf->size << " bytes in " << duration.count() << " us (" << (rx_buf->size / duration.count()) << " MBps)" << endl;
        */
    }
    catch(ConnectionError e)
    {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}