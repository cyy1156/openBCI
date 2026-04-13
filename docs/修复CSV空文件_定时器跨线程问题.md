# 修复 CSV 只有表头：`QTimer` 跨线程启动失败（最小改动版）

你当前日志里有这句关键报错：

```text
QObject::startTimer: Timers cannot be started from another thread
```

这会导致 `CsvLogWorker` 的 `m_timer.start(...)` 失败，`onFlushTick()` 不执行，结果就是：

- `logQ` 持续增长
- CSV 只有表头，没有数据行

---

## 一、最小改动目标

只改 `core/csvlogworker.cpp`（必要时头文件也可不动），保证：

1. `QTimer` 的线程归属与 `CsvLogWorker` 一致  
2. `m_timer.start()` 在 worker 线程内执行  

---

## 二、推荐修改（最少且稳定）

### 1) 在构造函数里给 `m_timer` 设 parent（关键）

把 `CsvLogWorker` 构造函数改成：

```cpp
CsvLogWorker::CsvLogWorker(LogBuffer *buffer, QObject *parent)
    : QObject{parent}
    , m_buf(buffer)
{
    m_timer.setParent(this); // 关键：确保与 worker 同归属
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &CsvLogWorker::onFlushTick);
}
```

> 为什么：`CsvLogWorker` 被 `moveToThread(m_logThread)` 后，带 parent 的子对象会和父对象同线程归属，避免 `startTimer` 跨线程错误。

---

### 2) 在 `start()` 里加线程检查日志（便于确认）

可选但强烈建议，临时加上：

```cpp
#include <QThread>
#include <QDebug>
```

在 `start()` 开头加：

```cpp
qDebug() << "[CsvLogWorker::start] current=" << QThread::currentThread()
         << " workerThread=" << thread();
```

若两者不同，说明你的连接方式有问题；正常应一致（都在 worker 线程）。

---

### 3) `stop()` 用当前写法即可

你已有：

```cpp
if (m_timer.isActive()) m_timer.stop();
onFlushTick();
```

这是对的，不需要改。

---

## 三、`ThinkGearLinkTester` 配套确认（你当前基本已改对）

确保仍然是下面这种连接方式（你现在是这个）：

```cpp
QObject::connect(m_logThread, &QThread::started, m_logWorker, &CsvLogWorker::start);
QMetaObject::invokeMethod(m_logWorker, "stop", Qt::QueuedConnection);
```

并且 **`start()` 里不要再调用整包 `stop()`**（你现在已经去掉了）。

---

## 四、修完后的验证步骤

1. 重新编译并运行。  
2. 看控制台不再出现：
   - `QObject::startTimer: Timers cannot be started from another thread`
3. 看每秒统计里 `logQ`：
   - 应该不再单调上涨到 4096，而是在小范围波动。
4. 打开 CSV：
   - 除表头外，应该持续新增数据行。

---

## 五、如果仍然空文件，再看这两点

1. `m_csvLoggingEnable(d)` 是否真的为 `true`（你点击保存对话框后是否点了 `OK`）。  
2. `m_csvOutputPath` 是否非空（`setCsvOutputPath` 是否在开始前已设置）。

---

## 六、附：可选更稳方案（后续再做）

如果你想彻底避免 `QTimer` 线程归属问题，可以把 `QTimer m_timer;` 改成指针，在 `start()` 里（worker线程）`new QTimer(this)` 创建，再连接和 `start()`。  
但这比“`setParent(this)`”改动大，当前不必。

