#include "global.h"
#include "pseudofat.h"

bool fat_partition::_read_bootrecord(FILE* f)
{
    // read bootrecord
    cout << "Reading bootrecord..." << endl;
    bootrec = new boot_record;
    if (fread(bootrec, sizeof(boot_record), 1, f) != 1)
        return false;

    real_cluster_count = bootrec->cluster_count - bootrec->reserved_cluster_count;

    return true;
}

bool fat_partition::_create_bootrecord(char* volume_desc, int32_t fat_type, int32_t fat_copies, uint32_t cluster_size, uint32_t cluster_count, uint32_t reserver_cluster_count, char* signature)
{
    cout << "Creating bootrecord..." << endl;
    bootrec = new boot_record;

    memset(bootrec, 0, sizeof(boot_record));

    strncpy(bootrec->volume_descriptor, volume_desc, FAT_VOLUME_DESC_SIZE);
    bootrec->fat_type = fat_type;
    bootrec->fat_copies = fat_copies;
    bootrec->cluster_size = cluster_size;
    bootrec->cluster_count = cluster_count;
    bootrec->reserved_cluster_count = reserver_cluster_count;
    bootrec->root_directory_max_entries_count = 0;

    strncpy(bootrec->signature, signature, FAT_SIGNATURE_SIZE);

    real_cluster_count = bootrec->cluster_count - bootrec->reserved_cluster_count;

    return true;
}

bool fat_partition::_read_fat_tables(FILE* f)
{
    int i;

    // read available fat tables
    fat_tables = new uint32_t*[bootrec->fat_copies];
    for (i = 0; i < bootrec->fat_copies; i++)
    {
        cout << "Reading FAT table " << i << "..." << endl;
        fat_tables[i] = new uint32_t[bootrec->cluster_count];

        if (fread(fat_tables[i], sizeof(uint32_t), bootrec->cluster_count, f) != bootrec->cluster_count)
            return false;
    }

    return true;
}

bool fat_partition::_create_fat_tables()
{
    int i;
    uint32_t j;

    fat_tables = new uint32_t*[bootrec->fat_copies];
    for (i = 0; i < bootrec->fat_copies; i++)
    {
        cout << "Creating FAT table " << i << "..." << endl;
        fat_tables[i] = new uint32_t[bootrec->cluster_count];

        // mark contents as unused (free)
        for (j = 0; j < bootrec->cluster_count; j++)
            fat_tables[i][j] = FAT_UNUSED;
    }

    return true;
}

bool fat_partition::_read_root_directory(FILE* f)
{
    int i;

    // read all root directory entries
    cout << "Reading root directory entries..." << endl;
    rootdir = new root_directory[(uint32_t)bootrec->root_directory_max_entries_count];
    for (i = 0; i < bootrec->root_directory_max_entries_count; i++)
    {
        if (fread(&rootdir[i], sizeof(root_directory), 1, f) != 1)
            return false;

        if (verbose_output)
        {
            cout << "File: " << rootdir[i].file_name << endl;
            cout << "File mod: " << rootdir[i].file_mod << endl;
            cout << "Size: " << rootdir[i].file_size << endl;
            cout << "Type: " << rootdir[i].file_type << endl;
            cout << "First cluster: " << rootdir[i].first_cluster << endl;
            cout << endl;
        }
    }

    return true;
}

bool fat_partition::_read_clusters(FILE* f)
{
    uint32_t i;

    // read all data clusters
    cout << "Reading cluster info..." << endl;
    clusters = new uint8_t*[real_cluster_count];
    for (i = 0; i < real_cluster_count; i++)
    {
        clusters[i] = new uint8_t[bootrec->cluster_size];
        if (fread(clusters[i], sizeof(uint8_t), bootrec->cluster_size, f) != bootrec->cluster_size)
            return false;

        if (verbose_output == 2)
        {
            if (clusters[i][0] != '\0')
            {
                cout << "Cluster " << i << " contents: " << endl;
                cout << clusters[i] << endl;
                cout << endl;
            }
        }
    }

    return true;
}

bool fat_partition::_create_clusters()
{
    uint32_t i = 0;

    cout << "Creating cluster array..." << endl;
    clusters = new uint8_t*[real_cluster_count];
    for (i = 0; i < real_cluster_count; i++)
    {
        clusters[i] = new uint8_t[bootrec->cluster_size];
        memset(clusters[i], 0, sizeof(uint8_t) * bootrec->cluster_size);
    }

    return true;
}

fat_partition* fat_partition::load_from_file(char* filename)
{
    fat_partition* partition = new fat_partition;

    FILE* f = fopen(filename, "rb");

    if (!f)
    {
        cerr << "Input file " << filename << " does not exist" << endl;
        return nullptr;
    }

    cout << "Reading filesystem..." << endl;

    if (!partition->_read_bootrecord(f))
    {
        cerr << "Invalid file supplied - file does not contain valid bootrecord format" << endl;
        return nullptr;
    }

    if (!partition->_read_fat_tables(f))
    {
        cerr << "Invalid file supplied - file does not contain FAT tables specified by count in bootrecord" << endl;
        return nullptr;
    }

    if (!partition->_read_root_directory(f))
    {
        cerr << "Invalid file supplied - file does not contain specified root directory entries" << endl;
        return nullptr;
    }

    if (!partition->_read_clusters(f))
    {
        cerr << "Invalid file supplied - file does not contain specified count of clusters" << endl;
        return nullptr;
    }

    fclose(f);

    return partition;
}

fat_partition* fat_partition::create(char* volume_desc, int32_t fat_type, int32_t fat_copies, uint32_t cluster_size, uint32_t cluster_count, uint32_t reserver_cluster_count, char* signature)
{
    fat_partition* partition = new fat_partition;

    cout << "Creating filesystem..." << endl;

    if (!partition->_create_bootrecord(volume_desc, fat_type, fat_copies, cluster_size, cluster_count, reserver_cluster_count, signature))
    {
        cerr << "Unable to create bootrecord" << endl;
        return nullptr;
    }

    if (!partition->_create_fat_tables())
    {
        cerr << "Unable to create requested FAT tables" << endl;
        return nullptr;
    }

    if (!partition->_create_clusters())
    {
        cerr << "Unable to create cluster array" << endl;
        return nullptr;
    }

    return partition;
}
