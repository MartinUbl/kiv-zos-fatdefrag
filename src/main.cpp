#include "global.h"
#include "pseudofat.h"

#define DEFAULT_INPUT_FILE "output.fat"
#define DEFAULT_OUTPUT_FILE "output.out.fat"

int verbose_output = 0;
bool matching_badblocks = false;
bool force_not_consistent = false;
uint8_t thread_count = 1;
bool writeout_only = false;
bool dump_result = false;
int program_mode = 0;

int read_mode(const char* filename)
{
    // create partition record
    fat_partition* partition = fat_partition::load_from_file(filename);
    if (!partition)
        return 1;

    cout << "Filesystem successfully loaded, proceeding with checks" << endl << endl;

    if (!partition->check_fattables())
        return 2;

    cout << "Caching data for future use..." << endl;
    partition->cache_counts();

    cout << "Filesystem is OK" << endl << endl;

    if (dump_result)
        partition->dump_contents();

    return 0;
}

int defrag_mode(const char* filename, const char* outfilename)
{
    // create partition record
    fat_partition* partition = fat_partition::load_from_file(filename);
    if (!partition)
        return 1;

    cout << "Filesystem successfully loaded, proceeding with checks" << endl << endl;

    if (!partition->check_fattables())
        return 2;

    cout << "Caching data for future use..." << endl;
    partition->cache_counts();

    cout << "All OK, ready to proceed with defragmentation" << endl << endl;

    if (!writeout_only)
    {
        if (!partition->defragment())
            return 3;

        if (!partition->save_to_file(outfilename))
            return 4;
    }

    if (dump_result || writeout_only)
        partition->dump_contents();

    return 0;
}

int create_mode(const char* outfilename, uint32_t cluster_count, uint32_t cluster_size, uint32_t fat_type, uint32_t fat_copies, uint32_t reserv_clust_count, const char* volumedesc, const char* signature)
{
    fat_partition* partition = fat_partition::create(volumedesc, fat_type, fat_copies, cluster_size, cluster_count, reserv_clust_count, signature);
    if (!partition)
        return 1;

    // test with: -mc -o bigfat.fat -cc 40000 -cs 16384 -ft 12 -fc 2 -vd "FATka jak vrata" -rc 10 -sig "OK"
    // -md -i bigfat.fat -v

    partition->cache_counts();

    cout << endl << "Requested filesystem successfully created" << endl;
    cout << "Awaiting commands. Type 'help' for list of available commands" << endl << endl;

    string command;
    bool saved = false;
    bool randomize_entry = false;
    int res, i;

    string loaded_filename, fname;
    uint32_t selected = 0, file_ender = FAT_FILE_END;
    FILE* loaded = nullptr;
    FILE* tmp;

    while (true)
    {
        cout << "> ";
        getline(cin, command);

        if (command == "help")
        {
            cout << "List of available commands:" << endl;
            cout << "  help         - prints this output" << endl
                 << "  load <file>  - loads file to be put into our filesystem" << endl
                 << "  put          - puts loaded file into filesystem" << endl
                 << "  select <id>  - selects cluster" << endl
                 << "  fileend <val>- sets file ending value" << endl
                 << "  random on/off- turns randomizing for files on/off" << endl
                 << "  save         - saves filesystem image to specified file" << endl
                 << "  exit         - exits program" << endl
                 << "  quit         - also exits program" << endl;
        }
        else if (command.substr(0, 6) == "random")
        {
            if (command.length() < 8)
            {
                cerr << "Please, specify randomness mode - on / off" << endl;
            }
            else
            {
                fname = command.substr(7);
                if (fname == "on")
                {
                    cout << "Randomizing entries turned ON" << endl;
                    randomize_entry = true;
                }
                else if (fname == "off")
                {
                    cout << "Randomizing entries turned OFF" << endl;
                    randomize_entry = false;
                }
                else
                {
                    cerr << "Unknown randomness mode, use on / off" << endl;
                }
            }
        }
        else if (command.substr(0, 7) == "fileend")
        {
            if (command.length() < 9)
            {
                cout << "Resetting file ending value back to FAT_FILE_END" << endl;
                file_ender = FAT_FILE_END;
            }
            else
            {
                fname = command.substr(8);
                file_ender = atoi(fname.c_str());
            }
        }
        else if (command.substr(0, 4) == "load")
        {
            if (command.length() < 6)
            {
                cerr << "Please, specify valid filename in load command" << endl;
            }
            else
            {
                fname = command.substr(5).c_str();

                tmp = loaded;

                loaded = fopen(fname.c_str(), "rb");
                if (!loaded)
                {
                    cerr << "File " << fname.c_str() << " could not be found!" << endl;
                    loaded = tmp;
                }
                else
                {
                    if (tmp)
                    {
                        cout << "Closing file " << loaded_filename << endl;
                        fclose(tmp);
                    }
                    cout << "Loaded file " << fname << endl;
                    loaded_filename = fname;
                }
            }
        }
        else if (command.substr(0, 3) == "put")
        {
            if (loaded)
            {
                int cnt = 1;
                if (command.length() > 4)
                    cnt = atoi(command.substr(4).c_str());
                if (!cnt)
                    cnt = 1;

                for (i = 0; i < cnt; i++)
                {
                    res = partition->write_source_file(loaded, loaded_filename.c_str(), selected, randomize_entry, 0, file_ender);

                    if (res == 0)
                        cout << "Successfullly written file by specified rules" << endl;
                    else if (res == 3)
                    {
                        cout << "No space left on (pseudo)device" << endl;
                        break;
                    }
                }
            }
            else
            {
                cerr << "At first, you must load valid file with 'load' command" << endl;
            }
        }
        else if (command == "save")
        {
            cout << "Saving image to file " << outfilename << "..." << endl;
            if (!partition->save_to_file(outfilename))
                cerr << "Could not save image to file!" << endl;
            else
            {
                saved = true;
                cout << "OK" << endl;
            }
        }
        else if (command == "exit" || command == "quit")
        {
            if (!saved)
            {
                cout << "Unsaved changes will be lost. Proceed? Y/N ";
                cin >> command;

                if (command == "Y" || command == "y")
                    break;
                // we consider anything else to be "no"
            }
            else
                break;
        }
    }

    if (loaded)
        fclose(loaded);

    return 0;
}

int main(int argc, char** argv)
{
    int i;

    cout << "KIV/ZOS: pseudoFAT manipulation" << endl;
    cout << "Program version: " << VERSION_PROGRAM << endl;
    cout << "PseudoFAT structure version: " << VERSION_PSEUDOFAT << endl;
    cout << "Author: Martin UBL (A13B0453P)" << endl;
    cout << "=========================================" << endl;
    cout << endl;

    std::string filename("");
    std::string outfilename("");

    uint32_t cluster_count = 0;
    uint32_t cluster_size = 512;
    uint32_t fat_type = 12;
    uint32_t fat_count = 2;
    std::string vol_descriptor = "NEW VOLUME";
    uint32_t reserved_clusters = 0;
    std::string signature = "OK";

    program_mode = -1;
    /* Table of relevant arguments:
     *   all modes
     *      -v          - verbose output
     *      -vv         - more verbose output (for suicidal cases)
     *      -t <1;16>   - number of worker threads  - default 1
     *      -f          - force recoverable errors ignore
     *      -m          - matching mode for badblocks (recover from another FAT table)
     *      -w          - if some changes would be made, do not write it into file, or so
     *
     *   -mc mode
     *      -cc <1;x>   - cluster count
     *      -cs <1;x>   - cluster size              - default 512
     *      -ft <type>  - FAT type (12, 16, 32)     - default 12
     *      -fc <count> - FAT table count           - default 2
     *      -vd <str>   - volume descriptor         - default "NEW VOLUME"
     *      -rc <count> - reserved cluster count    - default 10
     *      -sig <s>    - signature (OK, NOK)       - default "OK"
     *      -o <file>   - output filename           - default DEFAULT_OUTPUT_FILE macro value
     *
     *   -mr mode
     *      -i <file>   - input filename            - default DEFAULT_INPUT_FILE macro value
     *      -d          - dump results
     *
     *   -md mode
     *      -i <file>   - input filename            - default DEFAULT_INPUT_FILE macro value
     *      -o <file>   - output filename           - default DEFAULT_OUTPUT_FILE macro value
     */

    for (i = 1; i < argc; i++)
    {
        if (strcmp("-f", argv[i]) == 0)
        {
            cout << "Forced ignoring recoverable errors" << endl;
            force_not_consistent = true;
        }
        else if (strcmp("-v", argv[i]) == 0)
        {
            cout << "Verbose mode for log outputs" << endl;
            verbose_output = 1;
        }
        else if (strcmp("-vv", argv[i]) == 0)
        {
            cout << "Suicidal verbose mode for log outputs" << endl;
            verbose_output = 2;
        }
        else if (strcmp("-m", argv[i]) == 0)
        {
            cout << "Matching mode for badblocks" << endl;
            matching_badblocks = true;
        }
        else if (strcmp("-w", argv[i]) == 0)
        {
            cout << "Using only writeout mode - no changes will be made" << endl;
            writeout_only = true;
        }
        else if (strcmp("-d", argv[i]) == 0)
        {
            cout << "Will dump result at the end" << endl;
            dump_result = true;
        }
        else if (strcmp("-mr", argv[i]) == 0)
        {
            cout << "Running in read mode" << endl;
            program_mode = PROGRAM_MODE_READ;
        }
        else if (strcmp("-md", argv[i]) == 0)
        {
            cout << "Running in defragmentation mode" << endl;
            program_mode = PROGRAM_MODE_DEFRAG;
        }
        else if (strcmp("-mc", argv[i]) == 0)
        {
            cout << "Running in creation mode" << endl;
            program_mode = PROGRAM_MODE_CREATE;
        }
        else if (strcmp("-i", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                filename = argv[i + 1];
                i++;

                cout << "Using input file: " << filename << endl;
            }
            else
            {
                cerr << "Error: input file not specified after parameter -i" << endl;
                cerr << "Please, specify input file using -i <myfile.fat>" << endl;
                return 3;
            }
        }
        else if (strcmp("-o", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                outfilename = argv[i + 1];
                i++;

                cout << "Using output file: " << outfilename << endl;
            }
            else
            {
                cerr << "Error: output file not specified after parameter -o" << endl;
                cerr << "Please, specify output file using -o <myoutput.fat>" << endl;
                return 3;
            }
        }
        else if (strcmp("-t", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                thread_count = atoi(argv[i + 1]);
                i++;

                if (thread_count < 1 || thread_count > 16)
                {
                    cerr << "Invalid thread count, falling back to 1 thread" << endl;
                    thread_count = 1;
                }
            }
            else
            {
                cerr << "Error: thread count not specified after -t" << endl;
                cerr << "Please, specify thread count using -t <1;16>" << endl;
                return 3;
            }
        }
        else if (strcmp("-cc", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                cluster_count = atoi(argv[i + 1]);
                i++;

                if (cluster_count < 1)
                {
                    cerr << "Invalid cluster count, must be greater than 0!" << endl;
                    cluster_count = 0;
                }
            }
            else
            {
                cerr << "Error: cluster count not specified after -cc" << endl;
                cerr << "Please, specify cluster count using -cc <count>" << endl;
                return 3;
            }
        }
        else if (strcmp("-rc", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                reserved_clusters = atoi(argv[i + 1]);
                i++;

                if (reserved_clusters < 0)
                {
                    cerr << "Invalid reserved cluster count, must be greater than or equal 0!" << endl;
                    reserved_clusters = 0;
                }
            }
            else
            {
                cerr << "Error: reserved cluster count not specified after -rc" << endl;
                cerr << "Please, specify reserved cluster count using -rc <count>" << endl;
                return 3;
            }
        }
        else if (strcmp("-cs", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                cluster_size = atoi(argv[i + 1]);
                i++;

                if (cluster_size < 1)
                {
                    cerr << "Invalid cluster size, must be greater than 0!" << endl;
                    cluster_size = 0;
                }
            }
            else
            {
                cerr << "Error: cluster size not specified after -cs" << endl;
                cerr << "Please, specify cluster size using -cs <size in bytes>" << endl;
                return 3;
            }
        }
        else if (strcmp("-ft", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                fat_type = atoi(argv[i + 1]);
                i++;

                if (fat_type != 12 && fat_type != 16 && fat_type != 32)
                {
                    cerr << "Invalid FAT type, must be 12, 16 or 32! Falling back to FAT12" << endl;
                    fat_type = 12;
                }
            }
            else
            {
                cerr << "Error: FAT type not specified after -ft" << endl;
                cerr << "Please, specify FAT type using -ft <type>" << endl;
                return 3;
            }
        }
        else if (strcmp("-fc", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                fat_count = atoi(argv[i + 1]);
                i++;

                if (fat_count < 1)
                {
                    cerr << "Invalid FAT table count, must be at least 1! Using 1 FAT table" << endl;
                    fat_count = 1;
                }
            }
            else
            {
                cerr << "Error: FAT table count not specified after -fc" << endl;
                cerr << "Please, specify FAT table count using -fc <count>" << endl;
                return 3;
            }
        }
        else if (strcmp("-vd", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                vol_descriptor = argv[i + 1];
                i++;
            }
            else
            {
                cerr << "Error: volume descriptor not specified after parameter -vd" << endl;
                cerr << "Please, specify volume descriptor using -vd \"<volume descriptor>\" " << endl;
                return 3;
            }
        }
        else if (strcmp("-sig", argv[i]) == 0)
        {
            if (argc > i + 1)
            {
                signature = argv[i + 1];
                i++;

                if (strcmp("OK", signature.c_str()) != 0 && strcmp("NOK", signature.c_str()) != 0)
                {
                    cerr << "Invalid signature, must be eighter \"OK\" or \"NOK\"";
                }
            }
            else
            {
                cerr << "Error: signature not specified after parameter -sig" << endl;
                cerr << "Please, specify volume descriptor using -sig \"<signature>\" " << endl;
                return 3;
            }
        }
    }

    if (program_mode < 0)
    {
        cout << "Mode not specified. Please, specify mode using one of following parameters: " << endl;
        cout << "    -mr    read mode" << endl
             << "    -md    defragmentation mode" << endl
             << "    -mc    creation mode" << endl;

        return 5;
    }

    if (filename.length() == 0 && program_mode != PROGRAM_MODE_CREATE)
    {
        cout << "No input file specified, falling back to " << DEFAULT_INPUT_FILE << endl;
        filename = DEFAULT_INPUT_FILE;
    }
    if (outfilename.length() == 0 && program_mode != PROGRAM_MODE_READ)
    {
        cout << "No output file specified, falling back to " << DEFAULT_OUTPUT_FILE << endl;
        outfilename = DEFAULT_OUTPUT_FILE;
    }
    cout << "Using " << (uint32_t)thread_count << " worker threads" << endl;


    switch (program_mode)
    {
        case PROGRAM_MODE_READ:
            read_mode(filename.c_str());
            break;
        case PROGRAM_MODE_DEFRAG:
            defrag_mode(filename.c_str(), outfilename.c_str());
            break;
        case PROGRAM_MODE_CREATE:
            create_mode(outfilename.c_str(), cluster_count, cluster_size, fat_type, fat_count, reserved_clusters, vol_descriptor.c_str(), signature.c_str());
            break;
    }

    return 0;
}
