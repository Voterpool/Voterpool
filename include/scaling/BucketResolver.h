#pragma once

// Логические бакеты (docs/16 §3.2).
//
// Граница шарда зафиксирована в keyspace: каждая орг-принадлежащая запись
// хранится под префиксом "b{NNN}:", NNN = bucket(org_id). Функция размещения
// ЧИСТАЯ и ЗАМОРОЖЕНА тест-векторами (tests/fixtures/bucket_vectors.json):
// изменение алгоритма или K допустимо только новой миграцией схемы.
//
// Системная плоскость (meta:*, auth:{hash}, agent:{id}, proposal_lookup:{pid})
// префикса НЕ имеет и через resolver не проходит.

#include <cstdint>
#include <string>

namespace voterpool::scaling {

// Число логических бакетов; config cluster.buckets читается только для
// валидации равенства.
inline constexpr int kBucketCount = 256;

// FNV-1a 64: детерминирован кросс-платформенно, каноничный порядок байт.
inline std::uint64_t fnv1a64(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;  // offset basis
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;  // prime
    }
    return h;
}

// Размещение организации в бакете. Чистая функция, ноль состояния (SH-5).
inline int bucketFor(const std::string& orgId) {
    return static_cast<int>(fnv1a64(orgId) % static_cast<std::uint64_t>(kBucketCount));
}

// Префикс бакета: "b000:" .. "b255:" — ровно три цифры.
inline std::string bucketPrefix(int bucket) {
    std::string p = "b000:";
    int b = bucket;
    p[1] = static_cast<char>('0' + (b / 100) % 10);
    p[2] = static_cast<char>('0' + (b / 10) % 10);
    p[3] = static_cast<char>('0' + b % 10);
    return p;
}

// Склейка префикса бакета с ключом орг-плоскости.
inline std::string scoped(int bucket, std::string key) {
    return bucketPrefix(bucket) + std::move(key);
}

}  // namespace voterpool::scaling
