/*
 * This file and its contents are licensed under the Timescale License.
 * Please see the included NOTICE for copyright information and
 * LICENSE-TIMESCALE for a copy of the license.
 */
#include <postgres.h>
#include <foreign/fdwapi.h>
#include <planner.h>
#include "planner.h"
#include "nodes/gapfill/planner.h"
#include "hypertable_cache.h"
#include "compat.h"
#if !PG96
#include "fdw/fdw.h"
#include "fdw/data_node_scan_plan.h"
#endif
#include "guc.h"
#include "async_append.h"

void
tsl_create_upper_paths_hook(PlannerInfo *root, UpperRelationKind stage, RelOptInfo *input_rel,
							RelOptInfo *output_rel, void *extra)
{
	if (UPPERREL_GROUP_AGG == stage)
		plan_add_gapfill(root, output_rel);
	else if (UPPERREL_WINDOW == stage)
	{
		if (IsA(linitial(input_rel->pathlist), CustomPath))
			gapfill_adjust_window_targetlist(root, input_rel, output_rel);
	}
#if PG11_GE
	else if (UPPERREL_FINAL == stage && root->parse->resultRelation == 0)
		async_append_add_paths(root, output_rel);
#endif
}

#if PG11_GE
/* The fdw needs to expand a distributed hypertable inside the `GetForeignPath` callback. But, since
 * the hypertable base table is not a foreign table, that callback would not normally be called.
 * Thus, we call it manually in this hook.
 */
void
tsl_set_rel_pathlist(PlannerInfo *root, RelOptInfo *rel, Index rti, RangeTblEntry *rte)
{
	Cache *hcache = ts_hypertable_cache_pin();
	Hypertable *ht = ts_hypertable_cache_get_entry(hcache, rte->relid);

	if (rel->fdw_private != NULL && ht != NULL && hypertable_is_distributed(ht))
	{
		FdwRoutine *fdw = (FdwRoutine *) DatumGetPointer(
			DirectFunctionCall1(timescaledb_fdw_handler, PointerGetDatum(NULL)));

		fdw->GetForeignRelSize(root, rel, rte->relid);
		fdw->GetForeignPaths(root, rel, rte->relid);
	}

	ts_cache_release(hcache);
}
#endif
