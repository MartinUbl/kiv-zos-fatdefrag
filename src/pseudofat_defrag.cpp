#include "global.h"
#include "pseudofat.h"

bool fat_partition::_is_cluster_available(uint32_t id)
{
    return fat_tables[0][id] == FAT_UNUSED;
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

void fat_partition::_move_cluster(uint32_t source, uint32_t dest, int32_t thread_id)
{
    uint32_t i;
    int32_t j;

    if (verbose_output)
    {
        if (thread_id >= 0)
            cout << "Thread " << thread_id << ": ";
        cout << "Moving cluster " << source << " to " << dest << endl;
    }

    // physically copy data
    //memcpy(clusters[dest], clusters[source], bootrec->cluster_size);
    uint8_t* ptr = clusters[source];
    if (ptr == nullptr)
    {
        cout << "retard alert" << endl;
    }
    clusters[source] = clusters[dest];
    clusters[dest] = ptr;

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

    //std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void fat_partition::get_entry_params(uint32_t entry, uint32_t &base_offset, uint32_t &start_cluster)
{
    base_offset = file_base_offsets[entry];
    start_cluster = rootdir[entry].first_cluster;
}

uint32_t fat_partition::get_thread_work_entry(uint32_t prevEntry)
{
    std::unique_lock<std::mutex> lock(assign_mtx);

    if (prevEntry != FAT_UNUSED)
        mark_unassign_entry(prevEntry);

    if (process_index >= bootrec->root_directory_max_entries_count)
        return FAT_UNUSED;

    mark_assign_entry(process_index);

    return process_index++;
}

bool fat_partition::is_currently_assigned(uint32_t entry)
{
    std::unique_lock<std::mutex> lock(curr_assigned_mtx);

    return currently_assigned.find(entry) != currently_assigned.end();
}

void fat_partition::mark_assign_entry(uint32_t entry)
{
    std::unique_lock<std::mutex> lock(curr_assigned_mtx);

    currently_assigned.insert(entry);
}

void fat_partition::mark_unassign_entry(uint32_t entry)
{
    std::unique_lock<std::mutex> lock(curr_assigned_mtx);

    currently_assigned.erase(entry);
}

uint32_t fat_partition::_get_aligned_file_entry(uint32_t cluster)
{
    int64_t i;
    uint32_t currBase = 0;

    if (cluster == 0 && bootrec->root_directory_max_entries_count > 0)
        return 0;

    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        if (cluster < file_base_offsets[i])
            return i - 1;
    }

    return FAT_UNUSED_NOT_FOUND;
}

uint32_t fat_partition::_get_aligned_thread_storage(uint32_t cluster)
{
    uint32_t clcount = (real_cluster_count / MIN_DEFRAG_FREE_FRACTION);
    uint32_t dec_base = real_cluster_count - clcount;

    if (cluster < dec_base)
        return FAT_UNUSED_NOT_FOUND;

    return bootrec->root_directory_max_entries_count + (real_cluster_count - cluster) / (clcount / thread_count);
}

uint32_t fat_partition::_get_supercluster_index(uint32_t entry)
{
    return entry / ((free_clusters_count - free_space_size) / CLUSTERS_PER_SUPERCLUSTER);
}

uint32_t fat_partition::_get_mutex_lock_index(uint32_t cluster)
{
    uint32_t target;

    target = _get_aligned_file_entry(cluster);
    if (target != FAT_UNUSED_NOT_FOUND)
        return target;

    target = _get_aligned_thread_storage(cluster);
    if (target != FAT_UNUSED_NOT_FOUND)
        return target;

    target = _get_supercluster_index(cluster);

    return bootrec->root_directory_max_entries_count + thread_count + target;
}

void fat_partition::thread_process(uint32_t threadid)
{
    uint32_t entry, nowcluster, file_base, tmp, lockindex, lockindex2, lockindex3, target;
    bool suppressUnlock = false;

    // lockindex  = major lock, following current cluster block
    // lockindex2 = minor lock, locking source/target cluster (depending on situation)
    // lockindex3 = subminor lock, locking our thread storage

    // split reserved free space to thread-reserved subspaces
    uint32_t free_space_bottom_base = threadid * ((real_cluster_count / MIN_DEFRAG_FREE_FRACTION) / thread_count);

    // start at "unused" (means "no cluster" here)
    entry = FAT_UNUSED;
    // we will loop as long, as farmer offers us some entry to work with
    while ((entry = get_thread_work_entry(entry)) != FAT_UNUSED)
    {
        // retrieve additional info about processed chain
        get_entry_params(entry, file_base, nowcluster);

        // here we secure that file beginning didn't change during function call
        while (true)
        {
            lockindex = _get_mutex_lock_index(nowcluster);

            cluster_mtx_map[lockindex].lock();

            get_entry_params(entry, file_base, tmp);
            // file didn't move since last check - break loop and proceed with defrag
            if (tmp == nowcluster)
                break;

            cluster_mtx_map[lockindex].unlock();

            nowcluster = tmp;
        }
        // when leaving while, we ALREADY HAVE PRIMARY MUTEX LOCKED!

        //if (verbose_output)
            cout << "Thread " << threadid << ": processing file " << entry << " to base " << file_base << ", start cluster " << nowcluster << endl;

        // work until the end of file
        // at this time, cluster at "nowcluster" index should always be secured with mutex
        while (nowcluster != FAT_FILE_END)
        {
            // if the file is fragmented (contains something, that breaks sequence)..
            if (nowcluster != file_base)
            {
                // try to lock destination
                lockindex2 = _get_mutex_lock_index(file_base);
                if (lockindex != lockindex2)
                {
                    // lock lockindex2
                    cluster_mtx_map[lockindex2].lock();
                }

                // clear cluster, where we will put proper data
                if (!_is_cluster_available(file_base))
                {
                    target = _find_free_cluster_end(free_space_bottom_base);

                    while (true)
                    {
                        lockindex3 = _get_mutex_lock_index(target);
                        if (lockindex3 != lockindex && lockindex3 != lockindex2)
                            cluster_mtx_map[lockindex3].lock();

                        tmp = _find_free_cluster_end(free_space_bottom_base);
                        if (tmp == target)
                            break;

                        target = tmp;

                        if (lockindex3 != lockindex && lockindex3 != lockindex2)
                            cluster_mtx_map[lockindex3].unlock();
                    }
                    // lockindex3 will ALWAYS BE LOCKED WHEN LEAVING THIS LOOP

                    // file_base locked with lockindex2, target locked with lockindex3
                    _move_cluster(file_base, target, threadid);

                    if (lockindex3 != lockindex && lockindex3 != lockindex2)
                        cluster_mtx_map[lockindex3].unlock();
                }

                // nowcluster is locked by lockindex, file_base eighter with lockindex or lockindex2
                _move_cluster(nowcluster, file_base, threadid);
                nowcluster = file_base;

                cluster_mtx_map[lockindex2].unlock();
                if (lockindex != lockindex2)
                {
                    // this unlocks original nowcluster
                    cluster_mtx_map[lockindex].unlock();
                }

                // we now don't hold any locks - moved cluster will be there forever
                // just attempt to obtain lock to newly resolved piece of filesystem (next cluster)

                lockindex = _get_mutex_lock_index(fat_tables[0][nowcluster]);
                if (fat_tables[0][nowcluster] != FAT_FILE_END)
                {
                    // lock new mutex
                    cluster_mtx_map[lockindex].lock();
                }

                nowcluster = fat_tables[0][nowcluster];

                if (nowcluster == FAT_FILE_END)
                    suppressUnlock = true;
            }
            // if nowcluster equals file_base, it also means, that it's locked with the same mutex (lockindex)
            else
            {
                // move without locking, no need to defrag this piece of file
                nowcluster = fat_tables[0][nowcluster];
            }

            file_base++;
        }

        if (!suppressUnlock)
            cluster_mtx_map[lockindex].unlock();
        suppressUnlock = false;
    }
}

void defrag_thread_fnc(fat_partition* partition, uint32_t thread_id)
{
    partition->thread_process(thread_id);
}

bool fat_partition::defragment()
{
    uint32_t i, tmp, index;
    free_space_size = real_cluster_count / MIN_DEFRAG_FREE_FRACTION;
    // check free space
    if (free_clusters_count < free_space_size)
    {
        cout << "Not enough free space for defragmentation, please, make sure at least " << round(100.0f/(float)MIN_DEFRAG_FREE_FRACTION) << "% of disk is free" << endl;
        return false;
    }

    // make free space at the end of disk

    for (i = 0; i < free_space_size; i++)
    {
        index = real_cluster_count - i - 1;

        // move used block elsewhere
        if (fat_tables[0][index] != FAT_UNUSED)
        {
            tmp = find_free_cluster_begin();
            _move_cluster(index, tmp);
        }
    }

    // cache file cluster count - this is the resulting size of file block
    uint32_t currBase = 0;
    file_base_offsets = new uint32_t[(uint32_t)bootrec->root_directory_max_entries_count];
    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        file_base_offsets[i] = currBase;
        currBase += (uint32_t)((rootdir[i].file_size / bootrec->cluster_size)+1);
    }

    // 0 will be next assigned index
    process_index = 0;

    uint32_t mtx_count = bootrec->root_directory_max_entries_count + thread_count;

    // subtract "free space size" - it's free space reserved for thread temp storages
    mtx_count += 1 + ((free_clusters_count - free_space_size) / CLUSTERS_PER_SUPERCLUSTER);

    // build supercluster mutex array to lock only several parts of cluster map
    cluster_mtx_map = new std::mutex[mtx_count];

    // cluster mutex map consists of: file clusters lock (one per file), supercluster locks (one per CLUSTERS_PER_SUPERCLUSTER clusters),
    // and thread temp storage locks (one per thread - thread_count)

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
    delete[] cluster_mtx_map;
    delete[] file_base_offsets;

    return true;
}
