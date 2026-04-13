# CSV 日志写盘线程示例（`LogBuffer` → `.csv`，写完即“舍弃缓存”）

本文给你一套**可直接抄用**的示例：把 `core/logwbuffer.h` 里的 `LogBuffer` 当作“写盘队列”，在一个独立线程里用 `drainBatch()` 批量取出并写入 CSV。  
**关键语义**：`drainBatch()` 取出的数据会从队列里移除，所以**写入成功后，缓存区里这批数据就天然被舍弃**。

> 适用你当前结构：生产者（串口解析）不停 `push(LogItem)`；消费者（写盘线程）定时 `drainBatch()`。

---

## 一、你工程当前的队列结构回顾

你已有：

- `core/logwbuffer.h`：
  - `struct LogItem { tsMs, seq, rawInt16, rawUv }`
  - `class LogBuffer { push(), drainBatch(), droppedCount(), size() }`

其中：

- `push()`：队列满则返回 `false`，并且会增加 `droppedCount`
- `drainBatch(maxItems)`：一次性取走最多 `maxItems` 个元素（**从队列移除**）

---

## 二、写盘策略（推荐参数）

- **线程**：单独 `QThread`（避免写文件拖慢 UI/串口解析）
- **节流**：每 `50~100ms` 拉一批
- **批量**：每次 `256~2048` 条（视磁盘性能调整）
- **flush**：每批写完 flush（不要每条 flush）

如果你是 512Hz RAW：每秒 512 条，`100ms` 一次、每次拉 `512` 足够；写盘压力非常小。

---

## 三、`CsvLogWorker` 示例（头文件 + 源文件）

把下面两个代码块复制到工程里任意目录（例如 `core/` 或 `utils/`），并加入 CMake/工程文件即可。

### 3.1 `csvlogworker.h`

```cpp
#pragma once

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QString>

class LogBuffer;
struct LogItem;

class CsvLogWorker : public QObject
{
    Q_OBJECT
public:
    explicit CsvLogWorker(LogBuffer *buffer, QObject *parent = nullptr);

    // 建议：在 worker 所在线程调用（例如线程 started 后调用）
    void setOutputPath(const QString &csvPath) { m_csvPath = csvPath; }
    void setFlushIntervalMs(int ms) { m_flushIntervalMs = ms; }
    void setBatchSize(int n) { m_batchSize = n; }

public slots:
    void start();   // 打开文件 + 启动定时拉取
    void stop();    // 停止定时器 + 关闭文件

signals:
    void workerError(const QString &msg);
    void workerInfo(const QString &msg);
    void drained(int items, int queueSizeAfter, int droppedCount);

private slots:
    void onFlushTick();

private:
    void writeHeaderIfNeeded();
    void writeItem(const LogItem &it);

private:
    LogBuffer *m_buf = nullptr; // 外部管理生命周期（一般是 MainWindow / Tester 成员）

    QString m_csvPath;
    QFile m_file;
    QTextStream m_out;
    QTimer m_timer;

    int m_flushIntervalMs = 100; // 50~100ms 常用
    int m_batchSize = 512;       // 每次最多写多少条
    bool m_headerWritten = false;
};
```

### 3.2 `csvlogworker.cpp`

```cpp
#include "csvlogworker.h"

#include "core/logwbuffer.h"   // LogBuffer / LogItem
#include <QDateTime>

CsvLogWorker::CsvLogWorker(LogBuffer *buffer, QObject *parent)
    : QObject(parent)
    , m_buf(buffer)
{
    m_timer.setTimerType(Qt::CoarseTimer); // 写盘不需要高精度
    connect(&m_timer, &QTimer::timeout, this, &CsvLogWorker::onFlushTick);
}

void CsvLogWorker::start()
{
    if (!m_buf) {
        emit workerError(QStringLiteral("CsvLogWorker: buffer is null"));
        return;
    }
    if (m_csvPath.isEmpty()) {
        // 给个默认文件名（你也可以在外面 setOutputPath）
        m_csvPath = QString("raw_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    }

    if (m_file.isOpen())
        m_file.close();

    m_file.setFileName(m_csvPath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        emit workerError(QStringLiteral("CsvLogWorker: open failed: %1").arg(m_file.errorString()));
        return;
    }

    m_out.setDevice(&m_file);
    m_out.setCodec("UTF-8");

    // 如果是新文件（size==0），写表头
    m_headerWritten = false;
    writeHeaderIfNeeded();

    m_timer.start(m_flushIntervalMs);
    emit workerInfo(QStringLiteral("CsvLogWorker started, path=%1").arg(m_csvPath));
}

void CsvLogWorker::stop()
{
    if (m_timer.isActive())
        m_timer.stop();

    // 最后再冲一次，把残余队列尽量写完（可选）
    onFlushTick();

    if (m_file.isOpen()) {
        m_out.flush();
        m_file.flush();
        m_file.close();
    }
    emit workerInfo(QStringLiteral("CsvLogWorker stopped"));
}

void CsvLogWorker::writeHeaderIfNeeded()
{
    if (!m_file.isOpen())
        return;
    if (m_headerWritten)
        return;

    if (m_file.size() == 0) {
        // 你可以按需要扩列：例如加 kind、signal、att、med、帧计数等
        m_out << "tsMs,seq,rawInt16,rawUv\n";
        m_out.flush();
    }
    m_headerWritten = true;
}

void CsvLogWorker::writeItem(const LogItem &it)
{
    // 纯数值列，便于 Excel / Python
    m_out << it.tsMs << ','
          << it.seq << ','
          << it.rawInt16 << ','
          << it.rawUv
          << '\n';
}

void CsvLogWorker::onFlushTick()
{
    if (!m_buf || !m_file.isOpen())
        return;

    writeHeaderIfNeeded();

    // drainBatch：把队列里的元素“取走”（从队列移除）——写完即舍弃缓存的关键
    const QList<LogItem> batch = m_buf->drainBatch(m_batchSize);
    if (batch.isEmpty())
        return;

    for (const LogItem &it : batch)
        writeItem(it);

    // 每批 flush 一次即可（不要每条 flush）
    m_out.flush();
    m_file.flush();

    emit drained(batch.size(), m_buf->size(), m_buf->droppedCount());
}
```

---

## 四、怎么启动写盘线程（`QThread` 接线示例）

下面示例假设你在某个类里有成员：

- `LogBuffer m_logBuffer;`
- 想让写盘线程把 `m_logBuffer` 里的数据持续写到 CSV

### 4.1 成员定义（示意）

```cpp
#include <QThread>

QThread *m_logThread = nullptr;
CsvLogWorker *m_logWorker = nullptr;
```

### 4.2 在 `start()` 时启动（示意）

```cpp
// 例如 ThinkGearLinkTester::start() 成功打开串口后：
if (!m_logThread) {
    m_logThread = new QThread(this);
    m_logWorker = new CsvLogWorker(&m_logBuffer);
    m_logWorker->setOutputPath("raw_log.csv"); // 也可带时间戳
    m_logWorker->setFlushIntervalMs(100);
    m_logWorker->setBatchSize(1024);

    m_logWorker->moveToThread(m_logThread);

    QObject::connect(m_logThread, &QThread::started, m_logWorker, &CsvLogWorker::start);
    QObject::connect(m_logThread, &QThread::finished, m_logWorker, &QObject::deleteLater);

    QObject::connect(m_logWorker, &CsvLogWorker::workerError, this, [](const QString &msg) {
        qWarning().noquote() << msg;
    });
    QObject::connect(m_logWorker, &CsvLogWorker::drained, this, [](int items, int q, int dropped) {
        // 可选：每秒统计里也可以打印 size/dropped
        Q_UNUSED(items);
        Q_UNUSED(q);
        Q_UNUSED(dropped);
    });

    m_logThread->start();
}
```

### 4.3 在 `stop()` 时停止（示意）

```cpp
if (m_logWorker) {
    // 让 worker 在其线程中 stop（确保把残余 batch 冲刷到文件）
    QMetaObject::invokeMethod(m_logWorker, "stop", Qt::QueuedConnection);
}
if (m_logThread) {
    m_logThread->quit();
    m_logThread->wait(1500);
    m_logThread->deleteLater();
    m_logThread = nullptr;
    m_logWorker = nullptr;
}
```

> 注意：如果你希望 stop 时“必须写完队列里所有内容”，可以在 `CsvLogWorker::stop()` 里循环 drain 直到空；但一般没必要（尤其是 UI 关闭时）。

---

## 五、为什么这样写就不会 `logQ` 一下到 4096？

如果你现在看到 `logQ`（队列大小）快速到 4096，说明 **只有 push 没有 drain**。  
上述 `CsvLogWorker` 启动后，会每 100ms 拉走一批：

- 512Hz 数据：每秒 512 条
- 写盘：每 100ms 拉 1024 条（上限）  

那么队列会在很小的范围波动（接近 0），不会长期堆积。

---

## 六、常见坑（非常关键）

- **不要在生产者里每条写 CSV**：会把串口/GUI 卡死。
- **不要每条 `flush()`**：磁盘 IO 会被放大很多倍。
- **不要无限打印 `qDebug`**：512Hz 打印会严重拖慢事件循环，反过来导致 `logQ` 满。
- **队列满时要统计丢包**：定期打印 `droppedCount()`，它能告诉你是不是写盘不够快或 UI 卡顿。

---

## 七、扩展：多种 CSV（raw / algo / debug）怎么做？

有两种常见做法：

1. **一个队列 + `kind` 字段**：`LogItem` 增加 `kind`（枚举），writer 里 `switch(kind)` 写不同文件或同一文件不同列。
2. **多个队列**：raw 一个队列、algo 一个队列……实现更直观但维护更复杂。

你当前 `LogItem` 没有 `kind`，先把 raw 写稳是第一步。

