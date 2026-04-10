#include "thinkgearpayloadparser.h"

ThinkGearPayloadParser::ThinkGearPayloadParser(QObject *parent)
    : QObject{parent}
{}
bool ThinkGearPayloadParser::extractPayload(const QByteArray &frame,
                    QByteArray &payloadOut,QString *errorMsg)
{
    payloadOut.clear();
    if(frame.size()<4)
    {
        if(errorMsg)
            *errorMsg=QStringLiteral("frame too short");
        return false;
    }
    if(static_cast<quint8>(frame[0]) != 0xAA ||
        static_cast<quint8>(frame[1]) != 0xAA){
        if(errorMsg)
            *errorMsg=QStringLiteral("bad sync");
        return false;
    }
    const int n =static_cast<unsigned char>(frame[2]);
    if(n==0 ||n>170)
    {
        if(errorMsg)
            *errorMsg=QStringLiteral("invalid payload length");
        return false;
    }
    if(frame.size()!=4+n)
    {
        if(errorMsg)
            *errorMsg=QStringLiteral("frame size mismatch");
        return false;
    }
    payloadOut=frame.sliced(3,n);
    return true;
}
void ThinkGearPayloadParser::parseFrame(const QByteArray &frame)
{
    QByteArray payload;
    QString err;
    if(!extractPayload(frame,payload,&err))
    {
        emit parseWarning(err);
        return;
    }
    parsePayload(payload);
}
static quint32 readU24Be(const char* p)
{
    const auto b0 =static_cast<quint32>(static_cast<quint8>(p[0]));
    const auto b1 =static_cast<quint32>(static_cast<quint8>(p[1]));
    const auto b2 =static_cast<quint32>(static_cast<quint8>(p[2]));
    return (b0<<16)|(b1<<8)|b2;
}
void ThinkGearPayloadParser::parsePayload(const QByteArray &payload)
{
    int i=0;
    const int n=payload.size();
    while (i<n) {
        const auto code=static_cast<unsigned char>(
            payload[i]);
        switch(code)
        {
        case 0x02:  //信号质量
            if(i+1>=n)
            {
                emit parseWarning(QStringLiteral(
                    "truncated 0x02"));
                return;
            }
            emit signalQualityReceived(static_cast<quint8>(payload[i+1]));
            i+=2;
            break;
        case 0x04:  //注意力
            if(i+1>=n){
                emit parseWarning(QStringLiteral(
                    "truncated 0x04"));
                return;
            }
                emit attentionReceived(static_cast<quint8>(payload[i+1]));
                i+=2;
                break;
        case 0x05:  //冥想
                if (i + 1 >= n) {
                    emit parseWarning(QStringLiteral("truncated 0x05"));
                    return;
                }
                emit meditationReceived(static_cast<quint8>(payload[i + 1]));
                i += 2;
                break;

         case 0x16:  //眨眼强度
                if (i + 1 >= n) {
                    emit parseWarning(QStringLiteral("truncated 0x16"));
                    return;
                }
                emit blinkReceived(static_cast<quint8>(payload[i + 1]));
                i += 2;
                break;
         case 0x80:
         {             //raw,大端
             if(i+2>=n){
                 emit parseWarning(QStringLiteral("truncated 0x80"));
                 return;
             }

             const auto hi=static_cast<quint16>(static_cast<quint8>(payload[i+1]));
             const auto lo=static_cast<quint16>(static_cast<quint8>(payload[i+2]));
             const quint16 u=static_cast<quint16>((hi<<8|lo));
             emit rawReceived(static_cast<qint16>(u));
             i+=3;

             }
         break;
        case 0x83:
        { // 8 段 × 3 字节大端；与常见解析下标 i+2…i+25 一致
            if(i+25>=n)
            {
                emit parseWarning(QStringLiteral("truncated 0x83"));
                return;
            }
            ThinkGearEegPower p;
            const char*base=payload.constData()+i;
            p.delta = readU24Be(base + 2);
            p.theta = readU24Be(base + 5);
            p.lowAlpha = readU24Be(base + 8);
            p.highAlpha = readU24Be(base + 11);
            p.lowBeta = readU24Be(base + 14);
            p.highBeta = readU24Be(base + 17);
            p.lowGamma = readU24Be(base + 20);
            p.midGamma = readU24Be(base + 23);
            emit eegPowerReceived(p);
            i+=26;
            break;}
        default:
            ++i;
            break;
        }


    }
}
