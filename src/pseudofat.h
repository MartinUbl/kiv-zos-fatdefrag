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

// unused cluster
#define FAT_UNUSED      65535
// file ending - do not continue in chain
#define FAT_FILE_END    65534
// bad cluster mark
#define FAT_BAD_CLUSTER 65533

// just alias for several return values
#define FAT_UNUSED_NOT_FOUND FAT_UNUSED

// volume descriptor string length
#define FAT_VOLUME_DESC_SIZE    251
// signature string length
#define FAT_SIGNATURE_SIZE      4
// filename string length
#define FAT_FILENAME_SIZE       13
// filemod length
#define FAT_FILEMOD_SIZE        10

// maximum number of recoverable errors for partition
#define MAX_RECOVERABLE_ERRORS  20

// count / MIN_DEFRAG_FREE_FRACTION clusters has to be free 
#define MIN_DEFRAG_FREE_FRACTION 1

// bootrecord structure
struct boot_record
{
    char        volume_descriptor[FAT_VOLUME_DESC_SIZE];    // volume descriptor
    int32_t     fat_type;                                   // FAT type (12, 16, 32)
    int32_t     fat_copies;                                 // number of FAT tables
    uint32_t    cluster_size;                               // number of bytes per cluster
    int64_t     root_directory_max_entries_count;           // root directory entries count
    uint32_t    cluster_count;                              // count of clusters
    uint32_t    reserved_cluster_count;                     // reserved cluster count (not written in image)
    char        signature[FAT_SIGNATURE_SIZE];              // filesystem signature
};

// root directory entry structure
struct root_directory
{
    char        file_name[FAT_FILENAME_SIZE];               // filename
    char        file_mod[FAT_FILEMOD_SIZE];                 // file mod (rwx rights for user, group, others)
    int16_t     file_type;                                  // type of file
    int64_t     file_size;                                  // number of bytes in this file
    uint32_t    first_cluster;                              // first cluster to begin chain with
};

// fat partition class
class fat_partition
{
    private:
        // checks cluster chain in all FAT tables and reports errors; returns true if only recoverable errors found
        bool _check_cluster_chain_everywhere(uint32_t start_cluster);

        // finds free cluster from partition end
        uint32_t _find_free_cluster_end(int32_t end_offset = 0);

        // moves cluster contents from source to destination cluster, and performs changes in FAT tables chains
        bool _move_cluster(uint32_t source, uint32_t dest, int32_t thread_id = -1);
        // is cluster available for use?
        bool _is_cluster_available(uint32_t id);
        // is cluster marked as BAD?
        bool _is_cluster_bad(uint32_t id);

        // reads bootrecord from file
        bool _read_bootrecord(FILE* f);
        // reads FAT tables from file (needs to have bootrecord loaded) from file
        bool _read_fat_tables(FILE* f);
        // reads all root directory entries from file
        bool _read_root_directory(FILE* f);
        // reads cluster contents from file
        bool _read_clusters(FILE* f);

        // creates bootrecord from supplied parameters
        bool _create_bootrecord(const char* volume_desc, int32_t fat_type, int32_t fat_copies, uint32_t cluster_size, uint32_t cluster_count, uint32_t reserver_cluster_count, const char* signature);
        // creates space for FAT tables
        bool _create_fat_tables();
        // creates space for cluster contents
        bool _create_clusters();

        // writes bootrecord to file
        bool _write_bootrecord(FILE* f);
        // writes FAT tables to file
        bool _write_fat_tables(FILE* f);
        // writes root directory entries to file
        bool _write_root_directory(FILE* f);
        // writes cluster contents to file
        bool _write_clusters(FILE* f);

        // checks FAT tables for file consistency
        bool _check_fattables_files();
        // checks FAT tables for equal values
        bool _check_fattables_equal();

        // looks for n-th free cluster from partition beginning
        uint32_t _find_nth_free_cluster(uint32_t n);
        // sets entry in FAT table
        void _set_fat_entry(uint32_t index, uint32_t value);
        // sets contents of cluster
        void _set_cluster_content(uint32_t index, const char* content);

        // array of file base offsets
        uint32_t* file_base_offsets;
        // farmer-worker assigning mutex
        std::mutex assign_mtx;
        // cluster contents move mutex
        std::mutex move_mtx;

        // queue of all clusters to be processed
        std::deque<uint32_t> occupied_clusters_to_work;
        // vector of cluster chains
        std::vector<uint32_t>* rootdir_cluster_chains;

        // number of free clusters needed for defragmentation
        uint32_t free_space_size;

    public:

        // defragments loaded FAT partition
        bool defragment();

        // dumps FAT partition contents
        void dump_contents();
        // proceeds caching
        void cache_counts();

        // checks FAT tables for errors
        bool check_fattables();

        // finds free cluster from beginning
        uint32_t find_free_cluster_begin();

        // constructs fat_partition instance from file (filename supplied)
        static fat_partition* load_from_file(const char* filename);
        // constructs fat_partition based on supplied arguments
        static fat_partition* create(const char* volume_desc, int32_t fat_type, int32_t fat_copies, uint32_t cluster_size, uint32_t cluster_count, uint32_t reserver_cluster_count = 10, const char* signature = "OK");
        // saves fat_partition to image file
        bool save_to_file(const char* filename);

        // writes random file entry to random position (no overwriting)
        void write_randomized_entry(int32_t break_length_by = 0);
        // writes source file into image at specified position, or randomized, eventually with broken file ending signature (for testing purposes)
        int write_source_file(FILE* source, const char* filename, uint32_t dest, bool randomize = false, int32_t break_length_by = 0, uint32_t endfile_rec = FAT_FILE_END);

        // bootrecord structure
        boot_record*    bootrec;
        // root directory entries
        root_directory* rootdir;
        // FAT tables
        uint32_t**      fat_tables;

        // count of clusters (physical minus reserved)
        uint32_t        real_cluster_count;
        // count of free clusters
        uint32_t        free_clusters_count;

        // cluster contents
        uint8_t**       clusters;

        // thread-related stuff

        // retrieves cluster to be defragmented
        uint32_t get_thread_work_cluster(int32_t retback = -1);
        // puts back cluster to queue for processing
        void put_thread_work_cluster(int32_t retback);
        // reserves custom found cluster for moving (returns true on success, or false when another thread got it)
        bool get_thread_work_cluster_reserve(uint32_t entry);

        // retrieves cluster aligned position
        uint32_t get_aligned_position(uint32_t current);

        // thread worker
        void thread_process(uint32_t threadid);
};

#endif
