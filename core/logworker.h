#ifndef LOGWORKER_H
#define LOGWORKER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include "core/DataBuffer.h"
class LogWorker : public QObject
{
    Q_OBJECT
public:
    explicit LogWorker(QObject *parent = nullptr);
//slots是信号槽的处理函数
//siganls是信号槽的发送函数emit发送
public slots:
    void appendRawSample(const RawSample &s);
    void appendBigPackageSmaple(const BigPackageSample &s);
    void startCapture();
    // 中文注释：停止采集（点击“停止”后冻结图像）
    void stopCapture();
    // 中文注释：清空缓存并通知 UI 清图（点击“清除”）
    void clearData();
private slots:
    void onTimerTick();

signals:
   // void historyDataReady(const PlotDataChunk &chunk);
private:
    DataBuffer m_dataBuffer;
    //QTimer m_timer;

    bool m_captureEnabled=false;

};

#endif // LOGWORKER_H
