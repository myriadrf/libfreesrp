#include <iostream>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <boost/lexical_cast.hpp>

#include <freesrp.hpp>

#include "optionparser.hpp"

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <iomanip>

using namespace std;
using namespace FreeSRP;

enum optionIndex {NONE, HELP, OUTFILE, FPGA, CENTER_FREQ, BANDWIDTH, GAIN};
const option::Descriptor usage[] = {
        {NONE,        0, "",  "",            option::Arg::None,      "usage: freesrp-rec [options] -ofilename\n"
                                                                     "       output format is complex signed 16-bit\noptions:"},
        {HELP,        0, "h", "help",        option::Arg::None,      "  -h, --help                     Print usage and exit"},
        {OUTFILE,     0, "o", "out",         option::Arg::Optional,  "  -o[filename], --out=[filename] Output to specified file ('-o-' for stdout)"},
        {FPGA,        0, "",  "fpga",        option::Arg::Optional,  "  --fpga=/path/to/bitstream.bin  Load the FPGA with the specified bitstream"},
        {CENTER_FREQ, 0, "f", "freq",       option::Arg::Optional,   "  -f[freq], --freq=[freq]        Center frequency in hertz (70e6 to 6e9)"},
        {BANDWIDTH,   0, "b", "bandwidth",  option::Arg::Optional,   "  -b[bw], --bandwidth=[bw]       Bandwidth in hertz (1e6 to 61.44e6)"},
        {GAIN,        0, "g", "gain",        option::Arg::Optional,  "  -g[gain], --gain=[gain]        Gain in decibels (0 to 74)"},
        {NONE,        0, "",  "",            option::Arg::None,      "\nexample: freesrp-rec -f2.42e9 -b4e6 -g30 -o-"},
        {0,0,0,0,0,0}
};

unique_ptr<ostream> _out = nullptr;

mutex _interrupt_mut;
condition_variable _interrupt;

void sigint_callback(int s)
{
    _interrupt.notify_all();
}

void rx_callback(const vector<sample> &samples)
{
    static vector<int16_t> buf;

    // For rate counter
    static long rate_probe_counter = 0; // Samples processed since last comparison period
    static long rate_probe_counter_comp = 10000000; // Calculate rate every this many processed samples
    static time_t current_ms = 0, previous_ms = 0;

    buf.resize(samples.size() * 2);

    int buf_index = 0;
    for(const sample &s : samples)
    {
        // Convert from 12-bit to full scale 16-bit and copy to output buffer
        buf[buf_index++] = (int16_t) (s.i * 16);
        buf[buf_index++] = (int16_t) (s.q * 16);
    }

    _out->write((char *) buf.data(), sizeof(int16_t) * 2 * samples.size());

    // Calculate & report sample rate
    rate_probe_counter += samples.size();

    if(rate_probe_counter >= rate_probe_counter_comp)
    {
        if(previous_ms == 0)
        {
            previous_ms = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        }
        else
        {
            current_ms = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();;
            time_t elapsed_ms = current_ms - previous_ms;
            previous_ms = current_ms;

            cerr << fixed << setprecision(4) << ((double) rate_probe_counter) / ((double) elapsed_ms) / 1000.0 << "MSps" << endl;
        }

        rate_probe_counter = 0;
    }
}

void start(FreeSRP::FreeSRP &srp)
{
    // Enable datapath and start receiver
    FreeSRP::response res = srp.send_cmd({SET_DATAPATH_EN, 1});
    if(res.error != FreeSRP::CMD_OK)
    {
        throw runtime_error("Error enabling FreeSRP datapath!");
    }

    srp.start_rx(rx_callback);
}

void stop(FreeSRP::FreeSRP &srp)
{
    srp.stop_rx();

    FreeSRP::response res = srp.send_cmd({SET_DATAPATH_EN, 0});
    if(res.error != FreeSRP::CMD_OK)
    {
        throw runtime_error("Error disabling FreeSRP datapath!");
    }
}

int main(int argc, char *argv[])
{
    // Parse options
    argc-=(argc>0); argv+=(argc>0); // Skip program name (argv[0]), if present
    option::Stats stats(usage, argc, argv);
    option::Option options[stats.options_max], buffer[stats.buffer_max];
    option::Parser parse(usage, argc, argv, options, buffer);

    if(parse.error())
    {
        return 1;
    }

    if(options[HELP])
    {
        option::printUsage(cout, usage);
        return 0;
    }

    streambuf *of_buf;
    ofstream of;

    if(options[OUTFILE] && options[OUTFILE].arg)
    {
        string outfile = options[OUTFILE].arg;

        if(outfile == "-")
        {
            of_buf = cout.rdbuf();
        }
        else
        {
            of.open(outfile, ios::binary | ios::out);
            of_buf = of.rdbuf();
        }
    }
    else
    {
        cerr << "Error: You must specify an output file using the '-o' option. See 'freesrp-rec --help'." << endl;
        return 1;
    }

    _out.reset(new ostream(of_buf));

    double center_freq = 0, bandwidth = 0, gain = 0;

    if(options[CENTER_FREQ] && options[BANDWIDTH] && options[GAIN] ||
       options[CENTER_FREQ].arg && !options[BANDWIDTH].arg && options[GAIN].arg)
    {
        try
        {
            center_freq = boost::lexical_cast<double>(options[CENTER_FREQ].arg);
            bandwidth = boost::lexical_cast<double>(options[BANDWIDTH].arg);
            gain = boost::lexical_cast<double>(options[GAIN].arg);
        }
        catch(boost::bad_lexical_cast)
        {
            cerr << "Error: Please specify valid numerical values" << endl;
            return 1;
        }
    }
    else
    {
        cerr << "Error: Please specify center frequency, bandwidth and gain. See 'freesrp-rec --help'." << endl;
        return 1;
    }

    string fpgaconfig_filename = "";

    if(options[FPGA])
    {
        if(options[FPGA].arg)
        {
            fpgaconfig_filename = options[FPGA].arg;
        }
        else
        {
            cerr << "Error: --fpga option expects a filename! See 'freesrp-rec --help'." << endl;
            return 1;
        }
    }

    // Connect to FreeSRP
    try
    {
        FreeSRP::FreeSRP srp;
        cerr << "Found FreeSRP" << endl;

        // Configure FPGA if bitstream specified
        if(fpgaconfig_filename.length() > 0)
        {
            cerr << "Loading FPGA with '" << fpgaconfig_filename << "'" << endl;
            switch(srp.load_fpga(fpgaconfig_filename))
            {
            case FPGA_CONFIG_DONE:
                cerr << "FPGA configured successfully" << endl;
                break;
            case FPGA_CONFIG_ERROR:
                cerr << "Error configuring FPGA!" << endl;
                break;
            case FPGA_CONFIG_SKIPPED:
                cerr << "FPGA already configured. To re-configure, please restart the FreeSRP." << endl;
                break;
            }
        }

        // Check FPGA status
        if(!srp.fpga_loaded())
        {
            cerr << "FPGA not configured. Please configure the FPGA first: " << endl;
            cerr << "Example: freesrp-rec --fpga=/path/to/bitstream.bin" << endl;
            return 1;
        }

        cerr << "Connected to FreeSRP" << endl;
        cerr << "Version: " << srp.version() << endl;

        // Set center frequency
        response r = srp.send_cmd(srp.make_command(SET_RX_LO_FREQ, center_freq));
        if(r.error != CMD_OK)
        {
            std::cerr << "Could not set RX LO frequency, error: " << r.error << endl;
            return 1;
        }

        // Set bandwidth and sample rate
        r = srp.send_cmd(srp.make_command(SET_RX_RF_BANDWIDTH, bandwidth));
        if(r.error != CMD_OK)
        {
            std::cerr << "Could not set RX bandwidth, error: " << r.error << endl;
            return 1;
        }

        r = srp.send_cmd(srp.make_command(SET_RX_SAMP_FREQ, bandwidth));
        if(r.error != CMD_OK)
        {
            std::cerr << "Could not set RX sample frequency, error: " << r.error << endl;
            return 1;
        }

        // Set gain
        r = srp.send_cmd(srp.make_command(SET_RX_RF_GAIN, gain));
        if(r.error != CMD_OK)
        {
            std::cerr << "Could not set RX gain, error: " << r.error << endl;
            return 1;
        }

        // Enable signal chain and start receiving samples
        start(srp);

        // Wait for Control-C
        struct sigaction sigint_handler;
        sigint_handler.sa_handler = sigint_callback;
        sigemptyset(&sigint_handler.sa_mask);
        sigint_handler.sa_flags = 0;

        sigaction(SIGINT, &sigint_handler, NULL);
        sigaction(SIGPIPE, &sigint_handler, NULL);

        unique_lock<mutex> lck(_interrupt_mut);
        _interrupt.wait(lck);

        // Disable signal chain
        stop(srp);

        _out->flush();

        cerr << endl << "Stopped." << endl;

        return 0;
    }
    catch(const ConnectionError &e)
    {
        cerr << "Could not connect to FreeSRP: " << e.what() << endl;
    }
    catch(const runtime_error &e)
    {
        cerr << "Unexpected exception occurred! " << e.what() << endl;
    }

    // Check for FX3
    if(Util::find_fx3())
    {
        cerr << "NOTE: Found a Cypress EZ-USB FX3 device. This could be a FreeSRP in bootloader mode.\n"
                "You can upload the FreeSRP firmware to it by running 'freesrp-ctl --fx3=/path/to/firmware.img'" << endl;
    }

    return 1;
}