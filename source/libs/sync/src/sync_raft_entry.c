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

#include "sync_const.h"
#include "sync_raft_entry.h"
#include "sync_raft_proto.h"

struct SSyncRaftEntryArray {
  // Circular buffer of log entries
  SSyncRaftEntry *entries;

  // size of Circular buffer
  int size;

  // Indexes of used slots [front, back)
  int front, back;
};

static const SSyncRaftEntry* entryAt(const SSyncRaftEntryArray*, int i);
static int positionAt(const SSyncRaftEntryArray*, int i);

static void removePrefix(SSyncRaftEntryArray*, int pos);
static int numBeforePosition(const SSyncRaftEntryArray*, int pos);






static void resetEntries(SSyncRaftEntryArray*);

static int ensureCapacity(SSyncRaftEntryArray*, int n);

static int sliceEntries(SSyncRaftEntryArray* ents, SyncIndex from, SyncIndex to, SSyncRaftEntry **ppEntries, int* n);

static bool decrEntry(SSyncRaftEntry *entry);
static void incrEntry(SSyncRaftEntry *entry);

SSyncRaftEntryArray* syncRaftCreateEntryArray() {
  SSyncRaftEntry* entries = (SSyncRaftEntry*)malloc(sizeof(SSyncRaftEntry) * kEntryArrayInitSize);
  if (entries == NULL) {
    return NULL;
  }
  SSyncRaftEntryArray* ary = (SSyncRaftEntryArray*)malloc(sizeof(SSyncRaftEntryArray));
  if (ary == NULL) {
    free(entries);
    return NULL;
  }
  *ary = (SSyncRaftEntryArray) {
    .entries  = entries,
    .front    = 0,
    .back     = 0,
    .size     = kEntryArrayInitSize,
  };

  return ary;
}

int syncRaftNumOfEntries(const SSyncRaftEntryArray* ents) {
  // if circular buffer wrapped?
  if (ents->front > ents->back) {
    return ents->size + ents->back - ents->front;
  }

  return ents->back - ents->front;
}

const SSyncRaftEntry* syncRaftEntryOfPosition(const SSyncRaftEntryArray* ents, int pos) {
  return entryAt(ents, pos);
}

SyncTerm syncRaftTermOfPosition(const SSyncRaftEntryArray* ents, int pos) {
  return entryAt(ents, pos)->term;
}

// delete all entries before the given position(included)
void syncRaftRemoveEntriesBeforePosition(SSyncRaftEntryArray* ents, int pos) {
  removePrefix(ents, pos);
}

// return the index of circular buffer of the i'th entry in the log
static int positionAt(const SSyncRaftEntryArray* ents, int i) {
  return (ents->front + i) % ents->size;
}

static const SSyncRaftEntry* entryAt(const SSyncRaftEntryArray* ents, int i) {
  return &(ents->entries[positionAt(ents, i)]);
}

// delete all entries before the given position(included)
static void removePrefix(SSyncRaftEntryArray* ents, int pos) {
  int i, n;
  SSyncRaftEntry* entry;
  bool removeFront;

  assert(ents != NULL);

  // Number of entries to delete
  n = numBeforePosition(ents, pos);

  for (i = 0, removeFront = true; i < n; i++) {
    entry = &(ents->entries[ents->front]);
    if (!decrEntry(entry)) {
      removeFront = false;
      continue;
    }
    if (!removeFront) {
      continue;
    }
    if (ents->front == ents->size - 1) {
      ents->front = 0;
    } else {
      ents->front++;
    }
  }
}

// return number before position(included)
static int numBeforePosition(const SSyncRaftEntryArray* ents, int pos) {
  assert(pos >= 0 && pos < ents->size);

  int num = pos - ents->front + 1;
  if (num < 0) return num + ents->size;
  return num;
}


































// Ensure that the entries array has enough free slots for adding n new entry.
static int ensureCapacity(SSyncRaftEntryArray* ents, int n) {
  int size = syncRaftNumOfEntries(ents);

  if (size + n < ents->size) {
    return RAFT_OK;
  }

  int newSize = size * 2;
  while (newSize < size + n) {
    newSize += size;
  }

  SSyncRaftEntry *entries = (SSyncRaftEntry*)calloc(newSize, sizeof(SSyncRaftEntry));
  if (entries == NULL) {
    return RAFT_NO_MEM;
  }

  int i;
  for (i = 0; i < size; ++i) {
    memcpy(&entries[i], entryAt(ents, i), sizeof(SSyncRaftEntry*));
  }
  free(ents->entries);

  ents->entries = entries;
  ents->size = newSize;
  ents->front = 0;
  ents->back = size;

  return RAFT_OK;
}

static int sliceEntries(SSyncRaftEntryArray* ents, SyncIndex from, SyncIndex to, SSyncRaftEntry **ppEntries, int* n) {
  return RAFT_OK;
}



static bool decrEntry(SSyncRaftEntry *entry) {
  return true;
/*  
  entry->refCount -= 1;
  assert(entry->refCount >= 0);
  if (entry->refCount == 0) {
    free(entry->buffer.data);
    entry->buffer.len = 0;
    entry->term = SYNC_NON_TERM;
    return true;
  }

  return false;
*/
}

static void incrEntry(SSyncRaftEntry *entry) {
/*
  assert(entry->refCount >= 0);
  entry->refCount += 1;
*/
}