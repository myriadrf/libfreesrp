#include <iostream>
#include <fstream>
#include <mutex>
#include <condition_variable>

#include <freesrp.hpp>

#include "optionparser.hpp"

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

using namespace std;
using namespace FreeSRP;

enum optionIndex {NONE, HELP, OUTFILE, FPGA};
const option::Descriptor usage[] = {
        {NONE,        0, "",  "",       option::Arg::None,      "usage: freesrp-rec [options] -ofilename\noptions:"},
        {HELP,        0, "h", "help",   option::Arg::None,      "  -h, --help                       Print usage and exit"},
        {OUTFILE,     0, "o", "out",    option::Arg::Optional,  "  -ofilename, --out=filename       Output to specified file ('-o-' for stdout)"},
        {FPGA,        0, "",  "fpga",   option::Arg::Optional,  "  --fpga=/path/to/bitstream.bin    Load the FPGA with the specified bitstream"},
        {0,0,0,0,0,0}
};

unique_ptr<ostream> _out = nullptr;

mutex _interrupt_mut;
condition_variable _interrupt;

void sigint_callback(int s)
{
    _interrupt.notify_all();
}

void rx_callback(const vector<sample>& rx_buf)
{
    for(sample s : rx_buf)
    {
        _out->write((char *) &s.i, sizeof(s.i));
        _out->write((char *) &s.q, sizeof(s.q));
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

    if(options[OUTFILE] && options[OUTFILE].arg)
    {
        string outfile = options[OUTFILE].arg;
        ofstream of;

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
        cout << "Found FreeSRP" << endl;

        // Configure FPGA if bitstream specified
        if(fpgaconfig_filename.length() > 0)
        {
            cout << "Loading FPGA with '" << fpgaconfig_filename << "'" << endl;
            switch(srp.load_fpga(fpgaconfig_filename))
            {
            case FPGA_CONFIG_DONE:
                cout << "FPGA configured successfully" << endl;
                break;
            case FPGA_CONFIG_ERROR:
                cout << "Error configuring FPGA!" << endl;
                break;
            case FPGA_CONFIG_SKIPPED:
                cout << "FPGA already configured. To re-configure, please restart the FreeSRP." << endl;
                break;
            }
        }

        // Check FPGA status
        if(!srp.fpga_loaded())
        {
            cout << "FPGA not configured. Please configure the FPGA first: " << endl;
            cout << "Example: freesrp-rec --fpga=/path/to/bitstream.bin" << endl;
            return 1;
        }

        cout << "Connected to FreeSRP" << endl;
        cout << "Version: " << srp.version() << endl;

        start(srp);

        // Wait for Control-C
        struct sigaction sigint_handler;
        sigint_handler.sa_handler = sigint_callback;
        sigemptyset(&sigint_handler.sa_mask);
        sigint_handler.sa_flags = 0;

        sigaction(SIGINT, &sigint_handler, NULL);

        unique_lock<mutex> lck(_interrupt_mut);
        _interrupt.wait(lck);

        stop(srp);
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
        cout << "NOTE: Found a Cypress EZ-USB FX3 device. This could be a FreeSRP in bootloader mode.\n"
                "You can upload the FreeSRP firmware to it by running 'freesrp-ctl --fx3=/path/to/firmware.img'" << endl;
    }

    return 0;
}