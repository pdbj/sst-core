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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "sst/core/env/envconfig.h"
#include "sst/core/env/envquery.h"

// listModels argument which determines what will be returned:
enum class listOption
    {
        NONE    = 0,  // Return a nullptr
        ALL     = 1,  // Return a vector containing all of the registered components (both valid and invalid)
        INVALID = 2   // Return a vector containing only the INVALID components
    };

// Function declarations
static void              print_usage();
void                     checkConfigPath();
void                     sstRegister(std::string groupName, std::string keyValPair);
void                     sstUnregister(const std::string& element);
std::vector<std::string> listModels(listOption option);
void                     sstUnregisterMultiple(std::vector<std::string> elementsArray);
void                     autoUnregister();
bool                     validModel(const std::string& s);
void                     showListing();

// Global constants
const std::string START_DELIMITER = "[";
const std::string STOP_DELIMITER  = "]";
// Global path to configuration file
std::string cfgPath;

// Debugging output
#if 0
#  define DEBUG(msg) std::cout << "DBG: " << msg << std::endl
#else
#  define DEBUG(msg)
#endif

int
main(int argc, char* argv[])
{
    // Which main function is being invoked
    // We defer actually invoking them until we've got the -L arg
    int cmd_i {0};
    int cmd_l {0};
    int cmd_m {0};
    int cmd_u {0};
    std::string u_arg;

    { // Handle  args
        static struct option long_options[] =
            {
                {"help",         no_argument,        0,     'h'},
                {"invalid",      no_argument,       &cmd_i,  1},
                {"list",         no_argument,       &cmd_l,  1},
                {"lib-path",     required_argument,  0,     'L'},
                {"multi",        no_argument,       &cmd_m,  1},
                {"unregister",   required_argument,  0,     'u'},
                {nullptr,        0 ,                 0,      0 }
            };

        while (1) {
            const int intC = getopt_long(argc, argv, ":hilL:mu:", long_options, 0);
            if (intC == -1)
                break;

            switch (intC) {
            case 0:  break; // no more options

            case 'i':  cmd_i = 1;  DEBUG("invalid");  break;
            case 'l':  cmd_l = 1;  DEBUG("list")   ;  break;
            case 'm':  cmd_m = 1;  DEBUG("multi")  ;  break;

            case 'h':
		DEBUG("help");
                print_usage();
                exit(0);
                break;

            case 'L':
		DEBUG("lib-path got " << optarg);
                cfgPath = optarg;
                break;

            case 'u':
		DEBUG("unregister");
                cmd_u = true;
                u_arg = optarg;
                break;

            case '?':
		DEBUG("Unknown options: " << (char)optopt);
                break;

            case ':':
		DEBUG("Missing arg: " << (char)optopt);
                // fall through

            default:
		DEBUG("Unknown response: " << (char)intC);
                print_usage();
                exit(-1);

            }  // switch
        }  // while
    }  // Handle args

    if (cmd_i) autoUnregister();
    if (cmd_u) sstUnregister(u_arg);
    if (cmd_m) {
        std::vector<std::string> elementsArray;
        sstUnregisterMultiple(elementsArray);
    }
    if (cmd_l)  showListing();

    // If we did any of the optional commands, we're done
    if (cmd_i || cmd_l || cmd_m || cmd_u)
        exit(0);

    // Take the default register action with the remaining args

    // optind is provided by getopt.h
    if (optind + 2 > argc) {
        std::cerr << "Registration arguments are missing!" << std::endl;
        print_usage();
        exit(-1);
    }

    sstRegister(argv[optind], argv[optind + 1]);

    return 0;
}

//checkConfigPath
//Searches for the config file in a number of places.
//Exits program with an error if no config file can be found.
//Input: cfgPath
//Returns: none
void checkConfigPath()
{
    //Check for a valid configuration file
    static bool checked {false};
    if (checked) return;

    std::string libPath;
    std::string defPath;
    std::string homePath;

    FILE* cfgFile {nullptr};

    if (cfgPath.size())
        {
            // cfgPath was set by --lib-path option
            // Save it in case we have to include it in the error message
            libPath = cfgPath;
            cfgFile = fopen(cfgPath.c_str(), "r+");
        }
    if (nullptr == cfgFile)
        {
            defPath = SST_INSTALL_PREFIX + std::string("/etc/sst/sstsimulator.conf");
            cfgPath = defPath;
            cfgFile = fopen(cfgPath.c_str(), "r+");
        }
    if(nullptr == cfgFile) {
        std::string envHome {getenv("HOME")};

        if(!envHome.size()) {
            homePath = "~/.sst/sstsimulator.conf";
        } else {
            homePath = envHome + "/.sst/sstsimulator.conf";
        }
        cfgPath = homePath;
        cfgFile = fopen(cfgPath.c_str(), "r+");
    }
    if(nullptr == cfgFile) {
        std::cerr << "Unable to open configuration at any of the following paths:\n";
        if (libPath.size())
            std::cerr << "  --lib-path argument: " << libPath;
        std::cerr << "\n  " << defPath
                  << "\n  " << homePath
                  << "\nOne of those files must be editable."
                  << std::endl;
        exit(-1);
    }

    // Ok, we've got a good cfgPath
    fclose(cfgFile);
    std::cout << "Using config path " << cfgPath << std::endl;
    checked = true;
}

//sstRegister
//Registers a model with SST. Puts model name and location in the sst config file
//Input: char pointer to the command line arguments
void sstRegister(std::string groupName, std::string keyValPair){
    checkConfigPath();
    std::cout << "Registering in " << groupName
              << " the key-value: " << keyValPair
              << std::endl;
    size_t equalsIndex = keyValPair.find("=");

    std::string key   = keyValPair.substr(0, equalsIndex);
    std::string value = keyValPair.substr(equalsIndex + 1);

    SST::Core::Environment::EnvironmentConfiguration* database = new SST::Core::Environment::EnvironmentConfiguration();

    FILE* cfgFile = fopen(cfgPath.c_str(), "r+");
    populateEnvironmentConfig(cfgFile, database, true);

    database->getGroupByName(groupName)->setValue(key, value);
    fclose(cfgFile);

    cfgFile = fopen(cfgPath.c_str(), "w+");

    if(nullptr == cfgFile) {
        std::cerr << "Unable to open: " << cfgFile << " for writing."
                  << std::endl;
        exit(-1);
    }

    database->writeTo(cfgFile);

    fclose(cfgFile);
}

// sstUnregister
// Takes a string argument and searches the sstsimulator config file for that name.
// Removes the component from the file - unregistering it from SST
// Input: command line arguments
void
sstUnregister(const std::string& element)
{
    std::string str1;
    std::string s = "";
    std::string tempfile;
    int         found = 0;

    checkConfigPath();
    //setup element names to look for
    str1 = START_DELIMITER + element + STOP_DELIMITER;
    tempfile = "/tmp/sstsimulator.conf";

    std::ifstream infile(cfgPath);
    std::ofstream outfile(tempfile);

    // grab each line and compare to element name stored in str1
    // if not the same, then print the line to the temp file.
    while ( getline(infile, s) ) {
        if ( str1 == s ) {
            found = 1;
            // Grab the _LIBDIR= line to remove it from config
            getline(infile, s);
        }
        else
            outfile << s << "\n";
    }

    if ( found ) { std::cout << "\tModel " << element << " has been unregistered!\n"; }
    else
        std::cout << "Model " << element << " not found\n\n";

    infile.close();
    outfile.close();
    rename(tempfile.c_str(), cfgPath.c_str());
}

//listModels
//Prints to STDOUT all of the registered models
//Input: a listOption enum value that determines what will be returned:
//  NONE    = 0,  // Return a nullptr
//  ALL     = 1,  // Return a vector containing all of the registered components (both valid and invalid)
//  INVALID = 2   // Return a vector containing only the INVALID components
//Returns: a vector of strings.
std::vector<std::string> listModels(listOption option){
    std::string s;
    std::string section;
    std::vector<std::string> elements;
    // Variables controlling output
    bool output = (option != listOption::INVALID);
    int  count {0};  // Count of items output, for printing the list
    bool found {false};  // Whether we have found any models
    bool sectionOut {false};

    std::ifstream infile(cfgPath);

    //Begin search of sstconf for models
    getline(infile, s);
    while(infile.good()){
        std::size_t start = s.find(START_DELIMITER);

        if(start == std::string::npos){
	    getline(infile, s);
	    continue;
	}

        std::size_t stop = s.find(STOP_DELIMITER);
        section = s.substr(start+1,stop-(start+1));//The +1 removes the brackets from substring
	sectionOut = false;

        //disregard SSTCore and default
        if(section == "SSTCore" || section == "default"){
	    getline(infile, s);
	    continue;
	}
	// Found an interesting section

	//check if the model is valid by confirming it is located in the path registered in the sst config file
	getline(infile, s);
	while (infile.good()){
	    start = s.find(START_DELIMITER);
	    if (start != std::string::npos)
		break;

	    std::size_t equal = s.find("=");
	    if (equal == std::string::npos) {
		getline(infile, s);
		continue;
	    }

	    // Found a model
	    std::string model = s.substr(0, equal);
	    std::string path = s.substr(equal + 1);
	    bool valid = validModel(path);

	    if (output && !found){
		found = true;
		std::cout << "\nList of registered models by section:\n";
	    }
	    if (output && !sectionOut) {
		sectionOut = true;
		std::cout << "Section " << section << "\n";
	    }
	    if (output) {
		std::cout << count++ << ". "
			  << std::setw(35) << std::left << model
			  << (valid ? "VALID" : "INVALID")
			  << std::endl;
	    }

	    if (option == listOption::INVALID && !valid)
		elements.push_back(model);
	    else if(option == listOption::ALL)
		elements.push_back(model);

	    // And finally go the next line
	    getline(infile, s);

	}  // while model
    }  // while section

    if (output && !found)
        std::cout << "No models registered\n";

    infile.close();

    return elements;
}

//showListing
//List the models
//Input: none
//Returns: none
void showListing()
{
    checkConfigPath();
    std::cout << "\nA model labeled INVALID means it is registered in\n"
              << "SST, but no longer exists in the specified path.\n";
    listModels(listOption::NONE);
}

//sstUnregisterMultiple
//Lists the registered models and gives the user the
//option to choose multiple models to unregister.
//Input: a vector of strings
void sstUnregisterMultiple(std::vector<std::string> elementsArray){
    unsigned temp;
    std::vector<int> elementsToRemove;
    std::string      line;

    checkConfigPath();
    std::cout << "\nChoose which models you would like to unregister.\n"
              << "Separate your choices with a space. Ex: 1 2 3\n"
              << "Note: This does not delete the model files."
              << std::endl;
    elementsArray = listModels(listOption::ALL);
    if (elementsArray.size() != 0){
        std::cout << "> ";
        getline(std::cin,line);
        std::stringstream ss(line);
        // push the options chosen to a vector
        while ( ss >> temp ) {
            // Check for valid inputs
            if ( temp > elementsArray.size() ) {
                std::cerr << "\nError: A number you entered is not in the list.\n";
                exit(-1);
            }
            elementsToRemove.push_back(temp);
        }
        // go through the vector of items to be removed and unregister them
        for ( unsigned i = 0; i < elementsToRemove.size(); i++ )
            sstUnregister(elementsArray[elementsToRemove[i] - 1]); //-1 because our displayed list starts at 1 and not 0
    }
    else
        std::cout << "Nothing to unregister.\n\n";
}

// validModel
// Checks the path of the model to determine if it physically exists on the drive
// Input: a string containing the path
// Returns: a true or false
bool
validModel(const std::string& s)
{
    std::size_t locationStart = s.find("/");

    if (locationStart == std::string::npos) {
	DEBUG("invalid: no slash");
	return false;
    }

    std::string path = s.substr(locationStart);//grabs the rest of the line from / to the end
    DEBUG("Checking path '" << path << "'");
    struct stat statbuf;
    if(stat(path.c_str(), &statbuf) != -1){
        if(S_ISDIR(statbuf.st_mode)){
            return true;
	}else{
	    DEBUG("invalid: not dir");
	}
    }
    DEBUG("invalid: stat(path) failed");

    return false;
}

//autoUnregister
//Unregisters all INVALID components from the SST config file
//Input: none
//Returns: none
void autoUnregister(){
    checkConfigPath();
    std::cout << "Unregistering all INVALID components" << std::endl;
    std::vector<std::string> elementsArray = listModels(listOption::INVALID);
    for(unsigned i = 0; i < elementsArray.size(); i++){
        sstUnregister(elementsArray[i]);
    }
}

//print_usage
//Displays proper syntax to be used when running the feature
//Input: none
//Returns: none
void print_usage() {
    std::cout << "Usage: sst-register [options] [arguments]\n\n"
              << "Options:\n"
              << "  -h, --help                Print Help Message\n"
              << "  -i, --invalid             List all invalid components\n"
              << "  -l, --list                List all registered components\n"
              << "  -L, --lib-path=LIBPATH    Register to LIBPATH\n"
              << "  -m, --multi               Unregister multiple components\n"
              << "                            You will be prompted for the component numbers\n"
              << "  -u, --unregister=COMP     Unregister a specific component\n\n"
              << "In any listings a model labeled INVALID means it is registered in\n"
              << "SST, but no longer exists in the specified path.\n\n"
              << "Additional arguments are used to register a component:\n\n"
              << "    sst-register <Dependency Name> (<VAR>=<VALUE>)*\n\n"
              << "<Dependency Name>   : Name of the Third Party Dependency\n"
              << "<VAR>=<VALUE>       : One or more onfiguration variables and\n"
              << "associated value to add to registry.\n"
              << "If <VAR>=<VALUE> pairs are not provided, the tool will attempt\n"
              << "to auto-register $PWD/include and $PWD/lib to the name\n\n"
              << "Example: \n\n"
              << "    sst-register DRAMSim CPPFLAGS=\"-I$PWD/include\"\n\n"
              << std::endl;
}
