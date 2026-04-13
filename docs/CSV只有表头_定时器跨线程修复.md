# CSV 只有表头、`logQ` 持续上涨的修复说明（你当前项目）

你现在的现象：

- CSV 只有表头 `tsMS,seq,rawInt16,rawUv`
- `logQ` 每秒上涨
- 控制台报错：`QObject::startTimer: Timers cannot be started from another thread`

这说明：`m_logBuffer.push(...)` 一直在进队列，但 `CsvLogWorker` 的 `QTimer` 没正常启动，`onFlushTick()` 没跑，`drainBatch()` 从未消费。

---

## 一、根因（简述）

`CsvLogWorker` 被 `moveToThread(m_logThread)` 后，`QTimer` 的线程归属处理不当，导致在错误线程启动定时器。

---

## 二、最小修复方案（推荐，改动少）

目标：让 `QTimer` 成为 `CsvLogWorker` 的子对象，确保它跟随 worker 到同一线程，并在 worker 线程里启动。

### 1) 改 `core/csvlogworker.cpp` 构造函数

把当前构造函数中的这几行（你现在是）：

```cpp
CsvLogWorker::CsvLogWorker(LogBuffer *buffer,QObject *parent)
    : QObject{parent}
    ,m_buf(buffer)
{
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer,&QTimer::timeout,this,&CsvLogWorker::onFlushTick);
}
```

改成：

```cpp
CsvLogWorker::CsvLogWorker(LogBuffer *buffer, QObject *parent)
    : QObject(parent)
    , m_buf(buffer)
{
    m_timer.setParent(this); // 关键：让 timer 归属于 worker 对象
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &CsvLogWorker::onFlushTick);
}
```

---

### 2) 在 `start()` 增加线程断言（便于确认）

在 `CsvLogWorker::start()` 开头加一段调试检查：

```cpp
if (QThread::currentThread() != thread()) {
    emit workerError(QStringLiteral("CsvLogWorker::start 线程不一致"));
    return;
}
```

如果这条触发，说明你的 `connect(m_logThread, &QThread::started, ... CsvLogWorker::start)` 没按队列方式进入 worker 线程（正常不应触发）。

---

### 3) `ThinkGearLinkTester` 里保留这两点（你当前基本已对）

```cpp
QObject::connect(m_logThread, &QThread::started, m_logWorker, &CsvLogWorker::start);
QMetaObject::invokeMethod(m_logWorker, "stop", Qt::QueuedConnection);
```

并且 `start()` 里不要再调用会停掉日志线程的整包 `stop()`（你当前已改成 `shutdownLogWriter()` + `startupLogWriter()`，方向正确）。

---

## 三、可选更稳方案（如果你还想更保险）

把 `QTimer m_timer;` 改成指针并在 `start()` 里创建（此时一定在 worker 线程）：

```cpp
// h
QTimer *m_timer = nullptr;

// start
if (!m_timer) {
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &CsvLogWorker::onFlushTick);
}
m_timer->start(m_flushIntervalMs);
```

`stop()` 里相应改为：

```cpp
if (m_timer && m_timer->isActive())
    m_timer->stop();
```

这个方案更“线程安全直观”，但改动比方案二稍大。

---

## 四、修复后你应该看到什么

1. 不再出现 `QObject::startTimer...` 报错  
2. `logQ` 不再单调上涨，通常在小范围波动或下降  
3. CSV 不再只有表头，会持续追加数据行  

---

## 五、顺手建议（非本次主因）

- 把表头 `tsMS` 改为 `tsMs`（和字段名一致）  
- 你现在 `rawUv` 打印为 0 是上游数据链路问题，不影响“是否写行”；先修定时器，再查 0 值来源。

