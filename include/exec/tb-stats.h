/*
 * QEMU System Emulator, Code Quality Monitor System
 *
 * Copyright (c) 2019 Vanderson M. do Rosario <vandersonmr2@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef TB_STATS_H

#define TB_STATS_H

#include "exec/cpu-common.h"
#include "exec/tb-context.h"
#include "tcg.h"

#define tb_stats_enabled(tb, JIT_STATS) \
    (tb && tb->tb_stats && (tb->tb_stats->stats_enabled & JIT_STATS))

#define stat_per_translation(stat, name) \
    (stat->translations.total ? stat->name / stat->translations.total : 0)

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

    /* Execution stats */
    struct {
        unsigned long normal;
        unsigned long atomic;
    } executions;

    struct {
        unsigned num_guest_inst;
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

    struct {
        unsigned long total;
        unsigned long uncached;
        unsigned long spanning;
    } translations;

    struct {
        int64_t restore;
        uint64_t restore_count;
        int64_t interm;
        int64_t code;
        int64_t opt;
        int64_t la;
    } time;

    /* current TB linked to this TBStatistics */
    TranslationBlock *tb;
};

bool tb_stats_cmp(const void *ap, const void *bp);

void dump_jit_exec_time_info(uint64_t dev_time);

void init_tb_stats_htable_if_not(void);

void dump_jit_profile_info(TCGProfile *s);

/* TBStatistic collection controls */
enum TBStatsStatus {
    TB_STATS_DISABLED = 0,
    TB_STATS_RUNNING,
    TB_STATS_PAUSED,
    TB_STATS_STOPPED
};

#define TB_NOTHING    (1 << 0)
#define TB_EXEC_STATS (1 << 1)
#define TB_JIT_STATS  (1 << 2)
#define TB_JIT_TIME   (1 << 3)

extern int tcg_collect_tb_stats;
extern uint32_t default_tbstats_flag;

void enable_collect_tb_stats(void);
void disable_collect_tb_stats(void);
void pause_collect_tb_stats(void);
bool tb_stats_collection_enabled(void);
bool tb_stats_collection_paused(void);

uint32_t get_default_tbstats_flag(void);

#endif
