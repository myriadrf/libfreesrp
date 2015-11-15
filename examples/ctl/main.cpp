#include <iostream>
#include <boost/tokenizer.hpp>

#include <freesrp.hpp>

#include "cmds.hpp"

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

int main()
{
    try
    {
        FreeSRP::FreeSRP srp;
        cout << "Connected to FreeSRP" << endl;
        cout << "Version: " << srp.firmware_version() << endl;
        cout << "Type 'help' for a list of valid commands." << endl;
        while(process_command(srp)) {}
    }
    catch(const ConnectionError &e)
    {
        cerr << "Could not connect to FreeSRP, " << e.what() << endl;
    }

    return 0;
}