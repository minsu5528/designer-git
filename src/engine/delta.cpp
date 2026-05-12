#include "delta.h"
#include <fstream>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace fs = std::filesystem;

// ── CDC 상수 ──────────────────────────────────────────────────
static const size_t   IO_BUF_SIZE  = 4  * 1024 * 1024; // 4MB I/O 버퍼 (디스크 읽기 단위)
static const size_t   MIN_CHUNK    = 4  * 1024;          // 최소 블록 4KB  (과분할 방지)
static const size_t   MAX_CHUNK    = 64 * 1024;          // 최대 블록 64KB (과대 청크 방지)
static const size_t   WINDOW_SIZE  = 48;                 // 슬라이딩 윈도우 (경계 안정성 ↔ 비용 균형)
static const uint64_t CDC_BASE     = 257ULL;             // 롤링 해시 기저 (CDC 경계 탐지용)
//static const uint64_t CDC_MOD      = 1000000007ULL;      // 롤링 해시 모듈러
static const uint64_t CDC_MASK     = 16383ULL;           // (2^14-1): P(hit)≈1/16384 → 평균 ~16KB 블록
static const uint64_t HASH_BASE    = 131ULL;             // 블록 내용 해시 기저 (이중 해시 충돌 완화용)
static const uint64_t HASH_MOD     = 998244353ULL;       // 블록 내용 해시 모듈러 (CDC_MOD와 다른 소수)

// ── Delta 파일 포맷 상수 ─────────────────────────────────────
// 손상/잘못된 파일 조기 감지를 위한 헤더
static const char     DELTA_MAGIC[8] = {'D','G','D','E','L','T','A','\0'};
static const uint32_t DELTA_VERSION  = 1;
static const uint8_t  CMD_INSERT     = 0;
static const uint8_t  CMD_COPY       = 1;

// On-disk 명령 크기 상한 (악성/손상 delta 방어)
// CDC 설계상 블록은 MAX_CHUNK를 넘지 않음 → 2배 여유
static const uint64_t MAX_VALID_LEN  = static_cast<uint64_t>(MAX_CHUNK) * 2;

// ── BASE^WINDOW_SIZE % CDC_MOD 사전 계산 ─────────────────────
static uint64_t compute_pow_base_win() {
    uint64_t r = 1;
    for (size_t i = 0; i < WINDOW_SIZE; ++i)
        r = r * CDC_BASE; // 오버플로우 허용, % 없음
    return r;
}
static const uint64_t POW_BASE_WIN = compute_pow_base_win();

// ── 이중 해시 구조체 ──────────────────────────────────────────
// h1, h2 모두 일치해야 동일 블록으로 판단
// 충돌 확률 ≈ 1/(10^9 × 10^9) = 10^-18 (충돌 완화, 완전 방어 아님)
// 최종 무결성은 SHA256 검증(checkout 시)으로 보장
struct BlockHash {
    uint64_t h1;
    uint64_t h2;
    bool operator==(const BlockHash& o) const {
        return h1 == o.h1 && h2 == o.h2;
    }
};

struct BlockHasher {
    size_t operator()(const BlockHash& bh) const {
        // boost::hash_combine 방식
        size_t seed = std::hash<uint64_t>()(bh.h1);
        seed ^= std::hash<uint64_t>()(bh.h2)
                + 0x9e3779b9ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// 블록 전체 내용에 대한 이중 해시 계산
static BlockHash compute_block_hash(const std::vector<uint8_t>& data) {
    uint64_t h1 = 0, h2 = 0;
    for (uint8_t b : data) {
        h1 = h1 * CDC_BASE  + b;          // uint64_t 오버플로우 모듈러
        h2 = (h2 * HASH_BASE + b) % HASH_MOD;
    }
    return { h1, h2 };
}

// ── CDC 상태 ──────────────────────────────────────────────────
struct CdcState {
    uint8_t  ring[WINDOW_SIZE] = {};
    int      ring_head  = 0;
    size_t   ring_count = 0;
    uint64_t rolling    = 0;

    void reset() {
        std::fill(std::begin(ring), std::end(ring), uint8_t(0));
        ring_head  = 0;
        ring_count = 0;
        rolling    = 0;
    }

    // 바이트 하나를 슬라이딩 윈도우에 밀어 넣고 rolling hash 갱신
    void push(uint8_t b) {
        uint8_t removed = (ring_count == WINDOW_SIZE) ? ring[ring_head] : 0;
        if (ring_count < WINDOW_SIZE) ++ring_count;

        ring[ring_head] = b;
        ring_head = (ring_head + 1 < static_cast<int>(WINDOW_SIZE))
                    ? ring_head + 1 : 0; // 분기문이 % 보다 빠름

        rolling = rolling * CDC_BASE + b - (removed * POW_BASE_WIN);
    }

    bool is_boundary(size_t chunk_size) const {
        return chunk_size >= MIN_CHUNK &&
               ((rolling & CDC_MASK) == 0 || chunk_size >= MAX_CHUNK);
    }
};

// ── 파일 A CDC 분할 → 해시맵 구성 ────────────────────────────
// 블록 데이터는 저장하지 않고 해시 + 오프셋 + 길이만 저장
// → 10GB 파일 기준 해시맵 ~20MB (데이터 저장 시 10GB+)
static int build_hash_map(
    const char* path,
    std::unordered_map<BlockHash, std::pair<uint64_t, uint64_t>, BlockHasher>& map)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return ERR_OPEN_SRC;

    // 예상 청크 수 기반 rehash 방지 (평균 ~16KB 가정)
    std::error_code ec;
    auto file_sz = fs::file_size(path, ec);
    if (!ec && file_sz > 0) {
        size_t estimated = static_cast<size_t>(
            std::max<uint64_t>(1, file_sz / 16384));
        map.reserve(estimated * 2);
    }

    // new[] 대신 vector (RAII, 예외 안전)
    std::vector<uint8_t> io_buf(IO_BUF_SIZE);
    std::vector<uint8_t> current;
    current.reserve(MAX_CHUNK);

    CdcState cdc;
    uint64_t chunk_offset = 0;

    auto finalize = [&]() {
        if (current.empty()) return;
        BlockHash bh = compute_block_hash(current);
        map[bh] = { chunk_offset, static_cast<uint64_t>(current.size()) };
        chunk_offset += current.size();
        current.clear();
        cdc.reset();
    };

    while (file.read(reinterpret_cast<char*>(io_buf.data()), IO_BUF_SIZE)
           || file.gcount() > 0)
    {
        size_t n = static_cast<size_t>(file.gcount());

        // Block Copy 최적화: push_back 100억 번 대신
        // 인덱스만 추적하다가 블록 확정 시 insert()로 일괄 복사
        size_t chunk_start_idx = 0;
        for (size_t i = 0; i < n; ++i) {
            cdc.push(io_buf[i]);

            size_t temp_size = current.size() + (i - chunk_start_idx + 1);
            if (cdc.is_boundary(temp_size)) {
                current.insert(current.end(),
                               io_buf.data() + chunk_start_idx,
                               io_buf.data() + i + 1);
                finalize();
                chunk_start_idx = i + 1;
            }
        }
        // 이번 버퍼에서 확정 안 된 찌꺼기 일괄 복사
        if (chunk_start_idx < n) {
            current.insert(current.end(),
                           io_buf.data() + chunk_start_idx,
                           io_buf.data() + n);
        }
    }
    finalize();  // EOF 자투리 블록

    return DELTA_OK;
}

// ── 파일 B CDC 분할 → delta 즉시 출력 ────────────────────────
static int process_file_b(
    const char* path_b,
    const std::unordered_map<BlockHash, std::pair<uint64_t, uint64_t>, BlockHasher>& map,
    std::ofstream& out)
{
    std::ifstream file(path_b, std::ios::binary);
    if (!file) return ERR_OPEN_SRC;

    std::vector<uint8_t> io_buf(IO_BUF_SIZE);
    std::vector<uint8_t> current;
    current.reserve(MAX_CHUNK);

    CdcState cdc;

    auto emit = [&]() -> int {
        if (current.empty()) return DELTA_OK;
        BlockHash bh = compute_block_hash(current);
        auto it = map.find(bh);

        // COPY: 이중 해시 일치 + 길이 일치 → 같은 블록으로 판단
        // (길이 불일치 시 해시 충돌로 간주 → INSERT 처리)
        if (it != map.end() &&
            it->second.second == static_cast<uint64_t>(current.size()))
        {
            uint8_t  cmd     = CMD_COPY;
            uint64_t src_off = it->second.first;
            uint64_t src_len = it->second.second;
            if (!out.write(reinterpret_cast<const char*>(&cmd),     sizeof(cmd)))     return ERR_OUT_WRITE;
            if (!out.write(reinterpret_cast<const char*>(&src_off), sizeof(src_off))) return ERR_OUT_WRITE;
            if (!out.write(reinterpret_cast<const char*>(&src_len), sizeof(src_len))) return ERR_OUT_WRITE;
        } else {
            uint8_t  cmd = CMD_INSERT;
            uint64_t len = static_cast<uint64_t>(current.size());
            if (!out.write(reinterpret_cast<const char*>(&cmd), sizeof(cmd))) return ERR_OUT_WRITE;
            if (!out.write(reinterpret_cast<const char*>(&len), sizeof(len))) return ERR_OUT_WRITE;
            if (!out.write(reinterpret_cast<const char*>(current.data()),
                           static_cast<std::streamsize>(len)))                return ERR_OUT_WRITE;
        }

        current.clear();
        cdc.reset();
        return DELTA_OK;
    };

    while (file.read(reinterpret_cast<char*>(io_buf.data()), IO_BUF_SIZE)
           || file.gcount() > 0)
    {
        size_t n = static_cast<size_t>(file.gcount());

        size_t chunk_start_idx = 0;
        for (size_t i = 0; i < n; ++i) {
            cdc.push(io_buf[i]);

            size_t temp_size = current.size() + (i - chunk_start_idx + 1);
            if (cdc.is_boundary(temp_size)) {
                current.insert(current.end(),
                               io_buf.data() + chunk_start_idx,
                               io_buf.data() + i + 1);
                int ret = emit();
                if (ret != DELTA_OK) return ret;
                chunk_start_idx = i + 1;
            }
        }
        if (chunk_start_idx < n) {
            current.insert(current.end(),
                           io_buf.data() + chunk_start_idx,
                           io_buf.data() + n);
        }
    }
    return emit();  // EOF 자투리 블록
}

// ── delta_create ──────────────────────────────────────────────
int delta_create(const char* path_a,
                 const char* path_b,
                 const char* out_delta) {

    // 1단계: 파일 A CDC 분할 → 해시맵 구성 (싱글스레드, 순차)
    std::unordered_map<BlockHash, std::pair<uint64_t, uint64_t>, BlockHasher> hash_map;
    int ret = build_hash_map(path_a, hash_map);
    if (ret != DELTA_OK) return ret;

    // 2단계: delta 출력 파일 열기 + 헤더 쓰기
    std::ofstream out(out_delta, std::ios::binary);
    if (!out) return ERR_OPEN_OUT;

    std::vector<char> write_buf(1024 * 1024);
    out.rdbuf()->pubsetbuf(write_buf.data(), write_buf.size());

    if (!out.write(DELTA_MAGIC, sizeof(DELTA_MAGIC)))     return ERR_OUT_WRITE;
    if (!out.write(reinterpret_cast<const char*>(&DELTA_VERSION),
                   sizeof(DELTA_VERSION)))                 return ERR_OUT_WRITE;

    // 3단계: Producer-Consumer로 파일 B 처리
    // Double Buffer: Producer가 buf[0] 채우는 동안 Consumer가 buf[1] 처리
    constexpr int NUM_BUFS = 2;
    std::vector<std::vector<uint8_t>> bufs(NUM_BUFS,
                                           std::vector<uint8_t>(IO_BUF_SIZE));
    std::vector<size_t> buf_sizes(NUM_BUFS, 0);

    std::mutex              mtx;
    std::condition_variable cv_full;   // Consumer 깨우기
    std::condition_variable cv_empty;  // Producer 깨우기

    // 0: 비어있음, 1: 가득 참
    std::vector<int> buf_state(NUM_BUFS, 0);
    std::atomic<bool> producer_done{false};
    std::atomic<int>  consumer_error{DELTA_OK};

    // ── Producer ─────────────────────────────────────────────
    auto producer = [&]() {
    std::ifstream file(path_b, std::ios::binary);
    if (!file) {
        std::unique_lock<std::mutex> lk(mtx);
        producer_done = true;
        cv_full.notify_all();
        return;
    }

    int idx = 0;
    while (true) {
        // 슬롯이 빌 때까지 먼저 대기
        {
            std::unique_lock<std::mutex> lk(mtx);
            cv_empty.wait(lk, [&]{ return buf_state[idx] == 0; });
        }

        // 슬롯 비어있음 확인 후 디스크 읽기
        file.read(reinterpret_cast<char*>(bufs[idx].data()), IO_BUF_SIZE);
        size_t n = static_cast<size_t>(file.gcount());

        {
            std::unique_lock<std::mutex> lk(mtx);
            buf_sizes[idx] = n;
            buf_state[idx] = 1;
            cv_full.notify_one();
        }

        if (n == 0) break;
        idx = (idx + 1) % NUM_BUFS;
    }

    std::unique_lock<std::mutex> lk(mtx);
    producer_done = true;
    cv_full.notify_all();
};

    // ── Consumer ─────────────────────────────────────────────
    auto consumer = [&]() {
        std::vector<uint8_t> current;
        current.reserve(MAX_CHUNK);
        CdcState cdc;

        auto emit = [&]() -> int {
            if (current.empty()) return DELTA_OK;
            BlockHash bh = compute_block_hash(current);
            auto it = hash_map.find(bh);

            if (it != hash_map.end() &&
                it->second.second == static_cast<uint64_t>(current.size()))
            {
                uint8_t  cmd     = CMD_COPY;
                uint64_t src_off = it->second.first;
                uint64_t src_len = it->second.second;
                if (!out.write(reinterpret_cast<const char*>(&cmd),     sizeof(cmd)))     return ERR_OUT_WRITE;
                if (!out.write(reinterpret_cast<const char*>(&src_off), sizeof(src_off))) return ERR_OUT_WRITE;
                if (!out.write(reinterpret_cast<const char*>(&src_len), sizeof(src_len))) return ERR_OUT_WRITE;
            } else {
                uint8_t  cmd = CMD_INSERT;
                uint64_t len = static_cast<uint64_t>(current.size());
                if (!out.write(reinterpret_cast<const char*>(&cmd), sizeof(cmd))) return ERR_OUT_WRITE;
                if (!out.write(reinterpret_cast<const char*>(&len), sizeof(len))) return ERR_OUT_WRITE;
                if (!out.write(reinterpret_cast<const char*>(current.data()),
                            static_cast<std::streamsize>(len)))                return ERR_OUT_WRITE;
            }
            current.clear();
            cdc.reset();
            return DELTA_OK;
        };

        int idx = 0;
        while (true) {
            // 버퍼가 찰 때까지 대기
            std::unique_lock<std::mutex> lk(mtx);
            cv_full.wait(lk, [&]{
                return buf_state[idx] == 1 || producer_done.load();
            });

            if (buf_state[idx] != 1) break;

            size_t n = buf_sizes[idx];
            lk.unlock();

            if (n == 0) {
                // empty 신호 보내고 종료
                std::unique_lock<std::mutex> lk2(mtx);
                buf_state[idx] = 0;
                cv_empty.notify_one();
                break;
            }

            // bufs[idx] 처리 (락 없이)
            size_t chunk_start_idx = 0;
            for (size_t i = 0; i < n; ++i) {
                cdc.push(bufs[idx][i]);
                size_t temp_size = current.size() + (i - chunk_start_idx + 1);
                if (cdc.is_boundary(temp_size)) {
                    current.insert(current.end(),
                                bufs[idx].data() + chunk_start_idx,
                                bufs[idx].data() + i + 1);
                    int r = emit();
                    if (r != DELTA_OK) { consumer_error = r; return; }
                    chunk_start_idx = i + 1;
                }
            }
            if (chunk_start_idx < n) {
                current.insert(current.end(),
                            bufs[idx].data() + chunk_start_idx,
                            bufs[idx].data() + n);
            }

            // 처리 완료 후 empty 신호
            {
                std::unique_lock<std::mutex> lk2(mtx);
                buf_state[idx] = 0;
                cv_empty.notify_one();
            }
            idx = (idx + 1) % NUM_BUFS;
        }

        int r = emit();
        if (r != DELTA_OK) consumer_error = r;
    };

    // ── 스레드 실행 ───────────────────────────────────────────
    std::thread t_producer(producer);
    std::thread t_consumer(consumer);
    t_producer.join();
    t_consumer.join();

    return consumer_error.load();
}

// ── delta_apply ───────────────────────────────────────────────
int delta_apply(const char* path_base,
                const char* path_delta,
                const char* out_file) {

    std::ifstream base(path_base, std::ios::binary);
    if (!base) return ERR_OPEN_SRC;

    std::ifstream delta_f(path_delta, std::ios::binary);
    if (!delta_f) return ERR_OPEN_DELTA;

    std::ofstream out(out_file, std::ios::binary);
    if (!out) return ERR_OPEN_OUT;

    // 헤더 검증: magic + version
    char     magic[8]   = {};
    uint32_t version    = 0;
    if (!delta_f.read(magic, sizeof(magic)))         return ERR_TRUNCATED_DELTA;
    if (std::memcmp(magic, DELTA_MAGIC, 8) != 0)     return ERR_BAD_MAGIC;
    if (!delta_f.read(reinterpret_cast<char*>(&version),
                      sizeof(version)))               return ERR_TRUNCATED_DELTA;
    if (version != DELTA_VERSION)                    return ERR_BAD_VERSION;

    // 가변 크기 블록용 읽기 버퍼 (재사용)
    std::vector<uint8_t> buf;
    buf.reserve(MAX_CHUNK);

    uint8_t cmd;
    while (delta_f.read(reinterpret_cast<char*>(&cmd), sizeof(cmd))) {

        if (cmd == CMD_COPY) {
            // COPY: on-disk 포맷은 uint64_t (이식성 보장)
            uint64_t src_off = 0, src_len = 0;
            if (!delta_f.read(reinterpret_cast<char*>(&src_off), sizeof(src_off))) return ERR_TRUNCATED_DELTA;
            if (!delta_f.read(reinterpret_cast<char*>(&src_len), sizeof(src_len))) return ERR_TRUNCATED_DELTA;

            // 길이 상한 검증 (악성/손상 delta 방어)
            if (src_len == 0 || src_len > MAX_VALID_LEN) return ERR_INVALID_LEN;

            buf.resize(static_cast<size_t>(src_len));
            base.seekg(static_cast<std::streamoff>(src_off));
            if (!base) return ERR_BASE_READ;

            base.read(reinterpret_cast<char*>(buf.data()),
                      static_cast<std::streamsize>(src_len));
            // 요청한 바이트 수만큼 정확히 읽혔는지 검증
            if (static_cast<uint64_t>(base.gcount()) != src_len) return ERR_BASE_READ;

            if (!out.write(reinterpret_cast<const char*>(buf.data()),
                           static_cast<std::streamsize>(src_len))) return ERR_OUT_WRITE;

        } else if (cmd == CMD_INSERT) {
            uint64_t len = 0;
            if (!delta_f.read(reinterpret_cast<char*>(&len), sizeof(len))) return ERR_TRUNCATED_DELTA;

            // 길이 상한 검증
            if (len == 0 || len > MAX_VALID_LEN) return ERR_INVALID_LEN;

            buf.resize(static_cast<size_t>(len));
            delta_f.read(reinterpret_cast<char*>(buf.data()),
                         static_cast<std::streamsize>(len));
            // 요청한 바이트 수만큼 정확히 읽혔는지 검증
            if (static_cast<uint64_t>(delta_f.gcount()) != len) return ERR_TRUNCATED_DELTA;

            if (!out.write(reinterpret_cast<const char*>(buf.data()),
                           static_cast<std::streamsize>(len))) return ERR_OUT_WRITE;

        } else {
            // 알 수 없는 cmd → 손상된 delta 파일로 판단, 즉시 중단
            return ERR_BAD_CMD;
        }
    }

    return DELTA_OK;
}
