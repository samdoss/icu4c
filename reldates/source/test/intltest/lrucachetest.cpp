#include "cstring.h"
#include "intltest.h"
#include "unicode/lrucache.h"
#include "umutex.h"

static UMutex gMutex = U_MUTEX_INITIALIZER;

class CopyOnWriteForTesting : public UObject {
public:
    CopyOnWriteForTesting() : localeNamePtr(), formatStrPtr(), length(0) {
    }
    UObject *clone() const {
        return new CopyOnWriteForTesting(*this);
    }
    virtual ~CopyOnWriteForTesting() {
    }
    SharedPtr<UnicodeString> localeNamePtr;
    SharedPtr<UnicodeString> formatStrPtr;
    int32_t length;
private:
    CopyOnWriteForTesting(const CopyOnWriteForTesting &other) :
        localeNamePtr(other.localeNamePtr),
        formatStrPtr(other.formatStrPtr),
        length(other.length) {
    }
    CopyOnWriteForTesting &operator=(const CopyOnWriteForTesting &rhs);
};

class LRUCacheForTesting : public LRUCache {
public:
    LRUCacheForTesting(
        int32_t maxSize, UMutex *mutex,
        const UnicodeString &dfs, UErrorCode &status);
    virtual ~LRUCacheForTesting() {
    }
protected:
    virtual UObject *create(const char *localeId, UErrorCode &status);
private:
    SharedPtr<UnicodeString> defaultFormatStr;
};

LRUCacheForTesting::LRUCacheForTesting(
        int32_t maxSize, UMutex *mutex,
        const UnicodeString &dfs, UErrorCode &status) :
    LRUCache(maxSize, mutex, status), defaultFormatStr() {
    if (U_FAILURE(status)) {
        return;
    }
    defaultFormatStr.adoptInstead(new UnicodeString(dfs));
}

UObject *LRUCacheForTesting::create(const char *localeId, UErrorCode &status) {
    if (uprv_strcmp(localeId, "error") == 0) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return NULL;
    }
    CopyOnWriteForTesting *result = new CopyOnWriteForTesting;
    result->localeNamePtr.adoptInstead(new UnicodeString(localeId));
    result->formatStrPtr = defaultFormatStr;
    result->length = 5;
    return result;
}

class LRUCacheTest : public IntlTest {
public:
    LRUCacheTest() {
    }
    void runIndexedTest(int32_t index, UBool exec, const char *&name, char *par=0);
private:
    void TestSharedPointer();
    void TestLRUCache();
    void TestLRUCacheError();
    void verifySharedPointer(
            const SharedPtr<CopyOnWriteForTesting>& ptr,
            const UnicodeString& name,
            const UnicodeString& format);
    void verifyString(
            const UnicodeString &expected, const UnicodeString &actual);
    void verifyPtr(const void *expected, const void *actual);
    void verifyReferences(
            const SharedPtr<CopyOnWriteForTesting>& ptr,
            int32_t count, int32_t nameCount, int32_t formatCount);
};

void LRUCacheTest::runIndexedTest(int32_t index, UBool exec, const char* &name, char* /*par*/) {
  TESTCASE_AUTO_BEGIN;
  TESTCASE_AUTO(TestSharedPointer);
  TESTCASE_AUTO(TestLRUCache);
  TESTCASE_AUTO(TestLRUCacheError);
  TESTCASE_AUTO_END;
}

void LRUCacheTest::TestSharedPointer() {
    UErrorCode status = U_ZERO_ERROR;
    LRUCacheForTesting cache(3, &gMutex, "little", status);
    SharedPtr<CopyOnWriteForTesting> ptr;
    cache.get("boo", ptr, status);
    verifySharedPointer(ptr, "boo", "little");
    SharedPtr<CopyOnWriteForTesting> ptrCopy = ptr;
    verifyPtr(ptr.readOnly(), ptrCopy.readOnly());
    {
        SharedPtr<CopyOnWriteForTesting> ptrCopy2(ptrCopy);
        verifyReferences(ptr, 4, 1, 2);
    }
    
    // Test identity assignment
    ptr = ptr;

    verifyReferences(ptr, 3, 1, 2);
    verifyReferences(ptrCopy, 3, 1, 2);
    *ptrCopy.readWrite()->localeNamePtr.readWrite() = UnicodeString("hi there");
    *ptrCopy.readWrite()->formatStrPtr.readWrite() = UnicodeString("see you");
    verifyReferences(ptr, 2, 1, 2);
    verifyReferences(ptrCopy, 1, 1, 1);
    verifySharedPointer(ptr, "boo", "little");
    verifySharedPointer(ptrCopy, "hi there", "see you");
}

void LRUCacheTest::TestLRUCache() {
    UErrorCode status = U_ZERO_ERROR;
    LRUCacheForTesting cache(3, &gMutex, "little", status);
    SharedPtr<CopyOnWriteForTesting> ptr1;
    SharedPtr<CopyOnWriteForTesting> ptr2;
    SharedPtr<CopyOnWriteForTesting> ptr3;
    SharedPtr<CopyOnWriteForTesting> ptr4;
    SharedPtr<CopyOnWriteForTesting> ptr5;
    cache.get("foo", ptr1, status);
    cache.get("bar", ptr2, status);
    cache.get("baz", ptr3, status);
    verifySharedPointer(ptr1, "foo", "little");
    verifySharedPointer(ptr2, "bar", "little");
    verifySharedPointer(ptr3, "baz", "little");

    // Cache holds a reference to returned data which explains the 2s
    // Note the '4'. each cached data has a reference to "little" and the
    // cache itself also has a reference to "little"
    verifyReferences(ptr1, 2, 1, 4);
    verifyReferences(ptr2, 2, 1, 4);
    verifyReferences(ptr3, 2, 1, 4);
    
    // (Most recent) "baz", "bar", "foo" (Least Recent) 
    // Cache is now full but thanks to shared pointers we can still evict.
    cache.get("full", ptr4, status);
    verifySharedPointer(ptr4, "full", "little");

    verifyReferences(ptr4, 2, 1, 5);

    // (Most Recent) "full" "baz", "bar" (Least Recent)
    cache.get("baz", ptr5, status);
    verifySharedPointer(ptr5, "baz", "little");
    // ptr5, ptr3, and cache have baz data
    verifyReferences(ptr5, 3, 1, 5);

    // This should delete foo data since it got evicted from cache.
    ptr1.clear();
    // Reference count for little drops to 4 because foo data was deleted.
    verifyReferences(ptr5, 3, 1, 4);

    // (Most Recent) "baz" "full" "bar" (Least Recent)
    cache.get("baz", ptr5, status);
    verifySharedPointer(ptr5, "baz", "little");
    verifyReferences(ptr5, 3, 1, 4);

    // (Most Recent) "baz", "full", "bar" (Least Recent)
    // ptr3, ptr5 -> "baz" ptr4 -> "full" ptr2 -> "bar"
    if (!cache.contains("baz") || !cache.contains("full") || !cache.contains("bar") || cache.contains("foo")) {
        errln("Unexpected keys in cache.");
    }
    cache.get("new1", ptr5, status);
    verifySharedPointer(ptr5, "new1", "little");
    verifyReferences(ptr5, 2, 1, 5);

    // Since bar was evicted, clearing its pointer should delete its data.
    // Notice that the reference count to 'little' dropped from 5 to 4.
    ptr2.clear();
    verifyReferences(ptr5, 2, 1, 4);
    if (cache.contains("bar") || !cache.contains("full")) {
        errln("Unexpected 'bar' in cache.");
    }

    // (Most Recent) "new1", "baz", "full" (Least Recent)
    // ptr3 -> "baz" ptr4 -> "full" ptr5 -> "new1"
    cache.get("new2", ptr5, status);
    verifySharedPointer(ptr5, "new2", "little");
    verifyReferences(ptr5, 2, 1, 5);

    // since "full" was evicted, clearing its pointer should delete its data.
    ptr4.clear();
    verifyReferences(ptr5, 2, 1, 4);
    if (cache.contains("full") || !cache.contains("baz")) {
        errln("Unexpected 'full' in cache.");
    }

    // (Most Recent) "new2", "new1", "baz" (Least Recent)
    // ptr3 -> "baz" ptr5 -> "new2"
    cache.get("new3", ptr5, status);
    verifySharedPointer(ptr5, "new3", "little");
    verifyReferences(ptr5, 2, 1, 5);

    // since "baz" was evicted, clearing its pointer should delete its data.
    ptr3.clear();
    verifyReferences(ptr5, 2, 1, 4);
    if (cache.contains("baz") || !cache.contains("new3")) {
        errln("Unexpected 'baz' in cache.");
    }
}

void LRUCacheTest::TestLRUCacheError() {
    UErrorCode status = U_ZERO_ERROR;
    LRUCacheForTesting cache(3, &gMutex, "little", status);
    SharedPtr<CopyOnWriteForTesting> ptr1;
    cache.get("error", ptr1, status);
    if (status != U_ILLEGAL_ARGUMENT_ERROR) {
        errln("Expected an error.");
    }
}

void LRUCacheTest::verifySharedPointer(
        const SharedPtr<CopyOnWriteForTesting>& ptr,
        const UnicodeString& name,
        const UnicodeString& format) {
    const UnicodeString *strPtr = ptr->localeNamePtr.readOnly();
    verifyString(name, *strPtr);
    strPtr = ptr->formatStrPtr.readOnly();
    verifyString(format, *strPtr);
}

void LRUCacheTest::verifyString(const UnicodeString &expected, const UnicodeString &actual) {
    if (expected != actual) {
        errln(UnicodeString("Expected '") + expected + "', got '"+ actual+"'");
    }
}

void LRUCacheTest::verifyPtr(const void *expected, const void *actual) {
    if (expected != actual) {
       errln("Pointer mismatch.");
    }
}

void LRUCacheTest::verifyReferences(const SharedPtr<CopyOnWriteForTesting>& ptr, int32_t count, int32_t nameCount, int32_t formatCount) {
    int32_t actual = ptr.count();
    if (count != actual) {
        errln("Main reference count wrong: Expected %d, got %d", count, actual);
    }
    actual = ptr->localeNamePtr.count();
    if (nameCount != actual) {
        errln("name reference count wrong: Expected %d, got %d", nameCount, actual);
    }
    actual = ptr->formatStrPtr.count();
    if (formatCount != actual) {
        errln("format reference count wrong: Expected %d, got %d", formatCount, actual);
    }
}

extern IntlTest *createLRUCacheTest() {
    return new LRUCacheTest();
}
