#ifndef SERIALPORT_H
#define SERIALPORT_H
#include <QSerialPort>
#include <QObject>
#include <QString>
#include <QByteArray>
struct SerialPortConfig{
    QString portName="COM7";
    qint32 baudRate = 57600;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    int readBufferSize =32*1024;

};

class SerialPort :public QObject
{
    Q_OBJECT
public:
    explicit SerialPort(QObject *parent = nullptr);
    ~SerialPort() override;
    void setPortName(const QString& name);
    void setBaudRate(qint32 baud); // 与 SerialPortConfig::baudRate、QSerialPort::setBaudRate 一致
    void setConfig(const SerialPortConfig& cfg);
    SerialPortConfig config() const {return m_cfg;}

    bool openReadOnly();
    bool openReadWrite();
    void close();
    bool isOpen() const ;
    qint64 write(const QByteArray &data);
    // 调试：空格分隔十六进制，例如 "aa aa 04"
     static QString toHexSpaced(const QByteArray &data);

signals:
    void dataReceived(const QByteArray &chunk);
    void portError(const QString &message);

private:
    void setupConnections();
    void applyConfig();
private:
     QSerialPort m_port; //串口
     SerialPortConfig m_cfg;
};

#endif // SERIALPORT_H
