#ifndef TB_STATS_H

#define TB_STATS_H

#include "exec/cpu-common.h"
#include "exec/tb-context.h"

enum SortBy { SORT_BY_HOTNESS, SORT_BY_HG /* Host/Guest */, SORT_BY_SPILLS};

#define TB_NOTHING    0
#define TB_EXEC_STATS (1 << 1)
#define TB_JIT_STATS  (1 << 2)
#define TB_PAUSED     (1 << 3)

#define tb_stats_enabled(tb, JIT_STATS) \
    (tb && tb->tb_stats && (tb->tb_stats->stats_enabled & JIT_STATS))

typedef struct TBStatistics TBStatistics;

/*
 * This struct stores statistics such as execution count of the
 * TranslationBlocks. Each sets of TBs for a given phys_pc/pc/flags
 * has its own TBStatistics which will persist over tb_flush.
 *
 * We include additional counters to track number of translations as
 * well as variants for compile flags.
 */
struct TBStatistics {
    tb_page_addr_t phys_pc;
    target_ulong pc;
    uint32_t     flags;
    /* cs_base isn't included in the hash but we do check for matches */
    target_ulong cs_base;

    uint32_t stats_enabled;

    /* Translation stats */
    struct {
        unsigned long total;
        unsigned long uncached;
        unsigned long spanning;
    } translations;

    /* Execution stats */
    struct {
        unsigned long total;
        unsigned long atomic;
    } executions;

    struct {
        unsigned num_guest_inst;
        unsigned num_host_inst;
        unsigned num_tcg_ops;
        unsigned num_tcg_ops_opt;
        unsigned spills;

        /* CONFIG_PROFILE */
        unsigned temps;
        unsigned deleted_ops;
        unsigned in_len;
        unsigned out_len;
        unsigned search_out_len;
    } code;

    /* HMP information - used for referring to previous search */
    int display_id;
};

bool tb_stats_cmp(const void *ap, const void *bp);

/**
 * dump_coverset_info: report the hottest blocks to cover n% of execution
 *
 * @percentage: cover set percentage
 * @use_monitor: redirect output to monitor
 *
 * Report the hottest blocks to either the log or monitor
 */
void dump_coverset_info(int percentage, bool use_monitor);


/**
 * dump_tbs_info: report the hottest blocks
 *
 * @count: the limit of hotblocks
 * @sort_by: property in which the dump will be sorted
 * @use_monitor: redirect output to monitor
 *
 * Report the hottest blocks to either the log or monitor
 */
void dump_tbs_info(int count, int sort_by, bool use_monitor);

/**
 * dump_tb_info: dump information about one TB
 *
 * @id: the display id of the block (from previous search)
 * @mask: the temporary logging mask
 * @Use_monitor: redirect output to monitor
 *
 * Re-run a translation of a block at addr for the purposes of debug output
 */
void dump_tb_info(int id, int log_mask, bool use_monitor);

/**
 * clean_tbstats_info: remove all tb_stats information
 *
 */
void set_tbstats_flags(uint32_t flags);
void clean_tbstats(void);

void dump_jit_profile_info(void);

#endif
