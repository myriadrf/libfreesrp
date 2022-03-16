/*
 * Copyright 2017 by Lukas Lao Beyer <lukas@electronics.kitchen>
 *
 * This file is part of libfreesrp.
 *
 * libfreesrp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * libfreesrp is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with libfreesrp.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <boost/tokenizer.hpp>

#include <freesrp.hpp>

#include "cmds.hpp"

#include "optionparser.hpp"

using namespace std;
//using namespace FreeSRP;

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
                catch(FreeSRP::ConnectionError e)
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

enum optionIndex {NONE, HELP, INTERACTIVE, FPGA, FX3, LIST};
const option::Descriptor usage[] = {
    {NONE,        0, "",  "",       option::Arg::None,      "usage: freesrp-ctl [options] [id]\noptions:"},
    {HELP,        0, "",  "help",   option::Arg::None,      "  --help                           Print usage and exit"},
    {LIST,        0, "l", "list",   option::Arg::None,      "  --list, -l                       List serial numbers of connected FreeSRPs"},
    {INTERACTIVE, 0, "i", "interactive", option::Arg::None, "  --interactive, -i                Run in interactive mode"},
    {FPGA,        0, "",  "fpga",   option::Arg::Optional,  "  --fpga=/path/to/bitstream.bin    Load the FPGA with the specified bitstream"},
    {FX3,         0, "",  "fx3",    option::Arg::Optional,  "  --fx3=/path/to/firmware.img      Upload firmware to a Cypress EZ-USB FX3"},
    {NONE,        0, "",  "",       option::Arg::None,      "id:\n  (optional) the serial number of the device to connect to."},
    {0,0,0,0,0,0}
};

void list_devices()
{
    vector<string> connected_devs = FreeSRP::FreeSRP::list_connected();

    if(connected_devs.size() == 0)
    {
	cout << "No FreeSRP found" << endl;
	return;
    }

    cout << "FreeSRP devices detected:" << endl;
    
    for(string &serial : connected_devs)
    {
	cout << "   * " << serial << endl;
    }
}

void check_fx3()
{
    // Check for FX3
    if(FreeSRP::Util::find_fx3())
    {
        cout << "NOTE: Found a Cypress EZ-USB FX3 device. This could be a FreeSRP in bootloader mode.\n"
                "You can upload the FreeSRP firmware to it by running 'freesrp-ctl --fx3=/path/to/firmware.img'" << endl;
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

    std::string serial = "";
    
    if(parse.nonOptionsCount() > 0)
    {
        serial = parse.nonOption(0);
    }

    if(options[HELP])
    {
        option::printUsage(cout, usage);
        return 0;
    }

    if(options[LIST])
    {
	list_devices();
	check_fx3();
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
            cerr << "Error: --fpga option expects a filename! See 'freesrp-ctl --help'." << endl;
            return 1;
        }
    }

    bool interactive = false;
    if(options[INTERACTIVE])
    {
        interactive = true;
    }

    std::string fx3_firmware = "";

    if(options[FX3])
    {
        if(options[FX3].arg)
        {
            fx3_firmware = options[FX3].arg;
        }
        else
        {
            cerr << "Error: --fx3 option expects a filename! See 'freesrp-ctl --help'." << endl;
        }
    }

    if(fx3_firmware.length() > 0)
    {
        // Upload firmware to FX3.
        try
        {
            if(FreeSRP::Util::find_fx3(true, fx3_firmware))
            {
                // Firmware upload succeeded, continue
                cout << "Sucessfully uploaded FreeSRP firmware to FX3" << endl;
                this_thread::sleep_for(chrono::milliseconds(600));
            }
            else
            {
                cerr << "Firmware upload to FX3 failed!" << endl;
                return 1;
            }
        }
        catch(const runtime_error &e)
        {
            cerr << "Error while uploading firmware to FX3! " << e.what() << endl;
            return 1;
        }
    }

    // Connect to FreeSRP and start interactive mode if requested
    try
    {
        FreeSRP::FreeSRP srp(serial);
        cout << "Found FreeSRP" << endl;

        // Configure FPGA if bitstream specified
        if(fpgaconfig_filename.length() > 0)
        {
            cout << "Loading FPGA with '" << fpgaconfig_filename << "'" << endl;
            switch(srp.load_fpga(fpgaconfig_filename))
            {
                case FreeSRP::FPGA_CONFIG_DONE:
                    cout << "FPGA configured successfully" << endl;
                    break;
                case FreeSRP::FPGA_CONFIG_ERROR:
                    cout << "Error configuring FPGA!" << endl;
                    break;
                case FreeSRP::FPGA_CONFIG_SKIPPED:
                    cout << "FPGA already configured. To re-configure, please restart the FreeSRP." << endl;
                    break;
            }
        }

        // Check FPGA status
        if(!srp.fpga_loaded())
        {
            cout << "FPGA not configured. Please configure the FPGA first: " << endl;
            cout << "Example: freesrp-ctl --fpga=/path/to/bitstream.bin" << endl;
            return 1;
        }

        cout << "Connected to FreeSRP" << endl;
        cout << "Version: " << srp.version() << endl;

        if(interactive)
        {
            cout << "Type 'help' for a list of valid commands." << endl;
            while(process_command(srp)) {}
        }
    }
    catch(const FreeSRP::ConnectionError &e)
    {
        cerr << "Could not connect to FreeSRP: " << e.what() << endl;
    }
    catch(const runtime_error &e)
    {
        cerr << "Unexpected exception occurred! " << e.what() << endl;
    }

    check_fx3();
    
    return 0;
}
