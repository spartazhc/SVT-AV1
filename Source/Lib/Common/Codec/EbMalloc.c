// clang-format off
/*
* Copyright(c) 2019 Intel Corporation
* SPDX - License - Identifier: BSD - 2 - Clause - Patent
*/
#include <stdio.h>
#include <inttypes.h>
#include <pthread.h>


#include "EbMalloc.h"
#include "EbThreads.h"
#include "EbTime.h"

static EbHandle g_time_mutex;

static void create_time_mutex()
{
    g_time_mutex = eb_create_mutex();
}

static pthread_once_t g_time_once = PTHREAD_ONCE_INIT;

static EbHandle get_time_mutex()
{
    pthread_once(&g_time_once, create_time_mutex);
    return g_time_mutex;
}

uint32_t hash_ti(uint64_t p)
{
#define MASK32 ((((uint64_t)1)<<32)-1)

    uint64_t low32 = p & MASK32;
    return (uint32_t)((p >> 32) + low32);
}

typedef struct TimeEntry{
    uint64_t pic_num;
    uint32_t seg_idx;
    EbTimeType time_type;
    EbTaskType task_type;
    EbProcessType Ptype;
    uint64_t sTime;
    uint64_t uTime;
} TimeEntry;

//+1 to get a better hash result
#define TIME_ENTRY_SIZE (4 * 1024 * 1024 + 1)

TimeEntry g_time_entry[TIME_ENTRY_SIZE];

#define TO_INDEX_TI(v) ((v) % TIME_ENTRY_SIZE)
static EbBool g_add_time_entry_warning = EB_TRUE;

typedef EbBool (*Predicate2)(TimeEntry* e, void* param);

static EbBool for_each_hash_entry_ti(TimeEntry* bucket, uint32_t start, Predicate2 pred, void* param)
{

    uint32_t s = TO_INDEX_TI(start);
    uint32_t i = s;

    do {
        TimeEntry* e = bucket + i;
        if (pred(e, param)) {
            return EB_TRUE;
        }
        i++;
        i = TO_INDEX_TI(i);
    } while (i != s);
     return EB_FALSE;
}

static EbBool for_each_time_entry(uint32_t start, Predicate2 pred, void* param)
{
    EbBool ret;
    EbHandle m = get_time_mutex();
    eb_block_on_mutex(m);
    ret = for_each_hash_entry_ti(g_time_entry, start, pred, param);
    eb_release_mutex(m);
    return ret;
}

static EbBool add_time_entry(TimeEntry* e, void* param)
{
    TimeEntry* new_item = (TimeEntry*)param;
    if (!e->uTime) {
        EB_MEMCPY(e, new_item, sizeof(TimeEntry));
        return EB_TRUE;
    }
    return EB_FALSE;
}


void eb_add_time_entry(EbProcessType Ptype, EbTimeType TimeType, EbTaskType TaskType, uint64_t pic_num, int32_t seg_idx)
{
    TimeEntry item;
    item.pic_num = pic_num;
    item.seg_idx = seg_idx;
    item.time_type = TimeType;
    item.task_type = TaskType;
    item.Ptype = Ptype;
    EbStartTime(&item.sTime, &item.uTime);
    if (for_each_time_entry(hash_ti(item.uTime), add_time_entry, &item))
        return;
    if (g_add_time_entry_warning) {
        fprintf(stderr, "SVT: can't add time entry.\r\n");
        fprintf(stderr, "SVT: You need to increase TIME_ENTRY_SIZE\r\n");
        g_add_time_entry_warning = EB_FALSE;
    }
}

static int compare_time(const void* a,const void* b)
{
    const TimeEntry* pa = (const TimeEntry*)a;
    const TimeEntry* pb = (const TimeEntry*)b;
    if (pa->sTime == 0 && pb->sTime != 0) return 1;
    if (pa->sTime != 0 && pb->sTime == 0) return -1;
    if (pa->sTime < pb->sTime) return -1;
    else if (pa->sTime == pb->sTime) {
        if (pa->uTime < pb->uTime) return -1;
        else if (pa->uTime > pb->uTime) return 1;
        else return 0;
    }
    return 1;
}

static const char *process_namelist[EB_PROCESS_TYPE_TOTAL] = {
    "resource_coord", "pic_analysis", "pic_decision",
    "motion_estimation", "initial_rc", "source_based_op",
    "pic_manager", "rate_control", "mode_decision",
    "enc_dec", "dlf", "cdef", "rest", "entropy_coding", "packetization"
    };
static const char* process_name(EbProcessType type)
{
    return process_namelist[type];
}

void eb_print_time_usage() {
    EbHandle m = get_time_mutex();
    eb_block_on_mutex(m);
    FILE *fp = NULL, *fp_raw = NULL;
    fp = fopen("/tmp/profile.csv", "w+");
    fp_raw = fopen("/tmp/profile_raw.csv", "w+");
    qsort(g_time_entry, TIME_ENTRY_SIZE, sizeof(TimeEntry), compare_time);
    int i = 0;
    double mtime;
    while (g_time_entry[i].uTime) {
        EbComputeOverallElapsedTimeRealMs(
            g_time_entry[0].sTime,
            g_time_entry[0].uTime,
            g_time_entry[i].sTime,
            g_time_entry[i].uTime,
            &mtime);
        fprintf(fp, "%s, Timetype=%d, TaskType=%d, pic_num=%zu, seg_idx=%d, TimeUseinMS=%.4f\n",
            process_name(g_time_entry[i].Ptype), (int)g_time_entry[i].time_type, (int)g_time_entry[i].task_type,
             g_time_entry[i].pic_num, g_time_entry[i].seg_idx, mtime);
        fprintf(fp_raw, "%d, %d, %d, %zu, %d, %.4f\n",
            (int)g_time_entry[i].Ptype, (int)g_time_entry[i].time_type, (int)g_time_entry[i].task_type,
             g_time_entry[i].pic_num, g_time_entry[i].seg_idx, mtime);
        ++i;
    }
    fclose(fp);
    fclose(fp_raw);
    eb_release_mutex(m);
}

#ifdef DEBUG_MEMORY_USAGE

static EbHandle g_malloc_mutex;

#ifdef _WIN32

#include <windows.h>

static INIT_ONCE g_malloc_once = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK create_malloc_mutex (
    PINIT_ONCE InitOnce,
    PVOID Parameter,
    PVOID *lpContext)
{
    (void)InitOnce;
    (void)Parameter;
    (void)lpContext;
    g_malloc_mutex = eb_create_mutex();
    return TRUE;
}

static EbHandle get_malloc_mutex()
{
    InitOnceExecuteOnce(&g_malloc_once, create_malloc_mutex, NULL, NULL);
    return g_malloc_mutex;
}
#else
#include <pthread.h>
static void create_malloc_mutex()
{
    g_malloc_mutex = eb_create_mutex();
}

static pthread_once_t g_malloc_once = PTHREAD_ONCE_INIT;

static EbHandle get_malloc_mutex()
{
    pthread_once(&g_malloc_once, create_malloc_mutex);
    return g_malloc_mutex;
}
#endif // _WIN32

//hash function to speedup etnry search
uint32_t hash(void* p)
{
#define MASK32 ((((uint64_t)1)<<32)-1)

    uint64_t v = (uint64_t)p;
    uint64_t low32 = v & MASK32;
    return (uint32_t)((v >> 32) + low32);
}

typedef struct MemoryEntry{
    void* ptr;
    EbPtrType type;
    size_t count;
    const char* file;
    uint32_t line;
} MemoryEntry;

//+1 to get a better hash result
#define MEM_ENTRY_SIZE (4 * 1024 * 1024 + 1)

MemoryEntry g_mem_entry[MEM_ENTRY_SIZE];

#define TO_INDEX(v) ((v) % MEM_ENTRY_SIZE)
static EbBool g_add_mem_entry_warning = EB_TRUE;
static EbBool g_remove_mem_entry_warning = EB_TRUE;


/*********************************************************************************
*
* @brief
*  compare and update current memory entry.
*
* @param[in] e
*  current memory entry.
*
* @param[in] param
*  param you set to for_each_mem_entry
*
*
* @returns  return EB_TRUE if you want get early exit in for_each_mem_entry
*
s*
********************************************************************************/

typedef EbBool (*Predicate)(MemoryEntry* e, void* param);

/*********************************************************************************
*
* @brief
*  Loop through mem entries.
*
* @param[in] bucket
*  the hash bucket
*
* @param[in] start
*  loop start position
*
* @param[in] pred
*  return EB_TRUE if you want early exit
*
* @param[out] param
*  param send to pred.
*
* @returns  return EB_TRUE if we got early exit.
*
*
********************************************************************************/
static EbBool for_each_hash_entry(MemoryEntry* bucket, uint32_t start, Predicate pred, void* param)
{

    uint32_t s = TO_INDEX(start);
    uint32_t i = s;

    do {
        MemoryEntry* e = bucket + i;
        if (pred(e, param)) {
            return EB_TRUE;
        }
        i++;
        i = TO_INDEX(i);
    } while (i != s);
     return EB_FALSE;
}

static EbBool for_each_mem_entry(uint32_t start, Predicate pred, void* param)
{
    EbBool ret;
    EbHandle m = get_malloc_mutex();
    eb_block_on_mutex(m);
    ret = for_each_hash_entry(g_mem_entry, start, pred, param);
    eb_release_mutex(m);
    return ret;
}

static const char* mem_type_name(EbPtrType type)
{
    static const char *name[EB_PTR_TYPE_TOTAL] = {"malloced memory", "calloced memory", "aligned memory", "mutex", "semaphore", "thread"};
    return name[type];
}

static EbBool add_mem_entry(MemoryEntry* e, void* param)
{
    MemoryEntry* new_item = (MemoryEntry*)param;
    if (!e->ptr) {
        EB_MEMCPY(e, new_item, sizeof(MemoryEntry));
        return EB_TRUE;
    }
    return EB_FALSE;
}


void eb_add_mem_entry(void* ptr,  EbPtrType type, size_t count, const char* file, uint32_t line)
{
    MemoryEntry item;
    item.ptr = ptr;
    item.type = type;
    item.count = count;
    item.file = file;
    item.line = line;
    if (for_each_mem_entry(hash(ptr), add_mem_entry, &item))
        return;
    if (g_add_mem_entry_warning) {
        fprintf(stderr, "SVT: can't add memory entry.\r\n");
        fprintf(stderr, "SVT: You have memory leak or you need increase MEM_ENTRY_SIZE\r\n");
        g_add_mem_entry_warning = EB_FALSE;
    }
}

static EbBool remove_mem_entry(MemoryEntry* e, void* param)
{
    MemoryEntry* item = (MemoryEntry*)param;
    if (e->ptr == item->ptr) {
        if (e->type == item->type) {
            e->ptr = NULL;
            return EB_TRUE;
        } else if (e->type == EB_C_PTR && item->type == EB_N_PTR) {
            //speical case, we use EB_FREE to free calloced memory
            e->ptr = NULL;
            return EB_TRUE;
        }
    }
    return EB_FALSE;
}

void eb_remove_mem_entry(void* ptr, EbPtrType type)
{
    if (!ptr)
        return;
    MemoryEntry item;
    item.ptr = ptr;
    item.type = type;
    if (for_each_mem_entry(hash(ptr), remove_mem_entry, &item))
        return;
    if (g_remove_mem_entry_warning) {
        fprintf(stderr, "SVT: something wrong. you freed a unallocated memory %p, type = %s\r\n", ptr, mem_type_name(type));
        g_remove_mem_entry_warning = EB_FALSE;
    }
}

typedef struct MemSummary {
    uint64_t amount[EB_PTR_TYPE_TOTAL];
    uint32_t occupied;
} MemSummary;

static EbBool count_mem_entry(MemoryEntry* e, void* param)
{
    MemSummary* sum = (MemSummary*)param;
    if (e->ptr) {
        sum->amount[e->type] += e->count;
        sum->occupied++;
    }
    return EB_FALSE;
}

static void get_memory_usage_and_scale(uint64_t amount, double* usage, char* scale)
{
    char scales[] = { ' ', 'K', 'M', 'G' };
    size_t i;
    uint64_t v;
    for (i = 1; i < sizeof(scales); i++) {
        v = (uint64_t)1 << (i * 10);
        if (amount < v)
            break;
    }
    i--;
    v = (uint64_t)1 << (i * 10);
    *usage = (double)amount / v;
    *scale = scales[i];
}

//this need more memory and cpu
#define PROFILE_MEMORY_USAGE
#ifdef PROFILE_MEMORY_USAGE

//if we use a static array here, this size + sizeof(g_mem_entry) will exceed max size allowed on windows.
static MemoryEntry* g_profile_entry;

uint32_t hash_location(FILE* f, int line) {
#define MASK32 ((((uint64_t)1)<<32)-1)

    uint64_t v = (uint64_t)f;
    uint64_t low32 = v & MASK32;
    return (uint32_t)((v >> 32) + low32 + line);
}

static EbBool add_location(MemoryEntry* e, void* param) {
    MemoryEntry* new_item = (MemoryEntry*)param;
    if (!e->ptr) {
        *e = *new_item;
        return EB_TRUE;
    } else if (e->file == new_item->file && e->line == new_item->line) {
        e->count += new_item->count;
        return EB_TRUE;
    }
    //to next position.
    return EB_FALSE;
}

static EbBool collect_mem(MemoryEntry* e, void* param) {
    EbPtrType type = *(EbPtrType*)param;
    if (e->ptr && e->type == type) {
        for_each_hash_entry(g_profile_entry, 0, add_location, e);
    }
    //Loop entire bucket.
    return EB_FALSE;
}

static int compare_count(const void* a,const void* b)
{
    const MemoryEntry* pa = (const MemoryEntry*)a;
    const MemoryEntry* pb = (const MemoryEntry*)b;
    if (pb->count < pa->count) return -1;
    if (pb->count == pa->count) return 0;
    return 1;
}

static void print_top_10_locations() {
    EbHandle m = get_malloc_mutex();
    EbPtrType type = EB_N_PTR;
    eb_block_on_mutex(m);
    g_profile_entry = (MemoryEntry*)calloc(MEM_ENTRY_SIZE, sizeof(MemoryEntry));
    if (!g_profile_entry) {
        fprintf(stderr, "not enough memory for memory profile");
        eb_release_mutex(m);
        return;
    }

    for_each_hash_entry(g_mem_entry, 0, collect_mem, &type);
    qsort(g_profile_entry, MEM_ENTRY_SIZE, sizeof(MemoryEntry), compare_count);

    printf("top 10 %s locations:\r\n", mem_type_name(type));
    for (int i = 0; i < 10; i++) {
        double usage;
        char scale;
        MemoryEntry* e = g_profile_entry + i;
        get_memory_usage_and_scale(e->count, &usage, &scale);
        printf("(%.2lf %cB): %s:%d\r\n", usage, scale, e->file, e->line);
    }
    free(g_profile_entry);
    eb_release_mutex(m);
}
#endif //PROFILE_MEMORY_USAGE

static int g_component_count;

#endif //DEBUG_MEMORY_USAGE

void eb_print_memory_usage()
{
#ifdef DEBUG_MEMORY_USAGE
    MemSummary sum;
    double fulless;
    double usage;
    char scale;
    memset(&sum, 0, sizeof(MemSummary));

    for_each_mem_entry(0, count_mem_entry, &sum);
    printf("SVT Memory Usage:\r\n");
    get_memory_usage_and_scale(sum.amount[EB_N_PTR] + sum.amount[EB_C_PTR] + sum.amount[EB_A_PTR], &usage, &scale);
    printf("    total allocated memory:       %.2lf %cB\r\n", usage, scale);
    get_memory_usage_and_scale(sum.amount[EB_N_PTR], &usage, &scale);
    printf("        malloced memory:          %.2lf %cB\r\n", usage, scale);
    get_memory_usage_and_scale(sum.amount[EB_C_PTR], &usage, &scale);
    printf("        callocated memory:        %.2lf %cB\r\n", usage, scale);
    get_memory_usage_and_scale(sum.amount[EB_A_PTR], &usage, &scale);
    printf("        allocated aligned memory: %.2lf %cB\r\n", usage, scale);

    printf("    mutex count: %d\r\n", (int)sum.amount[EB_MUTEX]);
    printf("    semaphore count: %d\r\n", (int)sum.amount[EB_SEMAPHORE]);
    printf("    thread count: %d\r\n", (int)sum.amount[EB_THREAD]);
    fulless = (double)sum.occupied / MEM_ENTRY_SIZE;
    printf("    hash table fulless: %f, hash bucket is %s\r\n", fulless, fulless < .3 ? "healthy":"too full" );
#ifdef PROFILE_MEMORY_USAGE
    print_top_10_locations();
#endif
#endif
}


void eb_increase_component_count()
{
#ifdef DEBUG_MEMORY_USAGE
    EbHandle m = get_malloc_mutex();
    eb_block_on_mutex(m);
    g_component_count++;
    eb_release_mutex(m);
#endif
}

#ifdef DEBUG_MEMORY_USAGE
static EbBool print_leak(MemoryEntry* e, void* param)
{
    if (e->ptr) {
        EbBool* leaked = (EbBool*)param;
        *leaked = EB_TRUE;
        fprintf(stderr, "SVt: %s leaked at %s:L%d\r\n", mem_type_name(e->type), e->file, e->line);
    }
    //loop through all items
    return EB_FALSE;
}
#endif

void eb_decrease_component_count()
{
#ifdef DEBUG_MEMORY_USAGE
    EbHandle m = get_malloc_mutex();
    eb_block_on_mutex(m);
    g_component_count--;
    if (!g_component_count) {
        EbBool leaked = EB_FALSE;
        for_each_hash_entry(g_mem_entry, 0, print_leak, &leaked);
        if (!leaked) {
            printf("SVT: you have no memory leak\r\n");
        }
    }
    eb_release_mutex(m);
#endif
}
// clang-format on
