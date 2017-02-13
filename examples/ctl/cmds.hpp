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