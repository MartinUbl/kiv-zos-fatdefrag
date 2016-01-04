#include "global.h"
#include "pseudofat.h"

#include <map>
#include <string>
#include <vector>

bool fat_partition::_check_cluster_chain_everywhere(uint32_t start_cluster)
{
    uint32_t clusternow = start_cluster;
    int i;
    uint32_t limit_counter = 0;

    while (clusternow != FAT_FILE_END)
    {
        // check for FAT tables consistency
        // start from index 1 due to comparison with 0th table
        for (i = 1; i < bootrec->fat_copies; i++)
        {
            // if the record is not equal
            if (fat_tables[0][clusternow] != fat_tables[i][clusternow])
            {
                // one of blocks is marked as BAD_CLUSTER
                if (fat_tables[0][clusternow] == FAT_BAD_CLUSTER || fat_tables[i][clusternow] == FAT_BAD_CLUSTER)
                {
                    // when not matching bad blocks, return
                    if (!matching_badblocks)
                    {
                        cout << "File contains errors, could not proceed. Use -m for attempt to recover badblocks in file chains" << endl;
                        return false;
                    }
                    else
                    {
                        // otherwise attempt to recover files from another FAT table
                        cout << "File contains errors, attempting recovery..." << endl;
                        // use primary FAT to restore backup ones, when primary is not damaged
                        if (fat_tables[0][clusternow] != FAT_BAD_CLUSTER)
                        {
                            cout << "Recovered using information from primary FAT table" << endl;
                            fat_tables[i][clusternow] = fat_tables[0][clusternow];
                        }
                        else // otherwise fix primary table using data from backup tables
                        {
                            cout << "Recovered using information from backup FAT table " << i << endl;
                            fat_tables[0][clusternow] = fat_tables[i][clusternow];
                        }
                    }
                }
            }
        }

        // when all FAT tables contains the same information about damaged file, report it and end processing
        if (fat_tables[0][clusternow] == FAT_BAD_CLUSTER)
        {
            cout << "File contains unrecoverable errors, could not proceed" << endl;
            return false;
        }

        // move to next cluster using primary FAT table
        clusternow = fat_tables[0][clusternow];

        limit_counter++;

        // detect loops
        if (limit_counter > bootrec->cluster_count)
            return false;
    }

    return true;
}

bool fat_partition::_check_fattables_files()
{
    int i;

    // go through all root directory files and check them for consistency across all FAT tables
    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        if (!_check_cluster_chain_everywhere(rootdir[i].first_cluster))
        {
            cout << "File " << rootdir[i].file_name << " is not consistent across FAT tables" << endl;
            return false;
        }
    }

    return true;
}

bool fat_partition::_check_fattables_equal()
{
    int incons = 0;
    uint32_t i;
    int32_t j;

    // check all cluster references
    for (i = 0; i < bootrec->cluster_count; i++)
    {
        // check in all fat copies
        for (j = 1; j < bootrec->fat_copies; j++)
        {
            // they has to be equal
            // if not, it is recognized as recoverable error (some "lost" file remaining on partition, etc.)
            if (fat_tables[0][i] != fat_tables[j][i])
            {
                cout << "Recoverable inconsistency at cluster " << i << " on FAT table " << i << endl;

                incons++;
                if (incons > MAX_RECOVERABLE_ERRORS)
                    return false;
            }
        }
    }

    return true;
}

bool fat_partition::check_fattables()
{
    cout << "Checking FAT tables consistency..." << endl;
    if (!_check_fattables_files())
    {
        cout << "Supplied filesystem is not in consistent state, exiting" << endl;
        return false;
    }

    cout << "Checking FAT tables for recoverable errors..." << endl;
    if (!_check_fattables_equal() && !force_not_consistent)
    {
        cout << "Too many recoverable inconsistencies per partition, use -f to force working with messed filesystem" << endl;
        return false;
    }

    return true;
}

void fat_partition::cache_counts()
{
    uint32_t i, j;

    // cache count of free clusters
    free_clusters_count = 0;
    for (i = 0; i < bootrec->cluster_count; i++)
    {
        if (fat_tables[0][i] == FAT_UNUSED)
            free_clusters_count++;
        else if (fat_tables[0][i] != FAT_BAD_CLUSTER)
            occupied_clusters_to_work.push_back(i);
    }

    // cache cluster chains
    rootdir_cluster_chains = new std::vector<uint32_t>[(uint32_t)bootrec->root_directory_max_entries_count];
    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        // go from first cluster
        j = rootdir[i].first_cluster;

        // and one-by-one push used clusters to cached structure (this will i.e. preserve cache locality in future search)
        do
        {
            rootdir_cluster_chains[i].push_back(j);
            j = fat_tables[0][j];
        } while (j != FAT_FILE_END);
    }
}

const char outputFileLetters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789<>$!+-*/°~#&@{}[]|^()=_;:§'%";

void fat_partition::dump_contents()
{
    int i, j;
    uint32_t k;
    char fileChar[2] = {0,0};
    int useNum;
    uint32_t currcluster;
    std::map<int, std::string> fileMap;

    int spacing = 1;

    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        useNum = 0;

        if (i < (int32_t)strlen(outputFileLetters))
            fileChar[0] = outputFileLetters[i];
        else
            fileChar[0] = '?';

        currcluster = rootdir[i].first_cluster;

        while (true)
        {
            fileMap[currcluster] = std::string(fileChar) + std::to_string(useNum++);

            if (currcluster == FAT_FILE_END)
                break;

            currcluster = fat_tables[0][currcluster];
        }

        useNum--;

        if (1 + useNum / 10 >= spacing)
            spacing = 2 + useNum / 10; // 1 = implicit spacing, 1 = offset (0 means 1 letter, 1 means 2 letters, etc.)
    }

    std::string spacingStr = "";
    for (i = 0; i < spacing; i++)
        spacingStr += " ";

    for (k = 0; k < bootrec->cluster_count; k++)
    {
        // if it's in file map, print it as sequenced short
        if (fileMap.find(k) != fileMap.end())
        {
            cout << fileMap[k];
            for (j = fileMap[k].length(); j <= spacing; j++)
                cout << " ";
        }
        // print "!" for bad cluster
        else if (fat_tables[0][k] == FAT_BAD_CLUSTER)
            cout << "!" << spacingStr;
        else // otherwise print "_" for unused cluster
            cout << "_" << spacingStr;

        if (k > 0 && k % 16 == 0)
            cout << endl;
    }

    cout << endl;
}
