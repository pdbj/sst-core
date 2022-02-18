// Copyright 2009-2022 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2022, NTESS
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "sst_config.h"

#include "sst/core/env/envconfig.h"
#include "sst/core/env/envquery.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <map>
#include <string>

void
print_usage(FILE* output)
{
    fprintf(output, "sst-config\n");
    fprintf(output, "sst-config --<KEY>\n");
    fprintf(output, "sst-config <GROUP> <KEY>\n");
    fprintf(output, "sst-config -L <LIBPATH> ...\n");
    fprintf(output, "\n");
    fprintf(output, "<GROUP>    Name of group to which the key belongs\n");
    fprintf(output, "           (e.g. DRAMSim group contains all DRAMSim\n");
    fprintf(output, "           KEY=VALUE settings).\n");
    fprintf(output, "<KEY>      Name of the setting key to find.\n");
    fprintf(output, "           If <GROUP> not specified this is found in\n");
    fprintf(output, "           the \'SSTCore\' default group.\n");
    fprintf(output, "<LIBPATH>  Additional configuration file to query\n");
    fprintf(output, "\n");
    fprintf(output, "Example 1:\n");
    fprintf(output, "  sst-config --CXX\n");
    fprintf(output, "           Finds the CXX compiler specified by the core\n");
    fprintf(output, "Example 2:\n");
    fprintf(output, "  sst-config DRAMSim CPPFLAGS\n");
    fprintf(output, "           Finds CPPFLAGS associated with DRAMSim\n");
    fprintf(output, "Example 3:\n");
    fprintf(output, "  sst-config\n");
    fprintf(output, "           Dumps entire configuration found.\n");
    fprintf(output, "\n");
    fprintf(output, "The use of -- for the single <KEY> (Example 1) is\n");
    fprintf(output, "intentional to closely replicate behaviour of the\n");
    fprintf(output, "pkg-config tool used in Linux environments. This\n");
    fprintf(output, "should not be specified when using <GROUP> as well.\n");
    fprintf(output, "\n");
    fprintf(output, "Return: 0 is key found, 1 key/group not found\n");
    exit(1);
}

int main(int argc, char* argv[]) {

    std::vector<std::string> overrideConfigFiles;

    std::string groupName("");
    std::string key("");
    std::string keyTemp("");

    { // Handle args
	static struct option long_options[] =
	    {
		{"help",     no_argument,       0, 'h'},
		{"lib-path", required_argument, 0, 'L'},
		{nullptr,    0,                 0,  0 }
	    };
	// Disable error reporting
	opterr = 0;

	while (1) {
	    const int intC = getopt_long(argc, argv, ":hL:", long_options, 0);

	    if (intC == -1)
		break;

	    switch (intC) {
	    case 0 :  break;  // no more options

	    case 'h':
		print_usage(stdout);
		exit(0);
		break;

	    case 'L':
		overrideConfigFiles.push_back(optarg);
		break;

	    case '?':  // fall through
	    case ':':  // fall through
	    default:
		// We don't wan't to have to specify all the valid options
		// --CC, --CXX, ...
		// So we don't treat this as an error, just record the
		// options.
		keyTemp = argv[optind - 1];
		groupName = "SSTCore";

	    }  // switch
	}  // while
    }  // Handle args

    // Number of non-options
    int nonOptions = argc - optind;

    bool dumpEnv = false;

    if(nonOptions == 0 && keyTemp.size() == 0) {
        dumpEnv = true;
    } else if(keyTemp.size() > 0) {
        if(keyTemp.size() < 3 || keyTemp.substr(0, 2) != "--") {
            fprintf(stderr, "Error: key (%s) is not specified with a group and doesn't start with --\n", keyTemp.c_str());
            print_usage(stderr);
            exit(-1);
        }
	key = keyTemp.substr(2);
    } else if (nonOptions == 2) {
        groupName = static_cast<std::string>(argv[optind++]);
        key       = static_cast<std::string>(argv[optind]);
    } else {
        fprintf(stderr, "Error: you specified an incorrect number of parameters\n");
        print_usage(stderr);
	exit(0);
    }

    SST::Core::Environment::EnvironmentConfiguration* database =
        SST::Core::Environment::getSSTEnvironmentConfiguration(overrideConfigFiles);
    bool keyFound = false;

    if ( dumpEnv ) {
        database->print();
        exit(0);
    }
    else {
        SST::Core::Environment::EnvironmentConfigGroup* group = database->getGroupByName(groupName);

        std::set<std::string> groupKeys = group->getKeys();

        for ( auto keyItr = groupKeys.begin(); keyItr != groupKeys.end(); keyItr++ ) {
            if ( key == (*keyItr) ) {
                printf("%s\n", group->getValue(key).c_str());
                keyFound = true;
                break;
            }
        }
    }

    delete database;

    // If key is found, we return 0 otherwise 1 indicates not found
    return keyFound ? 0 : 1;
}
