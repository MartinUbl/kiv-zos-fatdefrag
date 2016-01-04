#ifndef ZOS_PSEUDOFAT_H
#define ZOS_PSEUDOFAT_H

#include <set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <list>
#include <vector>
#include <queue>
#include <deque>

#define FAT_UNUSED      65535
#define FAT_FILE_END    65534
#define FAT_BAD_CLUSTER 65533

#define FAT_UNUSED_NOT_FOUND FAT_UNUSED

#define FAT_VOLUME_DESC_SIZE    251
#define FAT_SIGNATURE_SIZE      4
#define FAT_FILENAME_SIZE       13
#define FAT_FILEMOD_SIZE        10

/* maximum number of recoverable errors for partition */
#define MAX_RECOVERABLE_ERRORS  20

/* count / MIN_DEFRAG_FREE_FRACTION clusters has to be free */
#define MIN_DEFRAG_FREE_FRACTION 10

#define CLUSTERS_PER_SUPERCLUSTER 20

struct boot_record
{
    char        volume_descriptor[FAT_VOLUME_DESC_SIZE];
    int32_t     fat_type;
    int32_t     fat_copies;
    uint32_t    cluster_size;
    int64_t     root_directory_max_entries_count;
    uint32_t    cluster_count;
    uint32_t    reserved_cluster_count;
    char        signature[FAT_SIGNATURE_SIZE];
};

struct root_directory
{
    char        file_name[FAT_FILENAME_SIZE];
    char        file_mod[FAT_FILEMOD_SIZE];
    int16_t     file_type;
    int64_t     file_size;
    uint32_t    first_cluster;
};

class fat_partition
{
    private:
        bool _check_cluster_chain_everywhere(uint32_t start_cluster);

        uint32_t _find_free_cluster_end(int32_t end_offset = 0);

        bool _move_cluster(uint32_t source, uint32_t dest, int32_t thread_id = -1);
        bool _is_cluster_available(uint32_t id);
        bool _is_cluster_bad(uint32_t id);

        bool _read_bootrecord(FILE* f);
        bool _read_fat_tables(FILE* f);
        bool _read_root_directory(FILE* f);
        bool _read_clusters(FILE* f);

        bool _create_bootrecord(const char* volume_desc, int32_t fat_type, int32_t fat_copies, uint32_t cluster_size, uint32_t cluster_count, uint32_t reserver_cluster_count, const char* signature);
        bool _create_fat_tables();
        bool _create_clusters();

        bool _write_bootrecord(FILE* f);
        bool _write_fat_tables(FILE* f);
        bool _write_root_directory(FILE* f);
        bool _write_clusters(FILE* f);

        bool _check_fattables_files();
        bool _check_fattables_equal();

        uint32_t _find_nth_free_cluster(uint32_t n);
        void _set_fat_entry(uint32_t index, uint32_t value);
        void _set_cluster_content(uint32_t index, const char* content);

        uint32_t* file_base_offsets;
        std::mutex assign_mtx;
        std::mutex move_mtx;

        std::deque<uint32_t> occupied_clusters_to_work;
        std::vector<uint32_t>* rootdir_cluster_chains;

        uint32_t free_space_size;

    public:

        bool defragment();

        void dump_contents();
        void cache_counts();

        bool check_fattables();

        uint32_t find_free_cluster_begin();

        static fat_partition* load_from_file(const char* filename);
        static fat_partition* create(const char* volume_desc, int32_t fat_type, int32_t fat_copies, uint32_t cluster_size, uint32_t cluster_count, uint32_t reserver_cluster_count = 10, const char* signature = "OK");
        bool save_to_file(const char* filename);

        void write_randomized_entry(int32_t break_length_by = 0);
        int write_source_file(FILE* source, const char* filename, uint32_t dest, bool randomize = false, int32_t break_length_by = 0, uint32_t endfile_rec = FAT_FILE_END);

        boot_record*    bootrec;
        root_directory* rootdir;
        uint32_t**      fat_tables;

        uint32_t        real_cluster_count;
        uint32_t        free_clusters_count;

        uint8_t**       clusters;

        // thread-related stuff
        uint32_t get_thread_work_cluster(int32_t retback = -1);
        void put_thread_work_cluster(int32_t retback);
        bool get_thread_work_cluster_reserve(uint32_t entry);

        uint32_t get_aligned_position(uint32_t current);

        void thread_process(uint32_t threadid);
};

#endif
