/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
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

#define _DEFAULT_SOURCE

#include "os.h"
#include "taoserror.h"
#include "tchecksum.h"
#include "tfile.h"
#include "walInt.h"

#if 0
static int32_t walRestoreWalFile(SWal *pWal, void *pVnode, FWalWrite writeFp, char *name, int64_t fileId);

int32_t walRenew(void *handle) {
  if (handle == NULL) return 0;

  SWal *  pWal = handle;
  int32_t code = 0;

  /*if (pWal->stop) {*/
    /*wDebug("vgId:%d, do not create a new wal file", pWal->vgId);*/
    /*return 0;*/
  /*}*/

  pthread_mutex_lock(&pWal->mutex);

  if (tfValid(pWal->logTfd)) {
    tfClose(pWal->logTfd);
    wDebug("vgId:%d, file:%s, it is closed while renew", pWal->vgId, pWal->logName);
  }

  /*if (pWal->keep == TAOS_WAL_KEEP) {*/
    /*pWal->fileId = 0;*/
  /*} else {*/
    /*if (walGetNewFile(pWal, &pWal->fileId) != 0) pWal->fileId = 0;*/
    /*pWal->fileId++;*/
  /*}*/

  snprintf(pWal->logName, sizeof(pWal->logName), "%s/%s%" PRId64, pWal->path, WAL_PREFIX, pWal->curFileId);
  pWal->logTfd = tfOpenCreateWrite(pWal->logName);

  if (!tfValid(pWal->logTfd)) {
    code = TAOS_SYSTEM_ERROR(errno);
    wError("vgId:%d, file:%s, failed to open since %s", pWal->vgId, pWal->logName, strerror(errno));
  } else {
    wDebug("vgId:%d, file:%s, it is created and open while renew", pWal->vgId, pWal->logName);
  }

  pthread_mutex_unlock(&pWal->mutex);

  return code;
}

void walRemoveOneOldFile(void *handle) {
  SWal *pWal = handle;
  if (pWal == NULL) return;
  /*if (pWal->keep == TAOS_WAL_KEEP) return;*/
  if (!tfValid(pWal->logTfd)) return;

  pthread_mutex_lock(&pWal->mutex);

  // remove the oldest wal file
  int64_t oldFileId = -1;
  if (walGetOldFile(pWal, pWal->curFileId, WAL_FILE_NUM, &oldFileId) == 0) {
    char walName[WAL_FILE_LEN] = {0};
    snprintf(walName, sizeof(walName), "%s/%s%" PRId64, pWal->path, WAL_PREFIX, oldFileId);

    if (remove(walName) < 0) {
      wError("vgId:%d, file:%s, failed to remove since %s", pWal->vgId, walName, strerror(errno));
    } else {
      wInfo("vgId:%d, file:%s, it is removed", pWal->vgId, walName);
    }
  }

  pthread_mutex_unlock(&pWal->mutex);
}

void walRemoveAllOldFiles(void *handle) {
  if (handle == NULL) return;

  SWal *  pWal = handle;
  int64_t fileId = -1;

  pthread_mutex_lock(&pWal->mutex);
  
  tfClose(pWal->logTfd);
  wDebug("vgId:%d, file:%s, it is closed before remove all wals", pWal->vgId, pWal->logName);

  while (walGetNextFile(pWal, &fileId) >= 0) {
    snprintf(pWal->logName, sizeof(pWal->logName), "%s/%s%" PRId64, pWal->path, WAL_PREFIX, fileId);

    if (remove(pWal->logName) < 0) {
      wError("vgId:%d, wal:%p file:%s, failed to remove since %s", pWal->vgId, pWal, pWal->logName, strerror(errno));
    } else {
      wInfo("vgId:%d, wal:%p file:%s, it is removed", pWal->vgId, pWal, pWal->logName);
    }
  }
  pthread_mutex_unlock(&pWal->mutex);
}
#endif

static void walUpdateChecksum(SWalHead *pHead) {
  pHead->sver = 2;
  pHead->cksum = taosCalcChecksum(0, (uint8_t *)pHead, sizeof(SWalHead) + pHead->len);
}

static int walValidateChecksum(SWalHead *pHead) {
  if (pHead->sver == 0) { // for compatible with wal before sver 1
    return taosCheckChecksumWhole((uint8_t *)pHead, sizeof(*pHead));
  } else if (pHead->sver >= 1) {
    uint32_t cksum = pHead->cksum;
    pHead->cksum = 0;
    return taosCheckChecksum((uint8_t *)pHead, sizeof(*pHead) + pHead->len, cksum);
  }

  return 0;
}

int64_t walWrite(SWal *pWal, int64_t index, void *body, int32_t bodyLen) {
  if (pWal == NULL) return -1;

  SWalHead *pHead = malloc(sizeof(SWalHead) + bodyLen);
  if(pHead == NULL) {
    return -1;
  }
  pHead->version = index;
  int32_t code = 0;

  // no wal
  if (!tfValid(pWal->curLogTfd)) return 0;
  if (pWal->level == TAOS_WAL_NOLOG) return 0;
  if (pHead->version <= pWal->curVersion) return 0;

  pHead->signature = WAL_SIGNATURE;
  pHead->len = bodyLen;
  memcpy(pHead->cont, body, bodyLen);

  walUpdateChecksum(pHead);

  int32_t contLen = pHead->len + sizeof(SWalHead);

  pthread_mutex_lock(&pWal->mutex);

  if (tfWrite(pWal->curLogTfd, pHead, contLen) != contLen) {
    code = TAOS_SYSTEM_ERROR(errno);
    wError("vgId:%d, file:%"PRId64".log, failed to write since %s", pWal->vgId, pWal->curFileFirstVersion, strerror(errno));
  } else {
    /*wTrace("vgId:%d, write wal, fileId:%" PRId64 " tfd:%" PRId64 " hver:%" PRId64 " wver:%" PRIu64 " len:%d", pWal->vgId,*/
           /*pWal->curFileId, pWal->logTfd, pHead->version, pWal->curVersion, pHead->len);*/
    pWal->curVersion = pHead->version;
  }

  pthread_mutex_unlock(&pWal->mutex);

  ASSERT(contLen == pHead->len + sizeof(SWalHead));

  return code;
}

void walFsync(SWal *pWal, bool forceFsync) {
  if (pWal == NULL || !tfValid(pWal->curLogTfd)) return;

  if (forceFsync || (pWal->level == TAOS_WAL_FSYNC && pWal->fsyncPeriod == 0)) {
    wTrace("vgId:%d, fileId:%"PRId64".log, do fsync", pWal->vgId, pWal->curFileFirstVersion);
    if (tfFsync(pWal->curLogTfd) < 0) {
      wError("vgId:%d, file:%"PRId64".log, fsync failed since %s", pWal->vgId, pWal->curFileFirstVersion, strerror(errno));
    }
  }
}

#if 0
int32_t walRestore(void *handle, void *pVnode, FWalWrite writeFp) {
  if (handle == NULL) return -1;

  SWal *  pWal = handle;
  int32_t count = 0;
  int32_t code = 0;
  int64_t fileId = -1;

  while ((code = walGetNextFile(pWal, &fileId)) >= 0) {
    /*if (fileId == pWal->curFileId) continue;*/

    char walName[WAL_FILE_LEN];
    snprintf(walName, sizeof(pWal->logName), "%s/%s%" PRId64, pWal->path, WAL_PREFIX, fileId);

    wInfo("vgId:%d, file:%s, will be restored", pWal->vgId, walName);
    code = walRestoreWalFile(pWal, pVnode, writeFp, walName, fileId);
    if (code != TSDB_CODE_SUCCESS) {
      wError("vgId:%d, file:%s, failed to restore since %s", pWal->vgId, walName, tstrerror(code));
      continue;
    }

    wInfo("vgId:%d, file:%s, restore success, wver:%" PRIu64, pWal->vgId, walName, pWal->curVersion);

    count++;
  }

  /*if (pWal->keep != TAOS_WAL_KEEP) return TSDB_CODE_SUCCESS;*/

  if (count == 0) {
    wDebug("vgId:%d, wal file not exist, renew it", pWal->vgId);
    return walRenew(pWal);
  } else {
    // open the existing WAL file in append mode
    /*pWal->curFileId = 0;*/
    snprintf(pWal->logName, sizeof(pWal->logName), "%s/%s%" PRId64, pWal->path, WAL_PREFIX, pWal->curFileId);
    pWal->logTfd = tfOpenCreateWriteAppend(pWal->logName);
    if (!tfValid(pWal->logTfd)) {
      wError("vgId:%d, file:%s, failed to open since %s", pWal->vgId, pWal->logName, strerror(errno));
      return TAOS_SYSTEM_ERROR(errno);
    }
    wDebug("vgId:%d, file:%s, it is created and open while restore", pWal->vgId, pWal->logName);
  }

  return TSDB_CODE_SUCCESS;
}

int32_t walGetWalFile(void *handle, char *fileName, int64_t *fileId) {
  if (handle == NULL) return -1;
  SWal *pWal = handle;

  if (*fileId == 0) *fileId = -1;

  pthread_mutex_lock(&(pWal->mutex));

  int32_t code = walGetNextFile(pWal, fileId);
  if (code >= 0) {
    sprintf(fileName, "wal/%s%" PRId64, WAL_PREFIX, *fileId);
    /*code = (*fileId == pWal->curFileId) ? 0 : 1;*/
  }

  wDebug("vgId:%d, get wal file, code:%d curId:%" PRId64 " outId:%" PRId64, pWal->vgId, code, pWal->curFileId, *fileId);
  pthread_mutex_unlock(&(pWal->mutex));

  return code;
}
#endif

static void walFtruncate(SWal *pWal, int64_t tfd, int64_t offset) {
  tfFtruncate(tfd, offset);
  tfFsync(tfd);
}

static int32_t walSkipCorruptedRecord(SWal *pWal, SWalHead *pHead, int64_t tfd, int64_t *offset) {
  int64_t pos = *offset;
  while (1) {
    pos++;

    if (tfLseek(tfd, pos, SEEK_SET) < 0) {
      wError("vgId:%d, failed to seek from corrupted wal file since %s", pWal->vgId, strerror(errno));
      return TSDB_CODE_WAL_FILE_CORRUPTED;
    }

    if (tfRead(tfd, pHead, sizeof(SWalHead)) <= 0) {
      wError("vgId:%d, read to end of corrupted wal file, offset:%" PRId64, pWal->vgId, pos);
      return TSDB_CODE_WAL_FILE_CORRUPTED;
    }

    if (pHead->signature != WAL_SIGNATURE) {
      continue;
    }

    if (pHead->sver >= 1) {
      if (tfRead(tfd, pHead->cont, pHead->len) < pHead->len) {
	wError("vgId:%d, read to end of corrupted wal file, offset:%" PRId64, pWal->vgId, pos);
	return TSDB_CODE_WAL_FILE_CORRUPTED;
      }

      if (walValidateChecksum(pHead)) {
	wInfo("vgId:%d, wal whole cksum check passed, offset:%" PRId64, pWal->vgId, pos);
	*offset = pos;
	return TSDB_CODE_SUCCESS;
      }
    }
  }

  return TSDB_CODE_WAL_FILE_CORRUPTED;
}

static int32_t walRestoreWalFile(SWal *pWal, void *pVnode, FWalWrite writeFp, char *name, int64_t fileId) {
  int32_t size = WAL_MAX_SIZE;
  void *  buffer = malloc(size);
  if (buffer == NULL) {
    wError("vgId:%d, file:%s, failed to open for restore since %s", pWal->vgId, name, strerror(errno));
    return TAOS_SYSTEM_ERROR(errno);
  }

  int64_t tfd = tfOpenReadWrite(name);
  if (!tfValid(tfd)) {
    wError("vgId:%d, file:%s, failed to open for restore since %s", pWal->vgId, name, strerror(errno));
    tfree(buffer);
    return TAOS_SYSTEM_ERROR(errno);
  } else {
    wDebug("vgId:%d, file:%s, open for restore", pWal->vgId, name);
  }

  int32_t   code = TSDB_CODE_SUCCESS;
  int64_t   offset = 0;
  SWalHead *pHead = buffer;

  while (1) {
    int32_t ret = (int32_t)tfRead(tfd, pHead, sizeof(SWalHead));
    if (ret == 0) break;

    if (ret < 0) {
      wError("vgId:%d, file:%s, failed to read wal head since %s", pWal->vgId, name, strerror(errno));
      code = TAOS_SYSTEM_ERROR(errno);
      break;
    }

    if (ret < sizeof(SWalHead)) {
      wError("vgId:%d, file:%s, failed to read wal head, ret is %d", pWal->vgId, name, ret);
      walFtruncate(pWal, tfd, offset);
      break;
    }

    if ((pHead->sver == 0 && !walValidateChecksum(pHead)) || pHead->sver < 0 || pHead->sver > 2) {
      wError("vgId:%d, file:%s, wal head cksum is messed up, hver:%" PRIu64 " len:%d offset:%" PRId64, pWal->vgId, name,
             pHead->version, pHead->len, offset);
      code = walSkipCorruptedRecord(pWal, pHead, tfd, &offset);
      if (code != TSDB_CODE_SUCCESS) {
        walFtruncate(pWal, tfd, offset);
        break;
      }
    }

    if (pHead->len < 0 || pHead->len > size - sizeof(SWalHead)) {
      wError("vgId:%d, file:%s, wal head len out of range, hver:%" PRIu64 " len:%d offset:%" PRId64, pWal->vgId, name,
             pHead->version, pHead->len, offset);
      code = walSkipCorruptedRecord(pWal, pHead, tfd, &offset);
      if (code != TSDB_CODE_SUCCESS) {
        walFtruncate(pWal, tfd, offset);
        break;
      }
    }

    ret = (int32_t)tfRead(tfd, pHead->cont, pHead->len);
    if (ret < 0) {
      wError("vgId:%d, file:%s, failed to read wal body since %s", pWal->vgId, name, strerror(errno));
      code = TAOS_SYSTEM_ERROR(errno);
      break;
    }

    if (ret < pHead->len) {
      wError("vgId:%d, file:%s, failed to read wal body, ret:%d len:%d", pWal->vgId, name, ret, pHead->len);
      offset += sizeof(SWalHead);
      continue;
    }

    if ((pHead->sver >= 1) && !walValidateChecksum(pHead)) {
      wError("vgId:%d, file:%s, wal whole cksum is messed up, hver:%" PRIu64 " len:%d offset:%" PRId64, pWal->vgId, name,
             pHead->version, pHead->len, offset);
      code = walSkipCorruptedRecord(pWal, pHead, tfd, &offset);
      if (code != TSDB_CODE_SUCCESS) {
        walFtruncate(pWal, tfd, offset);
        break;
      }
    }

    offset = offset + sizeof(SWalHead) + pHead->len;

    wTrace("vgId:%d, restore wal, fileId:%" PRId64 " hver:%" PRIu64 " wver:%" PRIu64 " len:%d offset:%" PRId64,
           pWal->vgId, fileId, pHead->version, pWal->curVersion, pHead->len, offset);

    pWal->curVersion = pHead->version;

    // wInfo("writeFp: %ld", offset);
    (*writeFp)(pVnode, pHead);
  }

  tfClose(tfd);
  tfree(buffer);

  wDebug("vgId:%d, file:%s, it is closed after restore", pWal->vgId, name);
  return code;
}

uint64_t walGetVersion(SWal *pWal) {
  if (pWal == NULL) return 0;

  return pWal->curVersion;
}

// Wal version in slave (dnode1) must be reset. 
// Because after the data file is recovered from peer (dnode2), the new file version in dnode1 may become smaller than origin.
// Some new wal record cannot be written to the wal file in dnode1 for wal version not reset, then fversion and the record in wal file may inconsistent, 
// At this time, if dnode2 down, dnode1 switched to master. After dnode2 start and restore data from dnode1, data loss will occur

void walResetVersion(SWal *pWal, uint64_t newVer) {
  if (pWal == NULL) return;
  wInfo("vgId:%d, version reset from %" PRIu64 " to %" PRIu64, pWal->vgId, pWal->curVersion, newVer);

  pWal->curVersion = newVer;
}
