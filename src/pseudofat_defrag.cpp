#include "global.h"
#include "pseudofat.h"

bool fat_partition::_is_cluster_available(uint32_t id)
{
    return fat_tables[0][id] == FAT_UNUSED;
}

bool fat_partition::_is_cluster_bad(uint32_t id)
{
    return fat_tables[0][id] == FAT_BAD_CLUSTER;
}

uint32_t fat_partition::find_free_cluster_begin()
{
    uint32_t i;

    for (i = 0; i < real_cluster_count; i++)
    {
        if (fat_tables[0][i] == FAT_UNUSED)
            return i;
    }

    return FAT_UNUSED_NOT_FOUND;
}

uint32_t fat_partition::_find_free_cluster_end(int32_t end_offset)
{
    uint32_t i;

    // we use offset due to counter marked as "unsigned"
    // when we reach 0, we need to have 0 tested, and then return
    // this is far the simpliest solution (arithmetic operations
    // like addition are way faster than branching)

    for (i = real_cluster_count - end_offset; i > 0; i--)
    {
        if (fat_tables[0][i-1] == FAT_UNUSED)
            return i-1;
    }

    return FAT_UNUSED_NOT_FOUND;
}

bool fat_partition::_move_cluster(uint32_t source, uint32_t dest, int32_t thread_id)
{
    uint32_t i;
    int32_t j;

    if (verbose_output)
    {
        if (thread_id >= 0)
            cout << "Thread " << thread_id << ": ";
        cout << "Moving cluster " << source << " to " << dest << endl;
    }

    // find FAT entry referencing source cluster, and make it point to relocated one
    for (i = 0; i < real_cluster_count; i++)
    {
        if (fat_tables[0][i] == source)
        {
            for (j = 0; j < bootrec->fat_copies; j++)
            {
                // point to relocated cluster
                fat_tables[j][i] = dest;
            }

            break;
        }
    }

    // physically copy data
    //memcpy(clusters[dest], clusters[source], bootrec->cluster_size);
    uint8_t* ptr = clusters[source];
    clusters[source] = clusters[dest];
    clusters[dest] = ptr;

    // mark cluster as unused in all FAT tables
    for (j = 0; j < bootrec->fat_copies; j++)
    {
        // move reference in all fat tables
        fat_tables[j][dest] = fat_tables[j][source];
        fat_tables[j][source] = FAT_UNUSED;
    }

    // if the source cluster was the beginning of FAT entry chain, relocate it also
    // in root directory entry
    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        if (rootdir[i].first_cluster == source)
        {
            rootdir[i].first_cluster = dest;
            break;
        }
    }

    // just for debugging purposes
    //std::this_thread::sleep_for(std::chrono::milliseconds(5));

    return true;
}

uint32_t fat_partition::get_thread_work_cluster(int32_t retback)
{
    std::unique_lock<std::mutex> lock(assign_mtx);

    if (retback >= 0)
        occupied_clusters_to_work.push_back(retback);

    if (occupied_clusters_to_work.empty())
        return FAT_UNUSED_NOT_FOUND;

    uint32_t toret = occupied_clusters_to_work.front();

    occupied_clusters_to_work.pop_front();

    return toret;
}

void fat_partition::put_thread_work_cluster(int32_t retback)
{
    std::unique_lock<std::mutex> lock(assign_mtx);

    occupied_clusters_to_work.push_back(retback);
}

bool fat_partition::get_thread_work_cluster_reserve(uint32_t entry)
{
    std::unique_lock<std::mutex> lock(assign_mtx);

    for (auto itr = occupied_clusters_to_work.begin(); itr != occupied_clusters_to_work.end(); ++itr)
    {
        uint32_t a = *itr;
        if (a == entry)
        {
            itr = occupied_clusters_to_work.erase(itr);
            return true;
        }
    }

    return false;
}

uint32_t fat_partition::get_aligned_position(uint32_t current)
{
    uint32_t i, j;

    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        for (j = 0; j < rootdir_cluster_chains[i].size(); j++)
        {
            if (rootdir_cluster_chains[i][j] == current)
            {
                while (_is_cluster_bad(file_base_offsets[i] + j))
                    j++;

                return file_base_offsets[i] + j;
            }
        }
    }

    return (uint32_t)(-1);
}

void fat_partition::thread_process(uint32_t threadid)
{
    uint32_t entry, dest;
    int32_t toret = -1;

    while (true)
    {
        if (toret == -1)
        {
            entry = get_thread_work_cluster(toret);
            if (entry == FAT_UNUSED_NOT_FOUND)
                break;
        }
        else
            entry = toret;

        toret = -1;
        dest = get_aligned_position(entry);

        if (entry != dest)
        {
            if (_is_cluster_available(dest))
            {
                if (verbose_output)
                    cout << "Thread " << threadid << ": moving " << entry << " to " << dest << endl;

                std::unique_lock<std::mutex> lck(move_mtx);

                _move_cluster(entry, dest, threadid);
            }
            else
            {
                toret = entry;

                // if some thread already got it, try it again
                if (!get_thread_work_cluster_reserve(dest))
                {
                    // give our time to others
                    std::this_thread::yield();
                    continue;
                }

                if (verbose_output)
                    cout << "Thread " << threadid << ": returning: " << entry << ", retaking: " << dest << endl;

                // return entry back
                put_thread_work_cluster(entry);

                toret = dest;
            }
        }
    }
}

void defrag_thread_fnc(fat_partition* partition, uint32_t thread_id)
{
    partition->thread_process(thread_id);
}

bool fat_partition::defragment()
{
    uint32_t i, tmp;
    free_space_size = real_cluster_count / MIN_DEFRAG_FREE_FRACTION;
    // check free space
    if (free_clusters_count < free_space_size)
    {
        cout << "Not enough free space for defragmentation, please, make sure at least " << round(100.0f/(float)MIN_DEFRAG_FREE_FRACTION) << "% of disk is free" << endl;
        return false;
    }

    cout << "Defragmenting..." << endl << endl;

    // cache file cluster count - this is the resulting size of file block
    uint32_t currBase = 0, oldBase = 0;
    file_base_offsets = new uint32_t[(uint32_t)bootrec->root_directory_max_entries_count];
    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        file_base_offsets[i] = currBase;
        oldBase = currBase;
        currBase += (uint32_t)((rootdir[i].file_size / bootrec->cluster_size)+1);

        for (tmp = oldBase; tmp < currBase; tmp++)
        {
            if (_is_cluster_bad(tmp))
                currBase++;
        }
    }

    // create worker pool
    std::thread** workers = new std::thread*[thread_count];
    // create worker threads
    for (i = 0; i < thread_count; i++)
        workers[i] = new std::thread(defrag_thread_fnc, this, i);

    // join every thread, and when it's dead, delete it
    for (i = 0; i < thread_count; i++)
    {
        workers[i]->join();
        delete workers[i];
    }

    // cleanup
    delete[] workers;
    delete[] file_base_offsets;

    return true;
}
