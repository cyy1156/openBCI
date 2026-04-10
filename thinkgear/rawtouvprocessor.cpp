#include "rawtouvprocessor.h"

#include <QDateTime>

RawtOutUvProcessor::RawtOutUvProcessor(QObject *parent)
    : QObject(parent)
{
}

void RawtOutUvProcessor::onRaw(qint16 rawValue)
{
    const UvSample s = buildSample(rawValue);
    emit uvSampleReady(s);
    emit uvValueReady(s.rawUv);
}

void RawtOutUvProcessor::onRawWithMeta(qint16 rawValue)
{
    const UvSample s = buildSample(rawValue);
    emit uvSampleReady(s);
    emit uvValueReady(s.rawUv);
}

UvSample RawtOutUvProcessor::buildSample(qint16 rawValue)
{
    UvSample s;
    s.timestampMs = QDateTime::currentMSecsSinceEpoch();
    s.seq = ++m_seq;
    s.rawInt16 = rawValue;

    // uV 转换：raw * (uV/count) * 标定系数
    s.rawUv = static_cast<double>(rawValue) * m_uvPerCount * m_gainAdjust;


    return s;
}
