#ifndef LOGWBUFFER_H
#define LOGWBUFFER_H


#include <QTimer>
#include <QString>
#include <QQueue>
#include <QMutex>
#include <QtGlobal>
struct LogItem {
    QString tsMs ;
    quint64 seq = 0;
    qint16 rawInt16 = 0;
    double rawUv = 0.0;
};
class LogBuffer
{
public:
    explicit LogBuffer(int capacity =4096);
    bool push(const LogItem &item);              // 满队列返回 false
    QList<LogItem> drainBatch(int maxItems);     // writer 线程批量取
    int droppedCount() const;
    int size() const;
private:
    int m_capacity = 0;
    mutable QMutex m_mutex;
    QQueue<LogItem> m_q;
    int m_dropped = 0;
};

#endif // LOGWBUFFER_H
