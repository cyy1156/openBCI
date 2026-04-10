# `device/serialport` 完整写法与原理说明

> 本文**只放在文档里**，不直接改你工程里的源文件。  
> 你当前的 `serialport.cpp` 里已经用了 `QObject` 父对象、`emit dataReceived` 等，但 `serialport.h` 若仍是最简版，会出现**声明与实现不一致**，编译报错。下面给出一套**头文件与实现互相匹配**的完整示例，并解释每一部分为什么要这样写。

---

## 一、设计目标（先想清楚再写类）

| 目标 | 做法 |
|------|------|
| 串口生命周期清晰 | 封装 `open` / `close`，析构时关闭 |
| 不阻塞界面 | 用 `QSerialPort::readyRead` 信号异步读，**禁止**在槽里长时间 `waitForReadyRead` |
| 配置可复用 | 用 `SerialPortConfig` 结构体保存端口名、波特率、数据位等 |
| 上层只管「字节流」 | 收到数据发信号 `dataReceived(QByteArray)`；解析/组帧在**别的类**里做 |
| 调试看原始数据 | 提供静态工具函数 `toHexSpaced`，不把打印逻辑写死在硬件层里 |

---

## 二、为什么必须继承 `QObject` 并写 `Q_OBJECT`

- **`QSerialPort` 的父对象建议是 `QObject*`**，这样窗口关闭时子对象会自动释放。  
- 你要发 **`signals`**（例如 `dataReceived`），封装类本身必须是 **`QObject` 子类**，且类内写 **`Q_OBJECT`**，以便 **moc** 生成元对象代码。  
- 若忘记 `Q_OBJECT` 或头文件里没声明信号，就会出现「`emit` 未定义」「信号找不到」等错误。

---

## 三、完整头文件示例：`serialport.h`

下面与下文 `serialport.cpp` **一一对应**，可直接整体替换你对照学习（或复制到你本地文件）。

```cpp
#ifndef SERIALPORT_H
#define SERIALPORT_H

#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QByteArray>

// 串口参数打包：避免 open 时漏设某一项；默认值符合常见脑电模块 8N1 + 57600
struct SerialPortConfig {
    QString portName = QStringLiteral("COM7");
    qint32 baudRate = 57600;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    // 0 表示不改默认；>0 则 setReadBufferSize，减轻高频小包下 read 次数
    int readBufferSize = 32 * 1024;
};

class SerialPort : public QObject
{
    Q_OBJECT

public:
    explicit SerialPort(QObject *parent = nullptr);
    ~SerialPort() override;

    void setPortName(const QString &name);
    void setBaudRate(qint32 baud);
    void setConfig(const SerialPortConfig &cfg);
    SerialPortConfig config() const { return m_cfg; }

    bool openReadOnly();
    bool openReadWrite();
    void close();
    bool isOpen() const;

    qint64 write(const QByteArray &data);

    // 调试：空格分隔十六进制，例如 "aa aa 04"
    static QString toHexSpaced(const QByteArray &data);

signals:
    void dataReceived(const QByteArray &chunk);
    void portError(const QString &message);

private:
    void setupConnections();
    void applyConfig();

    QSerialPort m_port;
    SerialPortConfig m_cfg;
};

#endif // SERIALPORT_H
```

### 头文件要点说明

1. **`explicit SerialPort(QObject *parent = nullptr)`**  
   避免 `SerialPort` 被隐式从 `QObject*` 转换构造，减少误用。

2. **`m_port` 用成员对象而不是指针**  
   生命周期跟封装类一致；构造时传 `this` 给 `QSerialPort(this)`，父对象树正确。

3. **`setPortName` / `setBaudRate` / `setConfig` 里同步 `m_cfg` 与已打开的 `m_port`**  
   若端口已打开，只改配置结构体而不调用 `setXxx`，硬件仍按旧参数工作。

4. **`openReadOnly` vs `openReadWrite`**  
   只收数据用脑电时通常 **ReadOnly** 足够；若你要下发命令再选 **ReadWrite**。

5. **`write` 暴露给上层**  
   需要发字节时直接调；返回值是已写字节数，`-1` 表示失败（可再读 `m_port.errorString()`）。

---

## 四、完整实现示例：`serialport.cpp`

```cpp
#include "serialport.h"

#include <QDebug>

SerialPort::SerialPort(QObject *parent)
    : QObject(parent)
    , m_port(this)
{
    setupConnections();
}

SerialPort::~SerialPort()
{
    close();
}

void SerialPort::setPortName(const QString &name)
{
    m_cfg.portName = name;
    if (m_port.isOpen())
        m_port.setPortName(name);
}

void SerialPort::setBaudRate(qint32 baud)
{
    m_cfg.baudRate = baud;
    if (m_port.isOpen())
        m_port.setBaudRate(baud);
}

void SerialPort::setConfig(const SerialPortConfig &cfg)
{
    m_cfg = cfg;
    if (m_port.isOpen())
        applyConfig();
}

void SerialPort::applyConfig()
{
    m_port.setPortName(m_cfg.portName);
    m_port.setBaudRate(m_cfg.baudRate);
    m_port.setDataBits(m_cfg.dataBits);
    m_port.setParity(m_cfg.parity);
    m_port.setStopBits(m_cfg.stopBits);
    m_port.setFlowControl(m_cfg.flowControl);
    if (m_cfg.readBufferSize > 0)
        m_port.setReadBufferSize(m_cfg.readBufferSize);
}

void SerialPort::setupConnections()
{
    // readyRead：只要有可读数据就会触发；readAll() 可能一次读出多帧或半帧，属正常现象
    connect(&m_port, &QSerialPort::readyRead, this, [this] {
        const QByteArray chunk = m_port.readAll();
        if (!chunk.isEmpty())
            emit dataReceived(chunk);
    });

    connect(&m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError)
            return;
        emit portError(m_port.errorString());
    });
}

bool SerialPort::openReadWrite()
{
    if (m_port.isOpen())
        m_port.close();
    applyConfig();
    const bool ok = m_port.open(QIODevice::ReadWrite);
    if (!ok)
        emit portError(m_port.errorString());
    return ok;
}

bool SerialPort::openReadOnly()
{
    if (m_port.isOpen())
        m_port.close();
    applyConfig();
    const bool ok = m_port.open(QIODevice::ReadOnly);
    if (!ok)
        emit portError(m_port.errorString());
    return ok;
}

void SerialPort::close()
{
    if (m_port.isOpen())
        m_port.close();
}

bool SerialPort::isOpen() const
{
    return m_port.isOpen();
}

qint64 SerialPort::write(const QByteArray &data)
{
    if (!m_port.isOpen())
        return -1;
    return m_port.write(data);
}

QString SerialPort::toHexSpaced(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    return QString::fromLatin1(data.toHex(' '));
}
```

### 实现要点说明

1. **`setupConnections()` 放在构造函数**  
   保证对象一生成，串口错误与数据到达都有路径通知上层。

2. **`readyRead` 里只做 `readAll()` + `emit`**  
   解析协议、拼帧、`qDebug` 刷屏都应放在**接收槽的下一层**（或节流），否则 512Hz 时界面会卡。

3. **`open*` 里先 `close` 再 `applyConfig` 再 `open`**  
   避免「端口已打开但参数未生效」的歧义；也避免重复 `open` 失败。

4. **`errorOccurred` 里过滤 `NoError`**  
   Qt 有时会发无错误回调，过滤掉可减少误报。

5. **`toHexSpaced` 用 `static`**  
   不依赖串口状态，任何地方都能把 `QByteArray` 打成十六进制字符串。

---

## 五、在界面里怎么用（示例片段）

在 `MainWindow` 里持有 `SerialPort* m_serial` 或成员对象，连接信号即可：

```cpp
#include "device/serialport.h"

// 构造里或初始化函数里：
m_serial = new SerialPort(this);
connect(m_serial, &SerialPort::dataReceived, this, [](const QByteArray &chunk) {
    qDebug().noquote() << "HEX:" << SerialPort::toHexSpaced(chunk);
});
connect(m_serial, &SerialPort::portError, this, [](const QString &msg) {
    qWarning() << "Serial:" << msg;
});

SerialPortConfig cfg;
cfg.portName = QStringLiteral("COM7");
cfg.baudRate = 57600;
m_serial->setConfig(cfg);
m_serial->openReadOnly();
```

**为什么用 `connect` 而不是在 `SerialPort` 里直接 `qDebug`？**  
职责分离：设备类只负责 IO；UI/日志/协议解析各自订阅，后期换 QWT、写文件、换协议都不改串口类。

---

## 六、CMake 与「找不到 QSerialPort」

`find_package(Qt6 … SerialPort)` 之后，**必须** `target_link_libraries(… Qt::SerialPort)`，否则会出现头文件找不到。你工程里若已按前文补全，重新运行 CMake 即可。

---

## 七、和你当前文件对齐时的检查清单

- [ ] `serialport.h`：`class SerialPort : public QObject`，且有 `Q_OBJECT`、`signals:`。  
- [ ] `SerialPortConfig` 若 cpp 里用了 `readBufferSize`，头文件结构体里**要有该字段**。  
- [ ] 构造函数声明与定义一致：`SerialPort::SerialPort(QObject *parent)`。  
- [ ] 类名与文件名：Windows 下文件名大小写不敏感，但**类名 `SerialPort` 与 Qt 的 `QSerialPort` 易混**，注意 include 与成员 `m_port` 类型不要写反。

---

## 八、下一步（解析器）放在哪

本类只发出 **`dataReceived(QByteArray chunk)`**。  
你后续要做的 **双 `0xAA` 组帧 + 校验 + TLV 解析**，应使用**单独的类**（例如 `EegFrameAssembler`），内部维护 `QByteArray m_rxBuffer`，在槽里 `m_rxBuffer.append(chunk)` 后循环切帧。这样 **串口层永远简单、可单测**，协议改了我只改组装器。

---

*文档与示例对应 Qt6；若你使用 Qt5，将 `QSerialPort::errorOccurred` 的槽参数改为 `QSerialPort::SerialPortError` 的旧式写法即可（Qt5.8+ 也有 `errorOccurred` 信号）。*
