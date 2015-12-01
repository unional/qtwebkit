/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "MemoryBackingStoreTransaction.h"

#if ENABLE(INDEXED_DATABASE)

#include "IDBKeyRangeData.h"
#include "IndexedDB.h"
#include "Logging.h"
#include "MemoryIDBBackingStore.h"
#include "MemoryObjectStore.h"
#include <wtf/TemporaryChange.h>

namespace WebCore {
namespace IDBServer {

std::unique_ptr<MemoryBackingStoreTransaction> MemoryBackingStoreTransaction::create(MemoryIDBBackingStore& backingStore, const IDBTransactionInfo& info)
{
    return std::make_unique<MemoryBackingStoreTransaction>(backingStore, info);
}

MemoryBackingStoreTransaction::MemoryBackingStoreTransaction(MemoryIDBBackingStore& backingStore, const IDBTransactionInfo& info)
    : m_backingStore(backingStore)
    , m_info(info)
{
    if (m_info.mode() == IndexedDB::TransactionMode::VersionChange)
        m_originalDatabaseInfo = std::make_unique<IDBDatabaseInfo>(m_backingStore.getOrEstablishDatabaseInfo());
}

MemoryBackingStoreTransaction::~MemoryBackingStoreTransaction()
{
    ASSERT(!m_inProgress);
}

void MemoryBackingStoreTransaction::addNewObjectStore(MemoryObjectStore& objectStore)
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::addNewObjectStore()");

    ASSERT(isVersionChange());
    m_versionChangeAddedObjectStores.add(&objectStore);

    addExistingObjectStore(objectStore);
}

void MemoryBackingStoreTransaction::addNewIndex(MemoryIndex& index)
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::addNewIndex()");

    ASSERT(isVersionChange());
    m_versionChangeAddedIndexes.add(&index);

    addExistingIndex(index);
}

void MemoryBackingStoreTransaction::addExistingIndex(MemoryIndex& index)
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::addExistingIndex");

    ASSERT(isWriting());

    ASSERT(!m_indexes.contains(&index));
    m_indexes.add(&index);
}

void MemoryBackingStoreTransaction::addExistingObjectStore(MemoryObjectStore& objectStore)
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::addExistingObjectStore");

    ASSERT(isWriting());

    ASSERT(!m_objectStores.contains(&objectStore));
    m_objectStores.add(&objectStore);

    objectStore.writeTransactionStarted(*this);

    m_originalKeyGenerators.add(&objectStore, objectStore.currentKeyGeneratorValue());
}

void MemoryBackingStoreTransaction::objectStoreDeleted(std::unique_ptr<MemoryObjectStore> objectStore)
{
    ASSERT(objectStore);
    ASSERT(m_objectStores.contains(objectStore.get()));
    m_objectStores.remove(objectStore.get());

    auto addResult = m_deletedObjectStores.add(objectStore->info().name(), nullptr);
    if (addResult.isNewEntry)
        addResult.iterator->value = WTF::move(objectStore);
}

void MemoryBackingStoreTransaction::objectStoreCleared(MemoryObjectStore& objectStore, std::unique_ptr<KeyValueMap>&& keyValueMap)
{
    ASSERT(m_objectStores.contains(&objectStore));

    auto addResult = m_clearedKeyValueMaps.add(&objectStore, nullptr);

    // If this object store has already been cleared during this transaction, we don't need to remember this clearing.
    if (!addResult.isNewEntry)
        return;

    // If values had previously been changed during this transaction, fold those changes back into the
    // cleared key-value map now, so we have exactly the map that will need to be restored if the transaction is aborted.
    auto originalValues = m_originalValues.take(&objectStore);
    if (originalValues) {
        for (auto iterator : *originalValues)
            keyValueMap->set(iterator.key, iterator.value);
    }

    addResult.iterator->value = WTF::move(keyValueMap);
}

void MemoryBackingStoreTransaction::recordValueChanged(MemoryObjectStore& objectStore, const IDBKeyData& key)
{
    ASSERT(m_objectStores.contains(&objectStore));

    if (m_isAborting)
        return;

    // If this object store had been cleared during the transaction, no point in recording this
    // individual key/value change as its entire key/value map will be restored upon abort.
    if (m_clearedKeyValueMaps.contains(&objectStore))
        return;

    auto originalAddResult = m_originalValues.add(&objectStore, nullptr);
    if (originalAddResult.isNewEntry)
        originalAddResult.iterator->value = std::make_unique<KeyValueMap>();

    auto* map = originalAddResult.iterator->value.get();

    auto addResult = map->add(key, ThreadSafeDataBuffer());
    if (!addResult.isNewEntry)
        return;

    addResult.iterator->value = objectStore.valueForKeyRange(IDBKeyRangeData(key));
}

void MemoryBackingStoreTransaction::abort()
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::abort()");

    TemporaryChange<bool> change(m_isAborting, true);

    // This loop moves the underlying unique_ptrs from out of the m_deleteObjectStores map,
    // but the entries in the map still remain.
    for (auto& objectStore : m_deletedObjectStores.values()) {
        MemoryObjectStore* rawObjectStore = objectStore.get();
        m_backingStore.restoreObjectStoreForVersionChangeAbort(WTF::move(objectStore));

        ASSERT(!m_objectStores.contains(rawObjectStore));
        m_objectStores.add(rawObjectStore);
    }

    // This clears the entries from the map.
    m_deletedObjectStores.clear();

    if (m_originalDatabaseInfo) {
        ASSERT(m_info.mode() == IndexedDB::TransactionMode::VersionChange);
        m_backingStore.setDatabaseInfo(*m_originalDatabaseInfo);
    }

    for (auto objectStore : m_objectStores) {
        ASSERT(m_originalKeyGenerators.contains(objectStore));
        objectStore->setKeyGeneratorValue(m_originalKeyGenerators.get(objectStore));

        auto clearedKeyValueMap = m_clearedKeyValueMaps.take(objectStore);
        if (clearedKeyValueMap) {
            objectStore->replaceKeyValueStore(WTF::move(clearedKeyValueMap));
            continue;
        }

        auto keyValueMap = m_originalValues.take(objectStore);
        if (!keyValueMap)
            continue;

        for (auto entry : *keyValueMap) {
            objectStore->deleteRecord(entry.key);
            objectStore->setKeyValue(entry.key, entry.value);
        }
    }

    finish();

    for (auto objectStore : m_versionChangeAddedObjectStores)
        m_backingStore.removeObjectStoreForVersionChangeAbort(*objectStore);
}

void MemoryBackingStoreTransaction::commit()
{
    LOG(IndexedDB, "MemoryBackingStoreTransaction::commit()");

    finish();
}

void MemoryBackingStoreTransaction::finish()
{
    m_inProgress = false;

    if (!isWriting())
        return;

    for (auto& objectStore : m_objectStores)
        objectStore->writeTransactionFinished(*this);
    for (auto& objectStore : m_deletedObjectStores.values())
        objectStore->writeTransactionFinished(*this);
}

} // namespace IDBServer
} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)