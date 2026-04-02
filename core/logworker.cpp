#include "logworker.h"

LogWorker::LogWorker(QObject *parent)
    : QObject{parent}
    ,m_dataBuffer(5*512,1*5)
{
    //时间驱动ui后期再做
}
void LogWorker::appendRawSample(const RawSample &s)
{
    if(!m_captureEnabled)
    {
        return;
    }
    m_dataBuffer.appendRawvalue(s);
}
void LogWorker::appendBigPackageSmaple( const BigPackageSample&s)
{
    if(!m_captureEnabled)
    {
        return;
    }
    m_dataBuffer.appendBigpackage(s);
}
void LogWorker::startCapture()
{
    m_dataBuffer.clear();
    m_captureEnabled=true;
}
void LogWorker::stopCapture()
{
    m_captureEnabled=false;
}
void LogWorker::clearData()
{
    m_captureEnabled=false;
    m_dataBuffer.clear();
}
