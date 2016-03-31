#include "cmds.hpp"
#include <iostream>
#include <iomanip>
#include <boost/lexical_cast.hpp>

using namespace std;

namespace cmds
{
    int cmd_help(const FreeSRP::FreeSRP &srp, vector<string> &params)
    {
        cout << left;
        cout << setw(12) << "Command" << setw(100) << "Description" << endl;
        cout << setw(12) << "-------" << setw(100) << "-----------" << endl;
        for(const cmd_def &cmd : cmds)
        {
            cout << setw(12) << cmd.cmd << setw(100) << cmd.descr << endl;
        }
        return 0;
    }

    struct param_def
    {
        string name;
        string descr;
        FreeSRP::command_id id;
    };

    const vector<param_def> set_params = {
        {"tx_lo", "set transmitter local oscillator frequency [Hz]", FreeSRP::SET_TX_LO_FREQ},
        {"tx_samp", "set transmitter sample rate [Hz]", FreeSRP::SET_TX_SAMP_FREQ},
        {"tx_bw", "set transmitter bandwidth [Hz]", FreeSRP::SET_TX_RF_BANDWIDTH},
        {"tx_atten", "set transmitter attenuation [milli-dB]", FreeSRP::SET_TX_ATTENUATION},
        {"tx_fir_en", "enable/disable transmitter FIR filter [enable|disable]", FreeSRP::SET_TX_FIR_EN},
        {"rx_lo", "set receiver local oscillator frequency [Hz]", FreeSRP::SET_RX_LO_FREQ},
        {"rx_samp", "set receiver sample rate [Hz]", FreeSRP::SET_RX_SAMP_FREQ},
        {"rx_bw", "set receiver bandwidth [Hz]", FreeSRP::SET_RX_RF_BANDWIDTH},
        {"rx_gc", "set receiver gain control mode [??]", FreeSRP::SET_RX_GC_MODE},
        {"rx_gain", "set receiver gain [dB]", FreeSRP::SET_RX_RF_GAIN},
        {"rx_fir_en", "enable/disable receiver FIR filter [enable|disable]", FreeSRP::SET_RX_FIR_EN},
        {"datapath_en", "enable/disable the FDD datapath and turn on/off Rx/Tx [enable|disable]", FreeSRP::SET_DATAPATH_EN}
    };

    const vector<param_def> get_params = {
        {"tx_lo", "get transmitter local oscillator frequency [Hz]", FreeSRP::GET_TX_LO_FREQ},
        {"tx_samp", "get transmitter sample rate [Hz]", FreeSRP::GET_TX_SAMP_FREQ},
        {"tx_bw", "get transmitter bandwidth [Hz]", FreeSRP::GET_TX_RF_BANDWIDTH},
        {"tx_atten", "get transmitter attenuation [milli-dB]", FreeSRP::GET_TX_ATTENUATION},
        {"tx_fir_en", "get transmitter FIR filter status [enabled|disabled]", FreeSRP::GET_TX_FIR_EN},
        {"rx_lo", "get receiver local oscillator frequency [Hz]", FreeSRP::GET_RX_LO_FREQ},
        {"rx_samp", "get receiver sample rate [Hz]", FreeSRP::GET_RX_SAMP_FREQ},
        {"rx_bw", "get receiver bandwidth [Hz]", FreeSRP::GET_RX_RF_BANDWIDTH},
        {"rx_gc", "get receiver gain control mode [??]", FreeSRP::GET_RX_GC_MODE},
        {"rx_gain", "get receiver gain [milli-dB]", FreeSRP::GET_RX_RF_GAIN},
        {"rx_fir_en", "get receiver FIR filter status [enabled|disabled]", FreeSRP::GET_RX_FIR_EN}
    };

    int cmd_set(const FreeSRP::FreeSRP &srp, vector<string> &params)
    {
        if(params.size() == 0)
        {
            // No parameters specified. Print help message.

            cout << "Usage: set [param] [value]" << endl;
            cout << "[param]: Name of the parameter to set" << endl;
            cout << "[value]: Value to set the parameter to" << endl;
            cout << "Type 'set params' for a list of parameters." << endl;
        }

        if(params.size() == 1)
        {
            // Parameter without value specified
            if(params[0] == "params")
            {
                // List available parameters
                cout << left;
                cout << setw(12) << "Parameter" << setw(100) << "Description" << endl;
                cout << setw(12) << "---------" << setw(100) << "-----------" << endl;
                for(const param_def &param : set_params)
                {
                    cout << setw(12) << param.name << setw(100) << param.descr << endl;
                }
            }
            else
            {
                cout << "Please specify a parameter and a value" << endl;
            }
        }

        if(params.size() == 2)
        {
            bool not_found = true;

            for(const param_def &def : set_params)
            {
                if(def.name == params[0])
                {
                    not_found = false;
                    try
                    {
                        double value = boost::lexical_cast<double>(params[1]);
                        FreeSRP::command cmd = srp.make_command(def.id, value);
                        FreeSRP::response res = srp.send_cmd(cmd);
                        if(res.error != FreeSRP::CMD_OK)
                        {
                            cerr << "FreeSRP reported error " << res.error << " setting the parameter" << endl;
                        }
                        else
                        {
                            cout << def.name << " = " << res.param << endl;
                        }
                    }
                    catch(boost::bad_lexical_cast)
                    {
                        cout << "'" << params[1] << "' is not a valid numerical value!" << endl;
                    }

                    break;
                }
            }

            if(not_found)
            {
                cout << "Invalid parameter. Type 'set params' for a list of available parameters." << endl;
            }
        }

        return 0;
    }

    int cmd_get(const FreeSRP::FreeSRP &srp, vector<string> &params)
    {
        if(params.size() == 0)
        {
            // No parameters specified. Print help message.

            cout << "Usage: get [param]" << endl;
            cout << "[param]: Name of the parameter to set" << endl;
            cout << "Type 'get params' for a list of parameters." << endl;
        }

        if(params.size() == 1)
        {
            // Parameter without value specified
            if(params[0] == "params")
            {
                // List available parameters
                cout << left;
                cout << setw(12) << "Parameter" << setw(100) << "Description" << endl;
                cout << setw(12) << "---------" << setw(100) << "-----------" << endl;
                for(const param_def &param : get_params)
                {
                    cout << setw(12) << param.name << setw(100) << param.descr << endl;
                }
            }
            else
            {
                bool not_found = true;

                for(const param_def &def : get_params)
                {
                    if(def.name == params[0])
                    {
                        not_found = false;

                        FreeSRP::command cmd{def.id};
                        FreeSRP::response res = srp.send_cmd(cmd);
                        if(res.error != FreeSRP::CMD_OK)
                        {
                            cerr << "FreeSRP reported error " << res.error << " getting the parameter" << endl;
                        }
                        else
                        {
                            cout << def.name << " = " << res.param << endl;
                        }

                        break;
                    }
                }

                if(not_found)
                {
                    cout << "Invalid parameter. Type 'get params' for a list of available parameters." << endl;
                }
            }
        }

        return 0;
    }
}
