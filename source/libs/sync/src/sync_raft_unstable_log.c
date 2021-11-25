/*
 * Copyright (c) 2019 TAOS Data, Inc. <cli@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "sync_raft_entry.h"
#include "sync_raft_proto.h"
#include "sync_raft_unstable_log.h"

static void unstableSliceEntries(SSyncRaftUnstableLog* unstable, SyncIndex lo, SyncIndex hi, SSyncRaftEntry** ppEntries, int* n);

// unstable.entries[i] has raft log position i+unstable.offset.
// Note that unstable.offset may be less than the highest log
// position in storage; this means that the next write to storage
// might need to truncate the log before persisting unstable.entries.
struct SSyncRaftUnstableLog {
  SyncRaftSnapshot* snapshot;

  // all entries that have not yet been written to storage.
  SSyncRaftEntryArray* entries;

  SyncIndex offset;
};

SSyncRaftUnstableLog* syncRaftCreateUnstableLog(SyncIndex lastIndex) {
  SSyncRaftUnstableLog* unstable = (SSyncRaftUnstableLog*)malloc(sizeof(SSyncRaftUnstableLog));
  if (unstable == NULL) {
    return NULL;
  }

  unstable->entries = syncRaftCreateEntryArray();
  if (unstable->entries == NULL) {
    free(unstable);
    return NULL;
  }
  unstable->offset = lastIndex + 1;

  return unstable;
}

// maybeFirstIndex returns the index of the first possible entry in entries
// if it has a snapshot.
bool syncRaftUnstableLogMaybeFirstIndex(const SSyncRaftUnstableLog* unstable, SyncIndex* index) {
  if (unstable->snapshot != NULL) {
    *index = unstable->snapshot->meta.index + 1;
    return true;
  }

  *index = 0;
  return false;
}

// maybeLastIndex returns the last index if it has at least one
// unstable entry or snapshot.
bool syncRaftUnstableLogMaybeLastIndex(const SSyncRaftUnstableLog* unstable, SyncIndex* index) {
  int num = syncRaftNumOfEntries(unstable->entries);
  if (num > 0) {
    *index = unstable->offset + num + 1;
    return true;
  }
  if (unstable->snapshot != NULL) {
    *index = unstable->snapshot->meta.index + 1;
    return true;
  }

  *index = 0;
  return false;
}

// maybeTerm returns the term of the entry at index i, if there
// is any.
bool syncRaftUnstableLogMaybeTerm(const SSyncRaftUnstableLog* unstable, SyncIndex i, SyncTerm* term) {
  *term = SYNC_NON_TERM;

  if (i < unstable->offset) {
    if (unstable->snapshot != NULL && unstable->snapshot->meta.index == i) {
      *term = unstable->snapshot->meta.term;
      return true;
    }

    return false;
  }
  
  SyncIndex lastIndex;
  bool ok = syncRaftUnstableLogMaybeLastIndex(unstable, &lastIndex);
  if (!ok) {
    return false;
  }
  if (i > lastIndex) {
    return false;
  }

  *term = syncRaftTermOfPosition(unstable, i - unstable->offset);
  return true;
}

void syncRaftUnstableLogStableTo(SSyncRaftUnstableLog* unstable, SyncIndex i, SyncTerm term) {
  SyncTerm gt;
  bool ok = syncRaftUnstableLogMaybeTerm(unstable, i, &gt);
  if (!ok) {
    return;
  }

	// if i < offset, term is matched with the snapshot
	// only update the unstable entries if term is matched with
	// an unstable entry.
  if (gt == term && i >= unstable->offset) {
    syncRaftRemoveEntriesBeforePosition(unstable->entries, i - unstable->offset);
    unstable->offset += 1;
  }
}

int syncRaftUnstableLogTruncateAndAppend(SSyncRaftUnstableLog* unstable, SSyncRaftEntry* entries, int n) {
  SyncIndex afterIndex = entries[0].index;
  int num = syncRaftNumOfEntries(unstable->entries);

  if (afterIndex == unstable->offset + num) {
    return syncRaftAppendEntries(unstable->entries, entries, n);
  }

  if (afterIndex <= unstable->offset) {
    unstable->offset = afterIndex;
    return syncRaftAssignEntries(unstable->entries, entries, n);
  }

  SSyncRaftEntry* sliceEnts;
  int nSliceEnts;

  unstableSliceEntries(unstable, unstable->offset, afterIndex, &sliceEnts, &nSliceEnts);

  syncRaftAppendEntries(unstable->entries, sliceEnts, nSliceEnts);
  return syncRaftAppendEntries(unstable->entries, entries, n);
}

static void unstableSliceEntries(SSyncRaftUnstableLog* unstable, SyncIndex lo, SyncIndex hi, SSyncRaftEntry** ppEntries, int* n) {
  syncRaftSliceEntries(unstable->entries, lo-unstable->offset, hi - unstable->offset, ppEntries, n);
}