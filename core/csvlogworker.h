#ifndef CSVLOGWORKER_H
#define CSVLOGWORKER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QString>
class LogBuffer;
struct LogItem;

class CsvLogWorker : public QObject
{
    Q_OBJECT
public:
    explicit CsvLogWorker(LogBuffer* buffer,QObject *parent = nullptr);
    void setOutputPath(const QString &csvPath){m_csvPath=csvPath;}
    void setFlushIntervalMs(int ms) { m_flushIntervalMs = ms; }
    void setBatchSize(int n) { m_batchSize = n; }

public slots:
    void start();   // 打开文件 + 启动定时拉取
    void stop();    // 停止定时器 + 关闭文件

signals:
    void workerError(const QString &msg);
    void workerInfo(const QString &msg);
    void drained(int items, int queueSizeAfter, int droppedCount);

private slots:
    void onFlushTick();

private:
    void writeHeaderIfNeeded();
    void writeItem(const LogItem &it);

private:
    LogBuffer *m_buf=nullptr;

    QString m_csvPath;//定义名字
    QFile m_file;
    QTextStream m_out;//对象按“文本流”的方式写进文件
    QTimer m_timer;
    int m_flushIntervalMs =100;//50~100ms
    int m_batchSize =512;   //每次最多写多少条
    bool m_headerWritten=false;

};

#endif // CSVLOGWORKER_H
