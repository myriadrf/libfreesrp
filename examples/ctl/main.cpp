#include <iostream>
#include <boost/tokenizer.hpp>

#include <freesrp.hpp>

#include "cmds.hpp"

#include "optionparser.hpp"

using namespace std;
using namespace FreeSRP;

bool process_command(const FreeSRP::FreeSRP &srp)
{
    bool exit = false;

    cout << "FreeSRP CTL> ";

    string input;
    getline(cin, input);

    boost::char_separator<char> sep(" ");
    boost::tokenizer<boost::char_separator<char>> tokens(input, sep);

    string cmd_id;
    vector<string> params;

    int i = 0;
    for(const string &token : tokens)
    {
        if(i == 0)
        {
            cmd_id = token;
        }
        else
        {
            params.push_back(token);
        }
        i++;
    }

    bool cmd_found = false;
    for(const cmds::cmd_def &cmd : cmds::cmds)
    {
        if(cmd.cmd == cmd_id)
        {
            cmd_found = true;
            if(cmd.exit)
            {
                exit = true;
                break;
            }

            if(cmd.func != nullptr)
            {
                try
                {
                    cmd.func(srp, params);
                }
                catch(ConnectionError e)
                {
                    cerr << "Error sending command to FreeSRP, " << e.what() << endl;
                    exit = true;
                }
            }
        }
    }

    if(!cmd_found)
    {
        cout << "Command '" << cmd_id << "' not found. Type 'help' for a list of available commands." << endl;
    }

    return !exit;
}

enum optionIndex {NONE, HELP, FPGA};
const option::Descriptor usage[] = {
    {NONE, 0, "", "",       option::Arg::None,      "usage: freesrp-ctl [options]\noptions:"},
    {HELP, 0, "", "help",   option::Arg::None,      "  --help                           Print usage and exit."},
    {FPGA, 0, "", "fpga",   option::Arg::Optional,  "  --fpga=/path/to/bitstream.bin    Load the FPGA with the specified bitstream"},
    {0,0,0,0,0,0}
};

int main(int argc, char *argv[])
{
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

    std::string fpgaconfig_filename = "";

    if(options[FPGA])
    {
        if(options[FPGA].arg)
        {
            fpgaconfig_filename = options[FPGA].arg;
        }
        else
        {
            cout << "Error: --fpga option expects a filename! See 'freesrp-ctl --help'." << endl;
            return 1;
        }
    }

    try
    {
        FreeSRP::FreeSRP srp;
        cout << "Connected to FreeSRP" << endl;
        cout << "Version: " << srp.firmware_version() << endl;

        // Configure FPGA if bitstream specified
        if(fpgaconfig_filename.length() > 0)
        {
            cout << "Loading FPGA with '" << fpgaconfig_filename << "'" << endl;
            if(srp.load_fpga(fpgaconfig_filename))
            {
                // FPGA loaded successfully
                cout << "FPGA configured successfully" << endl;
            }
            else
            {
                // Error configuring FPGA
                cout << "Could not configure FPGA" << endl;
            }
        }

        // Check FPGA status
        if(!srp.fpga_loaded())
        {
            cout << "FPGA not configured. Please configure the FPGA first: " << endl;
            cout << "Example: freesrp-ctl --fpga=/path/to/bitstream.bin" << endl;
            return 1;
        }

        cout << "Type 'help' for a list of valid commands." << endl;
        while(process_command(srp)) {}
    }
    catch(const ConnectionError &e)
    {
        cerr << "Could not connect to FreeSRP: " << e.what() << endl;
    }
    catch(const runtime_error &e)
    {
        cerr << "Unexpected exception occurred! " << e.what() << endl;
    }

    return 0;
}