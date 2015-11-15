#ifndef FREESRP_CTL_CMDS
#define FREESRP_CTL_CMDS

#include <string>
#include <vector>
#include <functional>

#include <freesrp.hpp>

namespace cmds
{
    using namespace std;

    struct cmd_def
    {
        string cmd;
        string descr;
        function<int(const FreeSRP::FreeSRP &, vector<string> &)> func;
        bool exit;
    };

    int cmd_help(const FreeSRP::FreeSRP &srp, vector<string> &params);
    int cmd_set(const FreeSRP::FreeSRP &srp, vector<string> &params);
    int cmd_get(const FreeSRP::FreeSRP &srp, vector<string> &params);

    const vector<cmd_def> cmds = {
            {"help", "display this help message", cmds::cmd_help, false},
            {"exit", "exit this program", nullptr, true},
            {"set", "set a parameter", cmd_set, false},
            {"get", "get a parameter", cmd_get, false}
    };
}

#endif