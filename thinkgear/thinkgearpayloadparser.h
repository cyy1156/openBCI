#ifndef THINKGEARPAYLOADPARSER_H
#define THINKGEARPAYLOADPARSER_H
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QObject>

struct ThinkGearEegPower
{
    quint32 delta = 0;
    quint32 theta = 0;
    quint32 lowAlpha = 0;
    quint32 highAlpha = 0;
    quint32 lowBeta = 0;
    quint32 highBeta = 0;
    quint32 lowGamma = 0;
    quint32 midGamma = 0;
};
class ThinkGearPayloadParser : public QObject
{
    Q_OBJECT
public:
    explicit ThinkGearPayloadParser(QObject *parent = nullptr);
public slots:
    void parseFrame(const QByteArray &frame);

signals:
    void rawReceived(qint16 value);


    /** 0x80：16 位原始波形（大端，按 qint16 解释） */


    void signalQualityReceived(quint8 value);   // 0x02，常见范围 0–200
    void attentionReceived(quint8 value);       // 0x04，0–100
    void meditationReceived(quint8 value);      // 0x05，0–100
    void blinkReceived(quint8 value);           // 0x16，1–255

    void eegPowerReceived(const ThinkGearEegPower &power); // 0x83

    void parseWarning(const QString &message);
private:
    static bool extractPayload(const QByteArray &frame,
                               QByteArray &payloadOut,QString *errorMsg);
    void parsePayload(const QByteArray &payload);

};

#endif // THINKGEARPAYLOADPARSER_H
