#ifndef PICTUREANDALGBUFFER_H
#define PICTUREANDALGBUFFER_H
#include<array>
#include<cstdint>
#include <QVector>
#include<QMutex>
enum class Band : int {
    Delta = 0,
    Theta,
    Alpha1,
    Alpha2,
    Beta1,
    Beta2,
    Gamma1,
    Gamma2,
    Count
};
struct BigPackageSample {
    std::array<double, static_cast<int>(Band::Count)> band{};
    short signal = 0;
    short att = 0;
    short med = 0;
    QString time = 0;
    uint64_t raw_count_at_update = 0; // 关键：记录“大包更新时的RAW计数”
};

struct RawSample {
    double raw = 0.0;   // μV
    QString time = 0;
    uint64_t raw_count = 0;
};

class PictureAndALgBuffer
{
public:
    explicit PictureAndALgBuffer(int m_RawValue_capacity=512*5);//,int m_BigPackage_capacity=1*5);

    void appendRawvalue(const RawSample &s);
    //void appendBigpackage(const BigPackageSample &s);
    QVector<RawSample> window_RawSample() const;
    // QVector<BigPackageSample> window_BigPackageSample() const;
    // 新增：最近 N 点（给绘图/算法）
    //QVector<RawSample> recentRaw(int count) const;
    // 新增：按序号窗口（给算法重叠窗）
    //QVector<RawSample> rawWindowBySeq(uint64_t endSeqInclusive, int count) const;
    //uint64_t latestRawSeq() const;
    //int rawCapacity() const { return RawValue_capacity; }
    bool tryDequeueRawChunkForAlgo(int chunkSize, QVector<RawSample> &outChunk);
    bool tryDequeueRawChunkForPlot(int chunkSize, QVector<RawSample> &outChunk);

    void resetAlgoCursor();
    void resetPlotCursor();
    void clear();
private:
    QVector<RawSample> rawOrderedUnsafe() const;
    static bool tryDequeueByCursor(const QVector<RawSample> &all,
                                   quint64 &cursorSeq,
                                   int chunkSize,
                                   QVector<RawSample> &outChunk);
    int RawValue_capacity =0;
    //int BigPackage_capacity=0;
    QVector<RawSample> m_raw;
   // QVector<BigPackageSample> m_bigPackage;
    int RawValue_writeIndex=0;
    //int BigPackage_writeIndex=0;
    bool RawValue_filled=false;
    quint64 m_algoReadSeq = 0;
    quint64 m_plotReadSeq = 0;
    //bool BigPackage_filled=false;
    //uint64_t m_latestRawSeq =0;
    mutable QMutex m_mutex;

};

#endif // PICTUREANDALGBUFFER_H
