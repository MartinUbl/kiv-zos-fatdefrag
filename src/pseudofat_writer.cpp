#include "global.h"
#include "pseudofat.h"

#include <string>

bool fat_partition::_write_bootrecord(FILE* f)
{
    cout << "Writing bootrecord..." << endl;
    if (fwrite(bootrec, sizeof(boot_record), 1, f) != 1)
        return false;

    return true;
}

bool fat_partition::_write_fat_tables(FILE* f)
{
    int32_t i;

    for (i = 0; i < bootrec->fat_copies; i++)
    {
        cout << "Writing FAT table " << i << "..." << endl;

        if (fwrite(fat_tables[i], sizeof(uint32_t), bootrec->cluster_count, f) != bootrec->cluster_count)
            return false;
    }

    return true;
}

bool fat_partition::_write_root_directory(FILE* f)
{
    int64_t i;

    cout << "Writing root directory entries" << endl;
    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        if (fwrite(&rootdir[i], sizeof(root_directory), 1, f) != 1)
            return false;
    }

    return true;
}

bool fat_partition::_write_clusters(FILE* f)
{
    uint32_t i;

    cout << "Writing cluster info..." << endl;
    for (i = 0; i < real_cluster_count; i++)
    {
        if (fwrite(clusters[i], sizeof(uint8_t), bootrec->cluster_size, f) != bootrec->cluster_size)
            return false;
    }

    return true;
}

bool fat_partition::save_to_file(char* filename)
{
    FILE* f = fopen(filename, "wb");

    if (!f)
    {
        cerr << "Failed to open file " << filename << " for writing. Please, check permissions." << endl;
        return false;
    }

    if (!_write_bootrecord(f))
    {
        cerr << "Failed to write bootrecord to file" << endl;
        return false;
    }

    if (!_write_fat_tables(f))
    {
        cerr << "Failed to write FAT tables to file" << endl;
        return false;
    }

    if (!_write_root_directory(f))
    {
        cerr << "Failed to write root directory entries to file" << endl;
        return false;
    }

    if (!_write_clusters(f))
    {
        cerr << "Failed to write cluster info to file" << endl;
        return false;
    }

    fclose(f);

    return true;
}

void fat_partition::_set_fat_entry(uint32_t index, uint32_t value)
{
    int32_t i;

    for (i = 0; i < bootrec->fat_copies; i++)
        fat_tables[i][index] = (value == FAT_FILE_END && i == 1) ? 65535 : value;
}

void fat_partition::_set_cluster_content(uint32_t index, char* content)
{
    strncpy((char*)clusters[index], content, bootrec->cluster_size);

    clusters[index][bootrec->cluster_size - 1] = '\0';
}

uint32_t fat_partition::_find_nth_free_cluster(uint32_t n)
{
    uint32_t i;

    for (i = 0; i < real_cluster_count; i++)
    {
        if (fat_tables[0][i] == FAT_UNUSED)
        {
            n--;
            if (n == 1)
                return i;
        }
    }

    return FAT_UNUSED_NOT_FOUND;
}

void fat_partition::write_randomized_entry(int32_t break_length_by)
{
    uint32_t i, prev, tmp;
    uint32_t nind = bootrec->root_directory_max_entries_count;

    root_directory* nrdir = new root_directory[nind + 1];
    memcpy(nrdir, rootdir, sizeof(root_directory)*bootrec->root_directory_max_entries_count);

    delete[] rootdir;
    rootdir = nrdir;

    bootrec->root_directory_max_entries_count++;

    for (i = 0; i < 8; i++)
        nrdir[nind].file_name[i] = 'A' + rand() % 27;
    nrdir[nind].file_name[8] = '.';
    nrdir[nind].file_name[9] = 't';
    nrdir[nind].file_name[10] = 'x';
    nrdir[nind].file_name[11] = 't';
    nrdir[nind].file_name[12] = '\0';

    strcpy(nrdir[nind].file_mod, "rwxrwxrwx");

    nrdir[nind].file_size = rand() % 2000;

    nrdir[nind].file_type = 1;

    uint32_t clcnt = rand() % (free_clusters_count - 5);
    nrdir[nind].first_cluster = _find_nth_free_cluster(clcnt);
    _set_fat_entry(nrdir[nind].first_cluster, FAT_FILE_END);
    free_clusters_count--;

    _set_cluster_content(nrdir[nind].first_cluster, "obsah zacatku souboru");

    uint32_t cluster_count = (nrdir[nind].file_size / bootrec->cluster_size) + break_length_by; // +1-1 (starting cluster is already written)

    prev = nrdir[nind].first_cluster;

    for (i = 0; i < cluster_count; i++)
    {
        clcnt = rand() % (free_clusters_count - 1);
        tmp = _find_nth_free_cluster(clcnt);
        free_clusters_count--;

        _set_fat_entry(prev, tmp);
        _set_fat_entry(tmp, FAT_FILE_END);

        _set_cluster_content(tmp, (char*)(std::string("nejaky obsah") + std::to_string(i)).c_str());

        prev = tmp;
    }

    //
}

int fat_partition::write_source_file(FILE* source, const char* filename, uint32_t dest, bool randomize, int32_t break_length_by, uint32_t endfile_rec)
{
    uint32_t i, prev, tmp;
    uint32_t nind = bootrec->root_directory_max_entries_count;

    if (free_clusters_count == 0)
        return 3;

    // seek to file start
    fseek(source, 0, SEEK_SET);

    root_directory* nrdir = new root_directory[nind + 1];
    memcpy(nrdir, rootdir, sizeof(root_directory)*bootrec->root_directory_max_entries_count);

    delete[] rootdir;
    rootdir = nrdir;

    bootrec->root_directory_max_entries_count++;

    memset(nrdir[nind].file_name, 0, FAT_FILENAME_SIZE);
    strncpy(nrdir[nind].file_name, filename, FAT_FILENAME_SIZE - 1);

    strcpy(nrdir[nind].file_mod, "rwxrwxrwx");

    nrdir[nind].file_size = 0; // will be increased during writeout

    nrdir[nind].file_type = 1;

    nrdir[nind].first_cluster = randomize ? _find_nth_free_cluster(rand() % (free_clusters_count - 5)) : dest;

    _set_fat_entry(nrdir[nind].first_cluster, endfile_rec);
    free_clusters_count--;

    uint8_t* write_buffer = new uint8_t[bootrec->cluster_size];
    uint32_t bytes_read = 0;

    bytes_read = fread(write_buffer, 1, bootrec->cluster_size, source);
    if (bytes_read == 0)
    {
        // TODO: what if the file is empty? now should end up with valid file record, but it still would have cluster allocated
        return 0;
    }

    _set_cluster_content(nrdir[nind].first_cluster, (char*)write_buffer);
    nrdir[nind].file_size += bytes_read;

    prev = nrdir[nind].first_cluster;
    tmp = nrdir[nind].first_cluster;

    while ((bytes_read = fread(write_buffer, 1, bootrec->cluster_size, source)) > 0)
    {
        do
        {
            tmp = randomize ? _find_nth_free_cluster(rand() % (free_clusters_count - 1)) : tmp + 1;
        }
        while (fat_tables[0][tmp] != FAT_UNUSED);

        free_clusters_count--;

        _set_fat_entry(prev, tmp);
        _set_fat_entry(tmp, endfile_rec);

        _set_cluster_content(tmp, (char*)write_buffer);
        nrdir[nind].file_size += bytes_read;

        // clear previous contents
        memset(write_buffer, 0, sizeof(uint8_t)*bootrec->cluster_size);

        prev = tmp;

        if (free_clusters_count == 0)
            return 3;
    }

    // break length if requested
    if (break_length_by)
    {
        if (break_length_by > 0 || -break_length_by > nrdir[nind].file_size)
            nrdir[nind].file_size += break_length_by;
    }

    return 0;
}
