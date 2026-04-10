# Qt C++ ↔ Python 实时接口方案（512Hz，1s窗，Hop=256）

> 目标：在不拖慢采集链路的前提下，把 `raw -> uV` 后的数据实时送入 Python 做预处理与模型推理；同时保证 CSV/HEX 记录完整。

---

## 1. 设计原则（按你的优先级）

1. **采集不丢包优先**：串口、组帧、解析、记录链路不得被 Python 阻塞。  
2. **记录完整优先于显示完整**：记录队列不轻易丢；绘图/推理输入可丢旧保实时。  
3. **接口稳定**：Qt 到 Python 的消息格式固定，算法改动不影响采集层。  

---

## 2. 总体数据流

```text
SerialPort -> FrameAssembler -> PayloadParser -> UvConverter
                                      |                |
                                      |                +--> metrics.csv (writer thread)
                                      |
                                      +--> frame_hex.csv (writer thread)

UvConverter -> PythonBridgeQueue (bounded, realtime)
            -> DataBuffer (for UI plot, ring)

PythonWorker (1s window, hop=256) -> model result
                                  -> Qt UI + result.csv
```

---

## 3. Qt 到 Python 的输入消息格式（建议固定）

每个 raw 点一条（512Hz）：

```text
timestamp_ms, seq, raw_int16, raw_uV, signal_quality, attention, meditation
```

字段说明：
- `timestamp_ms`：`qint64`，毫秒时间戳（建议 `QDateTime::currentMSecsSinceEpoch()`）
- `seq`：`quint64` 单调递增样本序号（用于检测掉点/重排）
- `raw_int16`：原始 16 位值（留档、可重算）
- `raw_uV`：已换算微伏（Python主输入）
- 其它低频字段可复用最近值填充（便于模型拿上下文）

---

## 4. 队列与线程策略（关键）

### 4.1 两类队列（不要混）

1. **记录队列（可靠）**  
   - 用于 CSV/HEX 落盘  
   - 容量建议：`2048~8192` 条（SSD 足够）  
   - 目标：尽量不丢  

2. **推理队列（实时）**  
   - 用于喂 Python  
   - 容量建议：`512~1024` 条  
   - 目标：低延迟，满时可丢旧

### 4.2 满队列策略（必须明确）

- 记录队列满：优先告警 + 扩容 + 加快 writer；尽量不丢。  
- 推理队列满：**丢最旧**（drop oldest），保当前实时性。  

---

## 5. Python 侧消费与窗口参数

你确定的是 `fs=512Hz`，`window=1s`：

- `window_size = 512`  
- `hop_size = 256`（每 0.5s 出一次结果，实时性与计算量平衡）  

处理流程：
1. 从队列读取 `raw_uV`  
2. 维护滑动缓冲（至少 512 点）  
3. 每到 hop_size 点执行：
   - 去直流 / 滤波
   - 加窗
   - FFT
   - 频带积分
   - 特征归一化
   - 模型推理
4. 输出一条结果给 Qt

---

## 6. Python 回传 Qt 的结果格式

建议每个 hop 输出一条：

```text
timestamp_ms, seq_end, valence, arousal, stress_score, confidence
```

可按你模型替换字段，关键保留：
- `timestamp_ms`：窗口结束时刻
- `seq_end`：该窗口最后一个样本序号（便于和原始数据对齐）
- `confidence`：置信度（UI 过滤/稳定显示用）

---

## 7. 通信方式建议（按实现难度排序）

1. **本机 ZeroMQ / TCP localhost（推荐）**
   - 解耦好，进程隔离，Python崩了不拖 Qt
2. **QProcess + stdin/stdout**
   - 上手快，适合先跑通
3. **嵌入 Python（pybind/CPython API）**
   - 复杂度高，不建议首版

首版建议：`QProcess + 行协议(JSON Lines)` 或 `TCP+JSON`。

---

## 8. 最小可行消息协议（JSON Lines）

Qt -> Python（一行一个样本）：

```json
{"t":1710000000123,"seq":12345,"raw":-512,"uv":-112.5,"sig":12,"att":58,"med":44}
```

Python -> Qt（一行一个结果）：

```json
{"t":1710000000623,"seq_end":12599,"valence":0.62,"arousal":0.48,"stress":0.31,"conf":0.87}
```

优点：易调试、易落盘、跨语言稳定。

---

## 9. CSV 文件模板（你当前目标可直接采用）

### 9.1 `metrics_raw.csv`
列：

```text
timestamp_ms,seq,raw_int16,raw_uV,signal_quality,attention,meditation,blink
```

### 9.2 `frames_hex.csv`
列：

```text
timestamp_ms,frame_index,frame_len,frame_hex
```

### 9.3 `model_result.csv`
列：

```text
timestamp_ms,seq_end,valence,arousal,stress_score,confidence
```

---

## 10. Qt 侧建议实现顺序（避免一次做太多）

1. 先跑通 `raw_int16 -> raw_uV` 与 `metrics_raw.csv`  
2. 再接 PythonBridge（先不做模型，只回显）  
3. 再接入预处理 + FFT + 模型  
4. 最后再做 UI 优化（低刷新、稳定显示、告警面板）

---

## 11. 关键监控指标（上线前必须有）

Qt 侧每秒打印/记录一次：
- `samples_in/s`（应接近 512）
- `frames_ok/s`
- `parser_warning/s`
- `record_queue_depth`
- `infer_queue_depth`
- `infer_latency_ms`（Python返回延迟）

触发阈值建议：
- `infer_queue_depth > 80%` 连续 3 秒 -> 告警并自动降载（如临时增大 hop）

---

## 12. 一句话落地建议

你现在就按这个策略走：
- **Qt：采集/组帧/解析/uV/记录**
- **Python：预处理 + FFT + 模型**
- **队列分离：记录可靠、推理实时**

这能最大化保证“采集不丢 + 分析可迭代 + 工程可维护”。

