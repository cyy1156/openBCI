# 双缓冲区改造方案（精简版）

> 只保留你当前必须的内容：  
> 1) 实时环形缓冲（算法/绘图都按连续 128 点取）  
> 2) 日志队列缓冲（CSV 全量写盘，不拖采集）

---

## 1. 结论（先看这个）

- **不用重写全部**：`DataBuffer` 可在现有基础上增强。  
- **要新增一个日志队列**：不要把“全量记录”依赖在会覆盖旧数据的环形缓冲上。  
- **算法与绘图都按同一规则取数**：连续 128 点，不够就等，下次继续。

---

## 2. 你真正需要保留的接口

### DataBuffer（必须）

1. `appendRawvalue(const RawSample&)`  
2. `tryDequeueRawChunkForAlgo(128, outChunk)`  
3. `tryDequeueRawChunkForPlot(128, outChunk)`  
4. `resetAlgoCursor()` / `resetPlotCursor()`  
5. `clear()`

### LogQueueBuffer（必须）

1. `push(const LogItem&)`  
2. `drainBatch(int maxItems)`  
3. `droppedCount()`

> 其他示例函数（如 `recentRaw`、`windowBySeq`、大段演示写法）先都可以不做。

---

## 3. DataBuffer 最小代码骨架（仅 RAW + 双游标）

```cpp
// databuffer.h（最小必要）
#ifndef DATABUFFER_H
#define DATABUFFER_H

#include <QVector>
#include <QMutex>
#include <QtGlobal>

struct RawSample {
    double raw = 0.0;      // uV
    qint64 tsMs = 0;       // 时间戳
    quint64 raw_count = 0; // 连续序号
};

class DataBuffer
{
public:
    explicit DataBuffer(int rawCapacity = 128 * 40);

    void appendRawvalue(const RawSample &s);

    bool tryDequeueRawChunkForAlgo(int chunkSize, QVector<RawSample> &outChunk);
    bool tryDequeueRawChunkForPlot(int chunkSize, QVector<RawSample> &outChunk);

    void resetAlgoCursor();
    void resetPlotCursor();
    void clear();

private:
    QVector<RawSample> rawOrderedUnsafe() const;
    static bool tryDequeueByCursor(const QVector<RawSample> &all,
                                   quint64 &cursorSeq,
                                   int chunkSize,
                                   QVector<RawSample> &outChunk);

    QVector<RawSample> m_raw;
    int m_rawCapacity = 0;
    int m_rawWrite = 0;
    bool m_rawFilled = false;

    quint64 m_algoReadSeq = 0;
    quint64 m_plotReadSeq = 0;
    mutable QMutex m_mutex;
};

#endif
```

```cpp
// databuffer.cpp（最小必要）
#include "databuffer.h"
#include <QMutexLocker>

DataBuffer::DataBuffer(int rawCapacity)
    : m_rawCapacity(rawCapacity > 0 ? rawCapacity : 1)
{
    m_raw.resize(m_rawCapacity);
}

void DataBuffer::appendRawvalue(const RawSample &s)
{
    QMutexLocker locker(&m_mutex);
    m_raw[m_rawWrite] = s;
    ++m_rawWrite;
    if (m_rawWrite >= m_rawCapacity) {
        m_rawWrite = 0;
        m_rawFilled = true;
    }
}

QVector<RawSample> DataBuffer::rawOrderedUnsafe() const
{
    QVector<RawSample> out;
    if (!m_rawFilled) {
        out.reserve(m_rawWrite);
        for (int i = 0; i < m_rawWrite; ++i) out.push_back(m_raw[i]);
        return out;
    }
    out.reserve(m_rawCapacity);
    for (int i = 0; i < m_rawCapacity; ++i) {
        const int idx = (m_rawWrite + i) % m_rawCapacity;
        out.push_back(m_raw[idx]);
    }
    return out;
}

bool DataBuffer::tryDequeueByCursor(const QVector<RawSample> &all,
                                    quint64 &cursorSeq,
                                    int chunkSize,
                                    QVector<RawSample> &outChunk)
{
    outChunk.clear();
    if (all.isEmpty() || chunkSize <= 0) return false;

    if (cursorSeq == 0) cursorSeq = all.first().raw_count;

    int begin = -1;
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].raw_count >= cursorSeq) { begin = i; break; }
    }
    if (begin < 0) return false;

    const int available = all.size() - begin;
    if (available < chunkSize) return false; // 不足 128，不消费

    outChunk = all.sliced(begin, chunkSize);
    cursorSeq = outChunk.last().raw_count + 1;
    return true;
}

bool DataBuffer::tryDequeueRawChunkForAlgo(int chunkSize, QVector<RawSample> &outChunk)
{
    QMutexLocker locker(&m_mutex);
    return tryDequeueByCursor(rawOrderedUnsafe(), m_algoReadSeq, chunkSize, outChunk);
}

bool DataBuffer::tryDequeueRawChunkForPlot(int chunkSize, QVector<RawSample> &outChunk)
{
    QMutexLocker locker(&m_mutex);
    return tryDequeueByCursor(rawOrderedUnsafe(), m_plotReadSeq, chunkSize, outChunk);
}

void DataBuffer::resetAlgoCursor()
{
    QMutexLocker locker(&m_mutex);
    m_algoReadSeq = 0;
}

void DataBuffer::resetPlotCursor()
{
    QMutexLocker locker(&m_mutex);
    m_plotReadSeq = 0;
}

void DataBuffer::clear()
{
    QMutexLocker locker(&m_mutex);
    m_rawWrite = 0;
    m_rawFilled = false;
    m_algoReadSeq = 0;
    m_plotReadSeq = 0;
}
```

---

## 4. LogQueueBuffer 最小代码骨架（给 CSV 全量写盘）

```cpp
// logqueuebuffer.h
#ifndef LOGQUEUEBUFFER_H
#define LOGQUEUEBUFFER_H

#include <QQueue>
#include <QMutex>
#include <QtGlobal>

struct LogItem {
    qint64 tsMs = 0;
    quint64 seq = 0;
    qint16 rawInt16 = 0;
    double rawUv = 0.0;
};

class LogQueueBuffer
{
public:
    explicit LogQueueBuffer(int capacity = 4096);
    bool push(const LogItem &item);              // 满队列返回 false
    QList<LogItem> drainBatch(int maxItems);     // writer 线程批量取
    int droppedCount() const;

private:
    int m_capacity = 0;
    mutable QMutex m_mutex;
    QQueue<LogItem> m_q;
    int m_dropped = 0;
};

#endif
```

```cpp
// logqueuebuffer.cpp
#include "logqueuebuffer.h"
#include <QMutexLocker>

LogQueueBuffer::LogQueueBuffer(int capacity)
    : m_capacity(capacity > 0 ? capacity : 1) {}

bool LogQueueBuffer::push(const LogItem &item)
{
    QMutexLocker locker(&m_mutex);
    if (m_q.size() >= m_capacity) {
        ++m_dropped;
        return false;
    }
    m_q.enqueue(item);
    return true;
}

QList<LogItem> LogQueueBuffer::drainBatch(int maxItems)
{
    QMutexLocker locker(&m_mutex);
    QList<LogItem> out;
    const int n = qMin(maxItems, m_q.size());
    out.reserve(n);
    for (int i = 0; i < n; ++i) out.push_back(m_q.dequeue());
    return out;
}

int LogQueueBuffer::droppedCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_dropped;
}
```

---

## 5. 算法与绘图的统一调用方式（连续 128 点）

```cpp
QVector<RawSample> chunk;
if (m_dataBuffer.tryDequeueRawChunkForAlgo(128, chunk)) {
    // 连续128点，做算法
}

QVector<RawSample> plotChunk;
if (m_dataBuffer.tryDequeueRawChunkForPlot(128, plotChunk)) {
    // 连续128点，做增量绘图
}
```

---

## 6. 你可以删除的内容（当前目标下）

- `recentRaw()`、`rawWindowBySeq()` 等扩展接口（先不做）
- `BigPackageSample` 相关缓存逻辑（若你暂时只跑 RAW）
- 旧 `LogWorker` 里未使用的空定时器回调和历史绘图占位代码

---

## 7. 参数建议（按你当前场景）

- `chunkSize = 128`
- `rawCapacity >= 128 * 40`（至少 5120 点，防止短时消费慢导致覆盖）
- `LogQueueBuffer capacity = 4096`（SSD + 100ms flush 可先这样）

---

这版就是“删繁就简”的最小落地方案。后续需要重叠窗/多特征再加扩展接口即可。

# 双缓冲区改造方案（基于 `core/databuffer.*` 与 `core/logworker.*`）

> 目标：你要“采集实时不丢、算法和绘图都从缓存拿、日志全量记录”。  
> 推荐：保留并增强 `DataBuffer` 作为**实时环形缓冲**；新增一个**日志队列缓冲**给写盘线程。  
> 不建议让 `LogWorker` 直接承担“全量实时写盘 + UI绘图 + 算法取窗”三件事。

---

## 一、先回答你问的：改现有代码还是重写？

### 结论
1. **`DataBuffer`：建议在原基础上增强（不用重写）**
   - 你现在环形写入逻辑已经可用
   - 需要补“按序号取窗口/最近N点”接口，方便算法线程稳定取数

2. **`LogWorker`：建议改造成“日志队列+批量写盘”或新建 `CsvLogWorker`**
   - 你当前 `LogWorker` 只是开关采集，尚未实现真正写盘队列
   - 全量日志要尽量不丢，应该用有界队列 + 批量写，不该复用可覆盖的环形缓冲

---

## 一点五、最小保留清单（哪些要留，哪些可删）

> 你说“在示例上保留有用代码，并告诉哪些可删除”，这里给你可直接执行的精简标准。

### A. `DataBuffer`（必须保留）

**必须保留：**
- `appendRawvalue(const RawSample&)`
- `rawOrderedUnsafe()`（内部重排）
- `tryDequeueRawChunkForAlgo(128, ...)`
- `tryDequeueRawChunkForPlot(128, ...)`
- `resetAlgoCursor()/resetPlotCursor()`
- `clear()`
- 成员：`m_raw`、`m_rawWrite`、`m_rawFilled`、`m_algoReadSeq`、`m_plotReadSeq`、`m_mutex`

**可选保留：**
- `window_RawSample()`（调试/一次性导出窗口时有用）
- `window_BigPackageSample()`（若你暂时不用大包可先不接）
- `recentRaw()`（你现在是“严格 128 连续分块”，可先不需要）
- `rawWindowBySeq()`（做重叠窗时有用）

**可删除（当前目标下）：**
- 与 `BigPackageSample` 相关的全部逻辑（如果你目前只做 RAW 算法与绘图）
- `latestRawSeq()`（若你不做监控/告警）

---

### B. `LogWorker`（建议替换为写盘 worker）

**必须保留（如果你要全量 CSV）：**
- `onUvSample(...)` 或 `appendRawSample(...)`（入队）
- `start()/stop()`
- `onFlushTick()`（每 100ms 批量写）
- 写盘对象：`QFile` + `QTextStream`
- 队列消费：`drainBatch(...)`

**可删除（你当前代码里的）：**
- 仅用于“开关采集但不写盘”的空壳逻辑
- `onTimerTick()` 空函数（如果不用于写盘）
- 与 UI 历史回放耦合的旧接口（后续再接）

---

### C. 新增 `LogQueueBuffer`（建议保留完整）

**必须保留：**
- `push(const LogItem&)`
- `drainBatch(int maxItems)`
- `droppedCount()`
- 成员：`QQueue<LogItem>`、`QMutex`、`capacity`

**可删除：**
- `QWaitCondition`（你如果不做阻塞等待模型）
- HEX 字段（如果你暂时只写数值 CSV）

---

### D. 你当前目标的“最小可运行组合”

如果你想先尽快跑通，保留以下 6 个接口就够了：

1. `DataBuffer::appendRawvalue`
2. `DataBuffer::tryDequeueRawChunkForAlgo(128, ...)`
3. `DataBuffer::tryDequeueRawChunkForPlot(128, ...)`
4. `DataBuffer::clear`
5. `LogQueueBuffer::push`
6. `LogQueueBuffer::drainBatch`

其余都可暂时删掉，后面需要再加回来。

---

### E. 精简版代码骨架（可直接对照删减）

```cpp
// DataBuffer 最小骨架（仅 RAW + 双游标）
class DataBuffer {
public:
  void appendRawvalue(const RawSample &s);
  bool tryDequeueRawChunkForAlgo(int chunkSize, QVector<RawSample> &out);
  bool tryDequeueRawChunkForPlot(int chunkSize, QVector<RawSample> &out);
  void resetAlgoCursor();
  void resetPlotCursor();
  void clear();
private:
  QVector<RawSample> rawOrderedUnsafe() const;
  QVector<RawSample> m_raw;
  int m_rawWrite = 0;
  bool m_rawFilled = false;
  uint64_t m_algoReadSeq = 0;
  uint64_t m_plotReadSeq = 0;
  mutable QMutex m_mutex;
};
```

```cpp
// LogQueueBuffer 最小骨架
class LogQueueBuffer {
public:
  bool push(const LogItem &item);
  QList<LogItem> drainBatch(int maxItems);
  int droppedCount() const;
private:
  QQueue<LogItem> m_q;
  mutable QMutex m_mutex;
  int m_capacity = 4096;
  int m_dropped = 0;
};
```

---

## 二、双缓冲的职责划分（推荐）

### Buffer A：实时环形缓冲（给算法/绘图）
- 类：`DataBuffer`（保留）
- 数据：`RawSample`、`BigPackageSample`
- 行为：允许覆盖旧数据（为了实时）
- 读法：`recentRaw(count)`、`windowBySeq(endSeq, count)`（后面给代码）

### Buffer B：日志队列缓冲（给 CSV/HEX 写盘）
- 类：建议新建 `LogQueueBuffer`（下面给完整代码）
- 数据：日志事件（raw、hex、模型结果都可扩展）
- 行为：尽量不丢；队列满时要告警/策略明确
- 读法：`drainBatch(maxItems)`，供 writer 线程批量写盘

---

## 三、在现有 `DataBuffer` 上增强（完整示例）

下面代码可作为你当前 `core/databuffer.h/.cpp` 的改造参考。

### 3.1 `core/databuffer.h`（增强版示例）

```cpp
#ifndef DATABUFFER_H
#define DATABUFFER_H

#include <array>
#include <cstdint>
#include <QVector>
#include <QMutex>

enum class Band : int {
    Delta = 0, Theta, Alpha1, Alpha2, Beta1, Beta2, Gamma1, Gamma2, Count
};

struct BigPackageSample {
    std::array<double, static_cast<int>(Band::Count)> band{};
    short signal = 0;
    short att = 0;
    short med = 0;
    QString time;
    uint64_t raw_count_at_update = 0;
};

struct RawSample {
    double raw = 0.0;      // uV
    QString time;
    uint64_t raw_count = 0; // 单调递增序号（关键）
};

class DataBuffer
{
public:
    explicit DataBuffer(int rawCapacity = 512 * 10, int bigCapacity = 1 * 30);

    void appendRawvalue(const RawSample &s);
    void appendBigpackage(const BigPackageSample &s);

    QVector<RawSample> window_RawSample() const;
    QVector<BigPackageSample> window_BigPackageSample() const;

    // 新增：最近 N 点（给绘图）
    QVector<RawSample> recentRaw(int count) const;
    // 新增：按序号窗口（给算法重叠窗）
    QVector<RawSample> rawWindowBySeq(uint64_t endSeqInclusive, int count) const;
    // 新增：连续消费接口（给算法固定块处理，未消费点保留到下次）
    bool tryDequeueRawChunk(int chunkSize, QVector<RawSample> &outChunk);
    void resetAlgoCursor();

    uint64_t latestRawSeq() const;
    int rawCapacity() const { return m_rawCapacity; }

    void clear();

private:
    QVector<RawSample> rawOrderedUnsafe() const;

    int m_rawCapacity = 0;
    int m_bigCapacity = 0;
    QVector<RawSample> m_raw;
    QVector<BigPackageSample> m_big;
    int m_rawWrite = 0;
    int m_bigWrite = 0;
    bool m_rawFilled = false;
    bool m_bigFilled = false;
    uint64_t m_latestRawSeq = 0;
    uint64_t m_algoReadSeq = 0; // 算法“下一个要读”的 raw_count 游标

    mutable QMutex m_mutex;
};

#endif // DATABUFFER_H
```

### 3.2 `core/databuffer.cpp`（增强版示例）

```cpp
#include "databuffer.h"
#include <QMutexLocker>

DataBuffer::DataBuffer(int rawCapacity, int bigCapacity)
    : m_rawCapacity(rawCapacity), m_bigCapacity(bigCapacity)
{
    if (m_rawCapacity <= 0) m_rawCapacity = 1;
    if (m_bigCapacity <= 0) m_bigCapacity = 1;
    m_raw.resize(m_rawCapacity);
    m_big.resize(m_bigCapacity);
}

void DataBuffer::appendRawvalue(const RawSample &s)
{
    QMutexLocker locker(&m_mutex);
    m_raw[m_rawWrite] = s;
    m_latestRawSeq = s.raw_count;
    ++m_rawWrite;
    if (m_rawWrite >= m_rawCapacity) {
        m_rawWrite = 0;
        m_rawFilled = true;
    }
}

void DataBuffer::appendBigpackage(const BigPackageSample &s)
{
    QMutexLocker locker(&m_mutex);
    m_big[m_bigWrite] = s;
    ++m_bigWrite;
    if (m_bigWrite >= m_bigCapacity) {
        m_bigWrite = 0;
        m_bigFilled = true;
    }
}

QVector<RawSample> DataBuffer::rawOrderedUnsafe() const
{
    QVector<RawSample> out;
    if (!m_rawFilled) {
        out.reserve(m_rawWrite);
        for (int i = 0; i < m_rawWrite; ++i) out.push_back(m_raw[i]);
        return out;
    }
    out.reserve(m_rawCapacity);
    for (int i = 0; i < m_rawCapacity; ++i) {
        const int idx = (m_rawWrite + i) % m_rawCapacity;
        out.push_back(m_raw[idx]);
    }
    return out;
}

QVector<RawSample> DataBuffer::window_RawSample() const
{
    QMutexLocker locker(&m_mutex);
    return rawOrderedUnsafe();
}

QVector<BigPackageSample> DataBuffer::window_BigPackageSample() const
{
    QMutexLocker locker(&m_mutex);
    QVector<BigPackageSample> out;
    if (!m_bigFilled) {
        out.reserve(m_bigWrite);
        for (int i = 0; i < m_bigWrite; ++i) out.push_back(m_big[i]);
        return out;
    }
    out.reserve(m_bigCapacity);
    for (int i = 0; i < m_bigCapacity; ++i) {
        const int idx = (m_bigWrite + i) % m_bigCapacity;
        out.push_back(m_big[idx]);
    }
    return out;
}

QVector<RawSample> DataBuffer::recentRaw(int count) const
{
    QMutexLocker locker(&m_mutex);
    QVector<RawSample> all = rawOrderedUnsafe();
    if (count <= 0 || all.isEmpty()) return {};
    if (count >= all.size()) return all;
    return all.sliced(all.size() - count, count);
}

QVector<RawSample> DataBuffer::rawWindowBySeq(uint64_t endSeqInclusive, int count) const
{
    QMutexLocker locker(&m_mutex);
    QVector<RawSample> all = rawOrderedUnsafe();
    if (count <= 0 || all.isEmpty()) return {};

    // 从后往前找 <= endSeq 的最后一个点
    int endIdx = -1;
    for (int i = all.size() - 1; i >= 0; --i) {
        if (all[i].raw_count <= endSeqInclusive) {
            endIdx = i;
            break;
        }
    }
    if (endIdx < 0) return {};
    const int beginIdx = qMax(0, endIdx - count + 1);
    return all.sliced(beginIdx, endIdx - beginIdx + 1);
}

bool DataBuffer::tryDequeueRawChunk(int chunkSize, QVector<RawSample> &outChunk)
{
    QMutexLocker locker(&m_mutex);
    outChunk.clear();
    if (chunkSize <= 0) return false;

    QVector<RawSample> all = rawOrderedUnsafe();
    if (all.isEmpty()) return false;

    // 第一次调用时把游标对齐到当前最早可见点，避免从 0 序号“补历史”
    if (m_algoReadSeq == 0) {
        m_algoReadSeq = all.first().raw_count;
    }

    // 找到第一个 raw_count >= m_algoReadSeq 的下标
    int begin = -1;
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].raw_count >= m_algoReadSeq) {
            begin = i;
            break;
        }
    }

    // 若游标已经落后到被环形覆盖，直接追到当前最早点
    if (begin < 0) {
        m_algoReadSeq = all.first().raw_count;
        begin = 0;
    }

    const int available = all.size() - begin;
    if (available < chunkSize) {
        return false; // 不足 128 点（或 chunkSize）时，不消费，等下次继续累积
    }

    outChunk = all.sliced(begin, chunkSize);
    m_algoReadSeq = outChunk.last().raw_count + 1; // 已消费到 last，下一次从 last+1 开始
    return true;
}

void DataBuffer::resetAlgoCursor()
{
    QMutexLocker locker(&m_mutex);
    m_algoReadSeq = 0;
}

uint64_t DataBuffer::latestRawSeq() const
{
    QMutexLocker locker(&m_mutex);
    return m_latestRawSeq;
}

void DataBuffer::clear()
{
    QMutexLocker locker(&m_mutex);
    m_rawWrite = 0;
    m_bigWrite = 0;
    m_rawFilled = false;
    m_bigFilled = false;
    m_latestRawSeq = 0;
    m_algoReadSeq = 0;
}
```

### 3.3 按你要求的“固定 128 连续采样”调用方式（示例）

> 你的要求是：每次算法拿 128 个连续点；没凑够就不算；没用到的点留到下次继续。  
> 对应接口就是 `tryDequeueRawChunk(128, chunk)`。

```cpp
QVector<RawSample> chunk;
if (m_dataBuffer.tryDequeueRawChunk(128, chunk)) {
    // chunk.size() == 128，且按 raw_count 连续递增
    // 在这里做预处理/FFT/模型
} else {
    // 还没凑够 128 点，等待下一轮
}
```

如果你后续要重叠窗（例如 window=512, hop=128），建议：
- 继续保留 `tryDequeueRawChunk(128)` 做“新点推进”
- 在算法线程内部自己维护 512 长度环形分析窗，把每次 128 新点推入即可

### 3.4 按你的要求：算法与绘图都用“128 连续点”，且不浪费点

你现在明确要：
- 不拿“最后几点”，而是按序号连续消费；
- 每次固定拿 128 点；
- 不够 128 点就等待；
- 没用到的点下次继续；
- 算法和绘图都一样。

推荐把 `DataBuffer` 的“分块接口”改成**双游标**（算法游标 + 绘图游标），两个消费者互不影响：

```cpp
// databuffer.h 增加（示例）
bool tryDequeueRawChunkForAlgo(int chunkSize, QVector<RawSample> &outChunk);
bool tryDequeueRawChunkForPlot(int chunkSize, QVector<RawSample> &outChunk);
void resetAlgoCursor();
void resetPlotCursor();

private:
uint64_t m_algoReadSeq = 0;
uint64_t m_plotReadSeq = 0;
```

```cpp
// databuffer.cpp 增加（示例）
static bool tryDequeueByCursorImpl(const QVector<RawSample> &all,
                                   uint64_t &cursorSeq,
                                   int chunkSize,
                                   QVector<RawSample> &outChunk)
{
    outChunk.clear();
    if (all.isEmpty() || chunkSize <= 0) return false;

    if (cursorSeq == 0) cursorSeq = all.first().raw_count; // 首次对齐到当前最早点

    int begin = -1;
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].raw_count >= cursorSeq) { begin = i; break; }
    }
    if (begin < 0) return false; // cursor 已超过当前缓存

    const int available = all.size() - begin;
    if (available < chunkSize) return false; // 不足 128 点，不消费

    outChunk = all.sliced(begin, chunkSize);
    cursorSeq = outChunk.last().raw_count + 1; // 下次从未消费点继续
    return true;
}

bool DataBuffer::tryDequeueRawChunkForAlgo(int chunkSize, QVector<RawSample> &outChunk)
{
    QMutexLocker locker(&m_mutex);
    const QVector<RawSample> all = rawOrderedUnsafe();
    return tryDequeueByCursorImpl(all, m_algoReadSeq, chunkSize, outChunk);
}

bool DataBuffer::tryDequeueRawChunkForPlot(int chunkSize, QVector<RawSample> &outChunk)
{
    QMutexLocker locker(&m_mutex);
    const QVector<RawSample> all = rawOrderedUnsafe();
    return tryDequeueByCursorImpl(all, m_plotReadSeq, chunkSize, outChunk);
}

void DataBuffer::resetAlgoCursor()
{
    QMutexLocker locker(&m_mutex);
    m_algoReadSeq = 0;
}

void DataBuffer::resetPlotCursor()
{
    QMutexLocker locker(&m_mutex);
    m_plotReadSeq = 0;
}
```

调用方式（算法与绘图完全一致）：

```cpp
// 算法线程
QVector<RawSample> algoChunk;
if (m_dataBuffer.tryDequeueRawChunkForAlgo(128, algoChunk)) {
    // 连续 128 点，做算法
}

// 绘图线程（或 UI 定时器）
QVector<RawSample> plotChunk;
if (m_dataBuffer.tryDequeueRawChunkForPlot(128, plotChunk)) {
    // 连续 128 点，做增量绘图
}
```

这样可以满足你的要求：
- 算法与绘图都只处理连续 128 点；
- 不够 128 点就等；
- 没用到的点绝不会丢（除非环形缓冲容量太小导致被覆盖）；
- 两个消费者互不“抢点”。

> 容量建议：`rawCapacity >= 128 * 20`（至少能容纳 20 个块）以降低覆盖风险。  
> 若你需要“绝对不丢任何历史点”，请另外保留日志队列写盘（第四章），不要只依赖环形缓存。

---

## 四、日志队列缓冲（新建类，完整示例）

> 这部分建议新建：`core/logqueuebuffer.h/.cpp`。  
> 不建议把“全量写盘”压在 DataBuffer（环形会覆盖旧数据）。

### 4.1 `core/logqueuebuffer.h`

```cpp
#ifndef LOGQUEUEBUFFER_H
#define LOGQUEUEBUFFER_H

#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QDateTime>
#include <QByteArray>

struct LogItem
{
    qint64 tsMs = 0;
    uint64_t seq = 0;
    qint16 rawInt16 = 0;
    double rawUv = 0.0;
    quint8 signal = 0;
    quint8 att = 0;
    quint8 med = 0;
    QString frameHex; // 可选，留空表示不写 HEX
};

class LogQueueBuffer
{
public:
    explicit LogQueueBuffer(int capacity = 4096);

    // 生产者：push 一条日志，成功返回 true；满队列返回 false
    bool push(const LogItem &item);

    // 消费者：一次取最多 maxItems 条
    QList<LogItem> drainBatch(int maxItems);

    int size() const;
    int capacity() const { return m_capacity; }
    int droppedCount() const; // 满队列时丢弃计数

private:
    int m_capacity = 0;
    mutable QMutex m_mutex;
    QQueue<LogItem> m_q;
    int m_dropped = 0;
};

#endif // LOGQUEUEBUFFER_H
```

### 4.2 `core/logqueuebuffer.cpp`

```cpp
#include "logqueuebuffer.h"
#include <QMutexLocker>

LogQueueBuffer::LogQueueBuffer(int capacity)
    : m_capacity(capacity > 0 ? capacity : 1)
{
}

bool LogQueueBuffer::push(const LogItem &item)
{
    QMutexLocker locker(&m_mutex);
    if (m_q.size() >= m_capacity) {
        ++m_dropped; // 这里是“日志队列满了”的硬告警计数
        return false;
    }
    m_q.enqueue(item);
    return true;
}

QList<LogItem> LogQueueBuffer::drainBatch(int maxItems)
{
    QMutexLocker locker(&m_mutex);
    QList<LogItem> out;
    if (maxItems <= 0) return out;
    const int n = qMin(maxItems, m_q.size());
    out.reserve(n);
    for (int i = 0; i < n; ++i) out.push_back(m_q.dequeue());
    return out;
}

int LogQueueBuffer::size() const
{
    QMutexLocker locker(&m_mutex);
    return m_q.size();
}

int LogQueueBuffer::droppedCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_dropped;
}
```

---

## 五、`LogWorker` 建议改造（写盘线程）

你现有 `LogWorker` 建议改成“只做消费队列 + 批量写盘”，示例骨架：

```cpp
// 关键思路（伪代码）
class CsvLogWorker : public QObject {
  Q_OBJECT
public slots:
  void onUvSample(const UvSample &s) {  // 由解析/转换层调用
    LogItem item;
    item.tsMs = s.timestampMs;
    item.seq = s.seq;
    item.rawInt16 = s.rawInt16;
    item.rawUv = s.rawUv;
    m_queue.push(item); // push 失败就告警（队列满）
  }

  void start();
  void stop();
private slots:
  void onFlushTick(); // 每 100ms 批量取队列并写 CSV
};
```

刷新策略（你之前确认 SSD + 100ms）：
- 每 `100ms` 调一次 `onFlushTick()`
- 每次 `drainBatch(200~1000)` 批量写
- 再 `flush()`（或每 N 次 tick flush）

---

## 六、你当前代码里“可以直接保留”的部分

1. `DataBuffer` 的环形写入模型（保留）
2. `LogWorker` 的开关采集接口风格（保留）

## 七、你当前代码里“建议调整”的部分

1. `DataBuffer` 增加“按序号窗口”接口（算法线程非常有用）
2. 日志写盘从 `LogWorker` 分离成“队列 + writer”模型（避免拖采集）
3. `RawSample/BigPackageSample` 的 `QString time = 0;` 改为默认空字符串（建议规范）

---

## 八、落地顺序（最稳）

1. 先改 `DataBuffer`（加 `recentRaw/rawWindowBySeq/latestRawSeq`）
2. 新建 `LogQueueBuffer`
3. 把 `LogWorker` 改为 `CsvLogWorker`（消费队列、批量写）
4. 跑测试看：
   - `raw/s` 是否稳定
   - `logQueue droppedCount` 是否为 0
   - UI 刷新是否流畅

---

## 九、结论

你的现有代码不是“推倒重来”的状态。最佳方案是：
- **`DataBuffer`：增强**
- **`LogWorker`：改职责 + 新增日志队列类**

这样最小改动就能满足你“算法和波形都从缓存取、日志全量记录不拖采集”的目标。

