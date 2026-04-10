# 转换层完整示例：`raw(int16) -> uV(double)`（Qt/C++）

> 目标：提供一个独立“转换层”，只做原始脑电值到微伏（uV）的换算，并输出统一结构给记录层、算法层、绘图层。  
> 本文是**参考代码文档**，不直接修改你工程源码。

---

## 1. 设计目标

转换层只做三件事：

1. 接收解析层发出的 `raw_int16`（可附带质量指标）
2. 按公式换算成 `raw_uV`
3. 发出结构化结果信号（供 CSV、Python、绘图复用）

不做的事：
- 不做组帧
- 不做 TLV 解析
- 不做 FFT/模型

---

## 2. 换算公式

按你给的 NeuroSky 思路：

`uV = rawValue * (1.8 / 4096) / 2000 * 1000000`

可写成：

`uV = rawValue * kUvPerCount`

其中：

`kUvPerCount = (1.8 * 1000000) / (4096 * 2000) ≈ 0.2197265625`

---

## 3. 头文件示例：`rawtouvprocessor.h`

```cpp
#ifndef RAWTOUVPROCESSOR_H
#define RAWTOUVPROCESSOR_H

#include <QObject>
#include <QtGlobal>

struct UvSample
{
    qint64 timestampMs = 0;      // 采样时刻（毫秒）
    quint64 seq = 0;             // 单调递增样本序号
    qint16 rawInt16 = 0;         // 原始值（保留用于追溯）
    double rawUv = 0.0;          // 换算后的微伏

    // 可选上下文（没有就填默认值）
    quint8 signalQuality = 0;    // 0x02
    quint8 attention = 0;        // 0x04
    quint8 meditation = 0;       // 0x05
    quint8 blink = 0;            // 0x16
};

class RawToUvProcessor : public QObject
{
    Q_OBJECT

public:
    explicit RawToUvProcessor(QObject *parent = nullptr);

    // 可调参数：uV/Count 比例，默认官方值
    void setUvPerCount(double v) { m_uvPerCount = v; }
    double uvPerCount() const { return m_uvPerCount; }

    // 可选增益微调（比如标定后 ±5%）
    void setGainAdjust(double gain) { m_gainAdjust = gain; } // 1.0 表示不调整
    double gainAdjust() const { return m_gainAdjust; }

    quint64 sampleCount() const { return m_seq; }

public slots:
    // 最简入口：只有 raw
    void onRaw(qint16 rawValue);

    // 扩展入口：携带质量字段，便于后续 CSV / Python 同步使用
    void onRawWithMeta(qint16 rawValue, quint8 signalQuality, quint8 attention, quint8 meditation, quint8 blink);

signals:
    // 输出统一结构（推荐）
    void uvSampleReady(const UvSample &sample);

    // 兼容简单消费者（只要 uV）
    void uvValueReady(double uv);

private:
    UvSample buildSample(qint16 rawValue, quint8 signalQuality, quint8 attention, quint8 meditation, quint8 blink);

    quint64 m_seq = 0;
    double m_uvPerCount = (1.8 * 1000000.0) / (4096.0 * 2000.0); // 0.2197265625
    double m_gainAdjust = 1.0; // 标定系数
};

#endif // RAWTOUVPROCESSOR_H
```

---

## 4. 实现文件示例：`rawtouvprocessor.cpp`

```cpp
#include "rawtouvprocessor.h"

#include <QDateTime>

RawToUvProcessor::RawToUvProcessor(QObject *parent)
    : QObject(parent)
{
}

void RawToUvProcessor::onRaw(qint16 rawValue)
{
    const UvSample s = buildSample(rawValue, 0, 0, 0, 0);
    emit uvSampleReady(s);
    emit uvValueReady(s.rawUv);
}

void RawToUvProcessor::onRawWithMeta(qint16 rawValue,
                                     quint8 signalQuality,
                                     quint8 attention,
                                     quint8 meditation,
                                     quint8 blink)
{
    const UvSample s = buildSample(rawValue, signalQuality, attention, meditation, blink);
    emit uvSampleReady(s);
    emit uvValueReady(s.rawUv);
}

UvSample RawToUvProcessor::buildSample(qint16 rawValue,
                                       quint8 signalQuality,
                                       quint8 attention,
                                       quint8 meditation,
                                       quint8 blink)
{
    UvSample s;
    s.timestampMs = QDateTime::currentMSecsSinceEpoch();
    s.seq = ++m_seq;
    s.rawInt16 = rawValue;

    // uV 转换：raw * (uV/count) * 标定系数
    s.rawUv = static_cast<double>(rawValue) * m_uvPerCount * m_gainAdjust;

    s.signalQuality = signalQuality;
    s.attention = attention;
    s.meditation = meditation;
    s.blink = blink;
    return s;
}
```

---

## 5. 在主流程里的接线示例

假设你已有：
- `ThinkGearPayloadParser`（发出 `rawReceived(qint16)`）
- `RawToUvProcessor`
- `CsvLogger` / `PythonBridge` / `DataBufferAdapter`

```cpp
auto *parser = new ThinkGearPayloadParser(this);
auto *converter = new RawToUvProcessor(this);

// 解析层 raw -> 转换层
connect(parser, &ThinkGearPayloadParser::rawReceived,
        converter, &RawToUvProcessor::onRaw);

// 转换层 -> CSV 写盘（结构化）
connect(converter, &RawToUvProcessor::uvSampleReady,
        csvLogger, &CsvLogger::onUvSample);

// 转换层 -> Python 实时推理
connect(converter, &RawToUvProcessor::uvSampleReady,
        pyBridge, &PythonBridge::onUvSample);

// 转换层 -> 绘图缓存
connect(converter, &RawToUvProcessor::uvSampleReady,
        dataBufferAdapter, &DataBufferAdapter::onUvSample);
```

---

## 6. 为什么这样设计（核心理由）

1. **单一职责**：协议解析与物理单位换算分离，后面替换模型更轻松。  
2. **可追溯**：同时保留 `rawInt16` 和 `rawUv`，CSV 回放能重算。  
3. **统一输出结构**：一个 `UvSample` 可以同时给 CSV、Python、UI，避免重复组装字段。  
4. **可标定**：`m_gainAdjust` 预留给后期设备偏差修正。  

---

## 7. 可选增强（后续再加）

- `onRawWithMeta(...)` 改成接收完整上下文对象（含最近 attention/meditation）  
- 增加 `setTimestampProvider(...)`，统一时间源（如果你有设备时间戳）  
- 增加饱和/异常值告警（例如 `abs(rawInt16)` 过大时 emit warning）  

---

## 8. CMake 添加（示例）

```cmake
    thinkgear/rawtouvprocessor.h
    thinkgear/rawtouvprocessor.cpp
```

---

## 9. 你现在就能用的最小版本

如果你只想先跑通：
- 只实现 `onRaw(qint16)` 这个槽  
- 只发 `uvValueReady(double)`  
- 后面确认链路稳定，再换成 `UvSample` 完整结构

这样最容易把“实时采集 + uV 换算 + Python”先打通。

