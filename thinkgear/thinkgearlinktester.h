#ifndef THINKGEARLINKTESTER_H
#define THINKGEARLINKTESTER_H

#include <QObject>
#include <QTimer>
#include <QtGlobal>
#include "core/pictureandalgbuffer.h"
#include "core/logwbuffer.h"
class SerialPort;
class ThinkGearFrameAssembler;
class ThinkGearPayloadParser;
class RawtOutUvProcessor;

class ThinkGearLinkTester : public QObject
{
    Q_OBJECT
public:
    explicit ThinkGearLinkTester(QObject *parent = nullptr);
    ~ThinkGearLinkTester() override;

    bool start(const QString &portName,qint32 baudRate=57600);
    void stop();

    void setTargetRawPerSec(int v){ m_targetRawPerSec =v;}
    void setTolerance(int v){m_tolerance=v;}

signals:
    void secondReport(quint64 secIndex,
                      int rawCountSec,
                      int frameCountPerSec,
                      int warningCountPerSec,
                      bool pass);
    void testMessage(const QString &msg);



private slots:
    void onRawUvReady(double uv);
    void onFrameReady(const QByteArray &frame);
    void onParseWarning(const QString &msg);
    void onPortError(const QString &msg);
    void onTick1s();
private:
    PictureAndALgBuffer m_pictureandalgBuffer{128*40};
    LogBuffer m_logBuffer{4096};
    SerialPort* m_serial=nullptr;
    ThinkGearFrameAssembler *m_assembler=nullptr;
    ThinkGearPayloadParser *m_parser=nullptr;
    RawtOutUvProcessor *m_converter =nullptr;

    QTimer m_tick;

    // 每秒计数器
    int m_rawCountSec = 0;
    int m_frameCountSec = 0;
    int m_warningCountSec = 0;
    quint64 m_secIndex = 0;

    // 阈值
    int m_targetRawPerSec = 256;
    int m_tolerance = 30 ;

    quint64 m_seq=0;
    int m_algBlocksSec =0;//计数块
    int m_plotBlocksSec=0;




};

#endif // THINKGEARLINKTESTER_H
