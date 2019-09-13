/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <set>
#include <vector>

#include "mongo/db/exec/plan_stage.h"
#include "mongo/db/exec/sort_executor.h"
#include "mongo/db/exec/sort_key_generator.h"
#include "mongo/db/exec/working_set.h"
#include "mongo/db/record_id.h"

namespace mongo {

/**
 * Sorts the input received from the child according to the sort pattern provided.
 *
 * Preconditions:
 *   -- For each field in 'pattern', all inputs in the child must handle a getFieldDotted for that
 *   field.
 *   -- All WSMs produced by the child stage must have the sort key available as WSM computed data.
 */
class SortStage final : public PlanStage {
public:
    SortStage(boost::intrusive_ptr<ExpressionContext> expCtx,
              WorkingSet* ws,
              BSONObj sortPattern,
              uint64_t limit,
              uint64_t maxMemoryUsageBytes,
              std::unique_ptr<PlanStage> child);

    bool isEOF() final;
    StageState doWork(WorkingSetID* out) final;

    StageType stageType() const final {
        return STAGE_SORT;
    }

    std::unique_ptr<PlanStageStats> getStats();

    const SpecificStats* getSpecificStats() const final;

    static const char* kStageType;

private:
    // Not owned by us.
    WorkingSet* _ws;

    // TODO SERVER-42182: Use SortExecutor to implement 'doWork()'.
    SortExecutor _sortExecutor;

    // The raw sort _pattern as expressed by the user
    BSONObj _pattern;

    // Equal to 0 for no limit.
    size_t _limit;

    //
    // Data storage
    //

    // Have we sorted our data? If so, we can access _resultIterator. If not,
    // we're still populating _data.
    bool _sorted;

    // Collection of working set members to sort with their respective sort key.
    struct SortableDataItem {
        WorkingSetID wsid;
        BSONObj sortKey;
        // Since we must replicate the behavior of a covered sort as much as possible we use the
        // RecordId to break sortKey ties.
        // See sorta.js.
        RecordId recordId;
    };

    // Comparison object for data buffers (vector and set). Items are compared on (sortKey, loc).
    // This is also how the items are ordered in the indices. Keys are compared using
    // BSONObj::woCompare() with RecordId as a tie-breaker.
    //
    // We are comparing keys generated by the SortKeyGenerator, which are already ordered with
    // respect the collation. Therefore, we explicitly avoid comparing using a collator here.
    struct WorkingSetComparator {
        explicit WorkingSetComparator(BSONObj p);

        bool operator()(const SortableDataItem& lhs, const SortableDataItem& rhs) const;

        BSONObj pattern;
    };

    /**
     * Inserts one item into data buffer (vector or set).
     * If limit is exceeded, remove item with lowest key.
     */
    void addToBuffer(const SortableDataItem& item);

    /**
     * Sorts data buffer.
     * Assumes no more items will be added to buffer.
     * If data is stored in set, copy set
     * contents to vector and clear set.
     */
    void sortBuffer();

    // Comparator for data buffer
    // Initialization follows sort key generator
    std::unique_ptr<WorkingSetComparator> _sortKeyComparator;

    // The data we buffer and sort.
    // _data will contain sorted data when all data is gathered
    // and sorted.
    // When _limit is greater than 1 and not all data has been gathered from child stage,
    // _dataSet is used instead to maintain an ordered set of the incomplete data set.
    // When the data set is complete, we copy the items from _dataSet to _data which will
    // be used to provide the results of this stage through _resultIterator.
    std::vector<SortableDataItem> _data;
    typedef std::set<SortableDataItem, WorkingSetComparator> SortableDataItemSet;
    std::unique_ptr<SortableDataItemSet> _dataSet;

    // Iterates through _data post-sort returning it.
    std::vector<SortableDataItem>::iterator _resultIterator;

    SortStats _specificStats;

    // The usage in bytes of all buffered data that we're sorting.
    size_t _memUsage;
};

}  // namespace mongo
