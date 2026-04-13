#include "csvlogworker.h"
#include "logwbuffer.h" // 需要完整类型：LogBuffer / LogItem

#include <QDateTime>
#include <QStringConverter>
#include <QThread>
CsvLogWorker::CsvLogWorker(LogBuffer *buffer,QObject *parent)
    : QObject{parent}
    ,m_buf(buffer)
{
    m_timer.setParent(this); // 关键：让 timer 归属于 worker 对象
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer,&QTimer::timeout,this,&CsvLogWorker::onFlushTick);

}
void CsvLogWorker::start()
{
    if (QThread::currentThread() != thread()) {
        emit workerError(QStringLiteral("CsvLogWorker::start 线程不一致"));
        return;}
    if(!m_buf)
    {
        emit workerError(QStringLiteral("CsvLogWorker: buffer is null"));
        return;
    }
    if(m_csvPath.isEmpty())
    {
        //运行时会自动生成类似这样的文件名：
        //raw_20251225_153025.csv
        m_csvPath=QString("raw_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    }
    if(m_file.isOpen())m_file.close();

    m_file.setFileName(m_csvPath);
    //以「只写、追加、文本」模式尝试打开文件
    if(!m_file.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text)){
        emit workerError(QStringLiteral("CsvLogWorker:open failed:%1").arg(m_file.errorString()));
        return;
    }
    m_out.setDevice(&m_file);//设置输出流对象为m_file
    m_out.setEncoding(QStringConverter::Utf8);

    m_headerWritten=false;
    writeHeaderIfNeeded();

    m_timer.start(m_flushIntervalMs);
    emit workerInfo(QStringLiteral("CsvWorkLoger started,path=%1").arg(m_csvPath));

}
void CsvLogWorker::stop()
{
    if(m_timer.isActive())
        m_timer.stop();
     // 最后再冲一次，把残余队列尽量写完
    onFlushTick();
    if(m_file.isOpen()){
        m_out.flush();
        m_file.flush();
        m_file.close();
    }
    emit workerInfo(QStringLiteral("CsvLogWorker stopped"));

}
//写顶头
void CsvLogWorker::writeHeaderIfNeeded()
{
    if(!m_file.isOpen())
    {
        return;
    }
    if(m_headerWritten)
        return;
    if(m_file.size()==0)
    {
        m_out<<"tsMS,seq,rawInt16,rawUv\n";
        m_out.flush();
    }
    m_headerWritten=true;
}
void CsvLogWorker::writeItem(const LogItem &it)
{
    // 纯数值列，便于 Excel / Python
    m_out << it.tsMs << ','
          << it.seq << ','
          << it.rawInt16 << ','
          << it.rawUv
          << '\n';
}

void CsvLogWorker::onFlushTick()
{
    if (!m_buf || !m_file.isOpen())
        return;

    writeHeaderIfNeeded();

    // drainBatch：把队列里的元素“取走”（从队列移除）——写完即舍弃缓存的关键
    const QList<LogItem> batch = m_buf->drainBatch(m_batchSize);
    if (batch.isEmpty())
        return;

    for (const LogItem &it : batch)
        writeItem(it);

    // 每批 flush 一次即可（不要每条 flush）
    m_out.flush();
    m_file.flush();

    emit drained(batch.size(), m_buf->size(), m_buf->droppedCount());
}






