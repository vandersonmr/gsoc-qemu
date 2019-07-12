#include "qemu/osdep.h"

#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg.h"

#include "qemu/qemu-print.h"

/* only accessed in safe work */
static GList *last_search;

static void collect_tb_stats(void *p, uint32_t hash, void *userp)
{
    last_search = g_list_prepend(last_search, p);
}

static void dump_tb_header(TBStatistics *tbs)
{
    unsigned g = tbs->translations.total ?
        tbs->code.num_guest_inst / tbs->translations.total : 0;
    unsigned ops = tbs->translations.total ?
        tbs->code.num_tcg_ops / tbs->translations.total : 0;
    unsigned ops_opt = tbs->translations.total ?
        tbs->code.num_tcg_ops_opt / tbs->translations.total : 0;
    unsigned h = tbs->translations.total ?
        tbs->code.num_host_inst / tbs->translations.total : 0;
    unsigned spills = tbs->translations.total ?
        tbs->code.spills / tbs->translations.total : 0;

    float guest_host_prop = g ? ((float) h / g) : 0;

    qemu_log("TB%d: phys:0x"TB_PAGE_ADDR_FMT" virt:0x"TARGET_FMT_lx
             " flags:%#08x (trans:%lu uncached:%lu exec:%lu ints: g:%u op:%u op_opt:%u h:%u h/g:%.2f spills:%d)\n",
             tbs->display_id,
             tbs->phys_pc, tbs->pc, tbs->flags,
             tbs->translations.total, tbs->translations.uncached,
             tbs->executions.total, g, ops, ops_opt, h, guest_host_prop,
             spills);
}

static gint
inverse_sort_tbs(gconstpointer p1, gconstpointer p2, gpointer psort_by)
{
    const TBStatistics *tbs1 = (TBStatistics *) p1;
    const TBStatistics *tbs2 = (TBStatistics *) p2;
    int sort_by = *((int *) psort_by);
    unsigned long c1 = 0;
    unsigned long c2 = 0;

    if (likely(sort_by == SORT_BY_SPILLS)) {
        c1 = tbs1->code.spills;
        c2 = tbs2->code.spills;
    } else if (likely(sort_by == SORT_BY_HOTNESS)) {
        c1 = tbs1->executions.total;
        c2 = tbs2->executions.total;
    } else if (likely(sort_by == SORT_BY_HG)) {
        if (tbs1->code.num_guest_inst == 0) {
            return -1;
        }
        if (tbs2->code.num_guest_inst == 0) {
            return 1;
        }

        float a = (float) tbs1->code.num_host_inst / tbs1->code.num_guest_inst;
        float b = (float) tbs2->code.num_host_inst / tbs2->code.num_guest_inst;
        c1 = a <= b ? 0 : 1;
        c2 = a <= b ? 1 : 0;
    }

    return c1 < c2 ? 1 : c1 == c2 ? 0 : -1;
}


static void do_dump_coverset_info(int percentage)
{
    uint64_t total_exec_count = 0;
    uint64_t covered_exec_count = 0;
    unsigned coverset_size = 0;
    int id = 1;
    GList *i;

    g_list_free(last_search);
    last_search = NULL;

    /* XXX: we could pass user data to collect_tb_stats to filter */
    qht_iter(&tb_ctx.tb_stats, collect_tb_stats, NULL);

    int sort_by = SORT_BY_HOTNESS;
    last_search = g_list_sort_with_data(last_search, inverse_sort_tbs, &sort_by);

    if (!last_search) {
        qemu_log("No data collected yet\n");
        return;
    }

    /* Compute total execution count for all tbs */
    for (i = last_search; i; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        total_exec_count += tbs->executions.total * tbs->code.num_guest_inst;
    }

    for (i = last_search; i; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        covered_exec_count += tbs->executions.total * tbs->code.num_guest_inst;
        tbs->display_id = id++;
        coverset_size++;
        dump_tb_header(tbs);

        /* Iterate and display tbs until reach the percentage count cover */
        if (((double) covered_exec_count / total_exec_count) >
                ((double) percentage / 100)) {
            break;
        }
    }

    qemu_log("\n------------------------------\n");
    qemu_log("# of TBs to reach %d%% of the total of guest insts exec: %u\t",
                percentage, coverset_size);
    qemu_log("Total of guest insts exec: %lu\n", total_exec_count);
    qemu_log("\n------------------------------\n");

    /* free the unused bits */
    i->next->prev = NULL;
    g_list_free(i->next);
    i->next = NULL;
}


static void do_dump_tbs_info(int count, int sort_by)
{
    int id = 1;
    GList *i;

    g_list_free(last_search);
    last_search = NULL;

    /* XXX: we could pass user data to collect_tb_stats to filter */
    qht_iter(&tb_ctx.tb_stats, collect_tb_stats, NULL);

    last_search = g_list_sort_with_data(last_search, inverse_sort_tbs,
                                        &sort_by);

    if (!last_search) {
        qemu_printf("No data collected yet!\n");
        return;
    }

    for (i = last_search; i && count--; i = i->next) {
        TBStatistics *tbs = (TBStatistics *) i->data;
        tbs->display_id = id++;
        dump_tb_header(tbs);
    }

    /* free the unused bits */
    if (i && i->next) {
        i->next->prev = NULL;
    }
    g_list_free(i->next);
    i->next = NULL;
}

static void
do_dump_coverset_info_safe(CPUState *cpu, run_on_cpu_data percentage)
{
    qemu_log_to_monitor(true);
    do_dump_coverset_info(percentage.host_int);
    qemu_log_to_monitor(false);
}

struct tbs_dump_info {
    int count;
    int sort_by;
};

static void do_dump_tbs_info_safe(CPUState *cpu, run_on_cpu_data tbdi)
{
    struct tbs_dump_info *info = tbdi.host_ptr;
    qemu_log_to_monitor(true);
    do_dump_tbs_info(info->count, info->sort_by);
    qemu_log_to_monitor(false);
    g_free(info);
}

/*
 * When we dump_tbs_info on a live system via the HMP we want to
 * ensure the system is quiessent before we start outputting stuff.
 * Otherwise we could pollute the output with other logging output.
 */
void dump_coverset_info(int percentage, bool use_monitor)
{
    if (use_monitor) {
        async_safe_run_on_cpu(first_cpu, do_dump_coverset_info_safe,
                              RUN_ON_CPU_HOST_INT(percentage));
    } else {
        do_dump_coverset_info(percentage);
    }
}

void dump_tbs_info(int count, int sort_by, bool use_monitor)
{
    if (use_monitor) {
        struct tbs_dump_info *tbdi = g_new(struct tbs_dump_info, 1);
        tbdi->count = count;
        tbdi->sort_by = sort_by;
        async_safe_run_on_cpu(first_cpu, do_dump_tbs_info_safe,
                              RUN_ON_CPU_HOST_PTR(tbdi));
    } else {
        do_dump_tbs_info(count, sort_by);
    }
}

static void do_tb_dump_with_statistics(TBStatistics *tbs, int log_flags)
{
    CPUState *cpu = current_cpu;
    uint32_t cflags = curr_cflags() | CF_NOCACHE;
    int old_log_flags = qemu_loglevel;
    TranslationBlock *tb = NULL;

    qemu_set_log(log_flags);

    qemu_log("\n------------------------------\n");
    dump_tb_header(tbs);

    if (sigsetjmp(cpu->jmp_env, 0) == 0) {
        mmap_lock();
        tb = tb_gen_code(cpu, tbs->pc, tbs->cs_base, tbs->flags, cflags);
        tb_phys_invalidate(tb, -1);
        mmap_unlock();
    } else {
        /*
         * The mmap_lock is dropped by tb_gen_code if it runs out of
         * memory.
         */
        fprintf(stderr, "%s: dbg failed!\n", __func__);
        assert_no_pages_locked();
    }

    qemu_set_log(old_log_flags);

    tcg_tb_remove(tb);
}

struct tb_dump_info {
    int id;
    int log_flags;
    bool use_monitor;
};

static void do_dump_tb_info_safe(CPUState *cpu, run_on_cpu_data info)
{
    struct tb_dump_info *tbdi = (struct tb_dump_info *) info.host_ptr;
    GList *iter;

    if (!last_search) {
        qemu_printf("no search on record");
        return;
    }
    qemu_log_to_monitor(tbdi->use_monitor);

    for (iter = last_search; iter; iter = g_list_next(iter)) {
        TBStatistics *tbs = iter->data;
        if (tbs->display_id == tbdi->id) {
            do_tb_dump_with_statistics(tbs, tbdi->log_flags);
        }
    }
    qemu_log_to_monitor(false);
    g_free(tbdi);
}

/* XXX: only from monitor? */
void dump_tb_info(int id, int log_mask, bool use_monitor)
{
    struct tb_dump_info *tbdi = g_new(struct tb_dump_info, 1);

    tbdi->id = id;
    tbdi->log_flags = log_mask;
    tbdi->use_monitor = use_monitor;

    async_safe_run_on_cpu(first_cpu, do_dump_tb_info_safe,
                          RUN_ON_CPU_HOST_PTR(tbdi));

    /* tbdi free'd by do_dump_tb_info_safe */
}

struct jit_profile_info {
    uint64_t translations;
    uint64_t aborted;
    uint64_t ops;
    unsigned ops_max;
    uint64_t del_ops;
    uint64_t temps;
    unsigned temps_max;
    uint64_t host;
    uint64_t host_ins;
    uint64_t search_data;
};

static void collect_jit_profile_info(void *p, uint32_t hash, void *userp)
{
    struct jit_profile_info *jpi = userp;
    TBStatistics *tbs = p;

    /* TODO: abort */
    jpi->translations += tbs->translations.total;
    jpi->ops += tbs->code.num_tcg_ops;
    if (tbs->translations.total && tbs->code.num_tcg_ops / tbs->translations.total
            > jpi->ops_max) {
        jpi->ops_max = tbs->code.num_tcg_ops / tbs->translations.total;
    }
    jpi->del_ops += tbs->code.deleted_ops;
    jpi->temps += tbs->code.temps;
    if (tbs->translations.total && tbs->code.temps / tbs->translations.total >
            jpi->temps_max) {
        jpi->temps_max = tbs->code.temps / tbs->translations.total;
    }
    jpi->host += tbs->code.out_len;
    jpi->host_ins += tbs->code.num_host_inst;
    jpi->search_data += tbs->code.search_out_len;
}

void dump_jit_profile_info(void)
{
    if (!tb_stats_collection_enabled()) {
        return;
    }

    struct jit_profile_info *jpi = g_new0(struct jit_profile_info, 1);

    qht_iter(&tb_ctx.tb_stats, collect_jit_profile_info, jpi);

    if (jpi->translations) {
        qemu_printf("translated TBs      %" PRId64 "\n", jpi->translations);
        qemu_printf("avg ops/TB          %0.1f max=%d\n",
                jpi->ops / (double) jpi->translations, jpi->ops_max);
        qemu_printf("deleted ops/TB      %0.2f\n",
                jpi->del_ops / (double) jpi->translations);
        qemu_printf("avg temps/TB        %0.2f max=%d\n",
                jpi->temps / (double) jpi->translations, jpi->temps_max);
        qemu_printf("avg host code/TB    %0.1f\n",
                jpi->host / (double) jpi->translations);
        qemu_printf("avg host ins/TB     %0.1f\n",
                jpi->host_ins / (double) jpi->translations);
        qemu_printf("avg search data/TB  %0.1f\n",
                jpi->search_data / (double) jpi->translations);
    }
}

void clean_tbstats_info(void)
{
/* TODO: remove all tb_stats */
}


