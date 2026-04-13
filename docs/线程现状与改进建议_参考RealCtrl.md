# QtBCI 当前线程现状与改进建议（参考 RealCtrl）

> 依据你当前代码：`main.cpp`、`mainwindow.cpp`、`thinkgear/thinkgearlinktester.{h,cpp}`、`core/csvlogworker.{h,cpp}`。  
> 目标：先稳住“采集 + CSV写盘 + UI响应”，再考虑扩展到 RealCtrl 的双工作线程架构。

---

## 1. 当前线程结构（实际运行）

## 1.1 UI 主线程（QApplication 线程）

- 创建并运行 `MainWindow`（`main.cpp`）。  
- `ThinkGearLinkTester` 由 `MainWindow` 持有，默认也在 UI 线程。  
- 串口链路对象 `SerialPort / ThinkGearFrameAssembler / ThinkGearPayloadParser / RawtOutUvProcessor` 都在 `ThinkGearLinkTester` 内创建，默认归属 UI 线程。  
- `onRawUvReady()`、`onTick1s()`、`onSecondReport()` 等高频逻辑目前都在 UI 线程回调中执行。

## 1.2 CSV 写盘线程（m_logThread）

- `ThinkGearLinkTester::startupLogWriter()` 创建 `QThread* m_logThread`。  
- `CsvLogWorker` `moveToThread(m_logThread)`。  
- `QThread::started -> CsvLogWorker::start`。  
- `CsvLogWorker` 内部 `QTimer` 定时 `drainBatch` 写 CSV。  

这条线是你当前唯一独立工作线程。

---

## 2. 当前线程风险点（按优先级）

## P0（必须先改）

- **写盘线程跨线程定时器风险**：你已出现过 `QObject::startTimer: Timers cannot be started from another thread`。  
  你现在已加 `m_timer.setParent(this)` 和线程检查，方向正确，需要持续验证是否彻底消失。

- **`onRawUvReady` 中 `li.tsMs` 赋值类型错误**：当前代码是  
  `li.tsMs = now.toString(Qt::TextDate);`  
  但 `LogItem.tsMs` 是 `qint64`。这会造成时间戳字段异常（线程问题外的功能错误）。

## P1（建议尽快）

- **UI线程承载采集+解析+缓冲+统计**：当前吞吐量上来后，可能影响界面响应（卡顿、掉帧）。  
- **`m_logBuffer.push` 无返回值处理**：队列满时未统计失败分支，仅靠 `size()` 观察不够精确。

## P2（后续优化）

- `main.cpp` 里保留多套测试 include（注释块）会降低可维护性。  
- 命名不一致：`m_csvLoggingEnable` vs `m_csvLoggingEnabled`。

---

## 3. 对照 RealCtrl：你可以借鉴的线程分层

RealCtrl 的思路（简化）：

- **UI线程**：界面交互、参数对话框、状态展示。  
- **DataWorkerThread**：采集与控制主流程（重计算不占 UI）。  
- **LogWorkerThread**：日志/落盘等 IO 密集工作。

你当前 QtBCI 相比 RealCtrl，缺少“数据处理线程”这一层。

---

## 4. 建议改造路线（分三步）

## 第一步：先把现有双线程跑稳（现在就能做）

1. 保持当前两线程模型：UI + `m_logThread`。  
2. 修正 `li.tsMs` 为毫秒整数：  
   `const qint64 nowMs = QDateTime::currentMSecsSinceEpoch(); li.tsMs = nowMs;`
3. `m_logBuffer.push(li)` 返回值加统计：失败时写 `testMessage` 或计数器。  
4. 观察指标：  
   - 无 `startTimer` 报错  
   - `logQ` 不再单调上涨  
   - CSV 持续追加数据行

## 第二步：把解析/缓冲从 UI 线程拆出去（参考 RealCtrl）

目标：新增 `m_dataThread`（类似 RealCtrl 的 DataWorkerThread）：

- 把 `ThinkGearLinkTester` 或其内部采集处理对象迁入 `m_dataThread`。  
- UI 仅接收信号（`secondReport/testMessage/plotChunkReady`）。  
- 写盘线程 `m_logThread` 保持独立。

这样可形成：

- UI线程：只绘图与控件更新  
- Data线程：串口、组帧、解析、转uV、入缓冲  
- Log线程：CSV/TXT IO

## 第三步：统一生命周期管理（Start/Stop）

- `MainWindow::on_pushButton_start_clicked`：  
  仅触发状态切换，不直接做耗时操作。  
- `ThinkGearLinkTester` 内部明确顺序：  
  `shutdown old workers -> config -> startup data/log workers -> open serial`。  
- 停止时：  
  `stop timer -> close serial -> stop log worker -> quit thread -> wait`。

---

## 5. 你当前代码的“最小线程改进清单”

可直接照此执行：

1. 修正 `thinkgearlinktester.cpp:onRawUvReady` 中 `li.tsMs` 赋值类型。  
2. `m_logBuffer.push(li)` 处理返回 false，增加 dropped 提示。  
3. 保留并观察 `CsvLogWorker::start` 线程一致性检查。  
4. 给 `ThinkGearLinkTester` 增加 `QThread* m_dataThread` 设计占位（先不迁移）。  
5. 在文档中把“当前为双线程，下一步三线程”明确给团队。

---

## 6. 推荐验收标准

- 启动采集 5 分钟：UI 不明显卡顿。  
- `logQ` 稳定在可控范围（不持续上涨到上限）。  
- CSV 每秒持续写入，且 `seq` 连续。  
- 停止后线程全部退出，无 QObject 线程警告。  

---

## 7. 结论

你当前不是“线程过多”，而是“**线程层次还不够清晰**”：

- 现在：`UI线程 + 写盘线程`（可用，但 UI 压力大）。  
- 建议目标（参考 RealCtrl）：`UI线程 + 数据线程 + 日志线程`。  

先把现有写盘线程稳定，再拆数据线程，是最稳妥路径。

