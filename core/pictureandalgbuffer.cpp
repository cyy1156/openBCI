#include "pictureandalgbuffer.h"

PictureAndALgBuffer::PictureAndALgBuffer(int m_RawValue_capacity)//,int m_BigPackage_capacity)
    :RawValue_capacity(m_RawValue_capacity)
   // BigPackage_capacity(m_BigPackage_capacity)
{
    if(RawValue_capacity<=0)
    {
        RawValue_capacity = 1;

    }
   /* if(BigPackage_capacity<=0)
    {
        BigPackage_capacity=1;
    }*/
    m_raw.resize(RawValue_capacity);
    /*
    m_bigPackage.resize(BigPackage_capacity);*/

}
void PictureAndALgBuffer::appendRawvalue(const RawSample &s)
{
    QMutexLocker locker(&m_mutex);
    m_raw[RawValue_writeIndex]=s;
    RawValue_writeIndex++;
    if(RawValue_writeIndex>=RawValue_capacity)
    {
        RawValue_writeIndex =0;
        RawValue_filled=true;
    }
}
/*void PictureAndALgBuffer::appendBigpackage(const BigPackageSample & s)
{
    QMutexLocker locker(&m_mutex);
    m_bigPackage[BigPackage_writeIndex]=s;
    BigPackage_writeIndex++;
    if(BigPackage_writeIndex>=BigPackage_capacity)
    {
        BigPackage_writeIndex=0;
        BigPackage_filled=true;
    }
}
*/
QVector<RawSample> PictureAndALgBuffer::rawOrderedUnsafe() const//把当前环形缓冲里的有效 RAW 数据
{
    QVector<RawSample> out;
    if(!RawValue_filled){
        out.reserve(RawValue_writeIndex);
        for(int i=0;i<RawValue_writeIndex;i++)
        {
            out.push_back((m_raw[i]));

        }
        return out;

    }
    out.reserve(RawValue_capacity);
    for(int i=0;i<RawValue_capacity;i++)
    {const int idx = (RawValue_writeIndex + i) % RawValue_capacity;
        out.push_back(m_raw[idx]);}
    return out;

}
//通过游标来截取chucksize个数据
bool PictureAndALgBuffer::tryDequeueByCursor(const QVector<RawSample> &all,
                                    quint64 &cursorSeq,
                                    int chunkSize,
                                    QVector<RawSample> &outChunk)
{
    outChunk.clear();
    if(all.isEmpty()||chunkSize <=0)return false;
    if(cursorSeq ==0)cursorSeq=all.first().raw_count;
    int begin =-1;
    for(int i=0;i<all.size();i++)
    {
        if(all[i].raw_count>=cursorSeq){begin =i;break;}

    }
    if(begin<0)return false;
    const int available =all.size()-begin;
    if(available<chunkSize)return false;

    outChunk=all.sliced(begin,chunkSize);
    cursorSeq=outChunk.last().raw_count+1;
    return true;

}
bool PictureAndALgBuffer::tryDequeueRawChunkForAlgo(int chunkSize, QVector<RawSample> &outChunk)
{
    QMutexLocker locker(&m_mutex);
    return tryDequeueByCursor(rawOrderedUnsafe(), m_algoReadSeq, chunkSize, outChunk);
}
bool PictureAndALgBuffer::tryDequeueRawChunkForPlot(int chunkSize, QVector<RawSample> &outChunk)
{
    QMutexLocker locker(&m_mutex);
    return tryDequeueByCursor(rawOrderedUnsafe(), m_plotReadSeq, chunkSize, outChunk);
}
void PictureAndALgBuffer::resetAlgoCursor()
{
    QMutexLocker locker(&m_mutex);
    m_algoReadSeq = 0;
}

void PictureAndALgBuffer::resetPlotCursor()
{
    QMutexLocker locker(&m_mutex);
    m_plotReadSeq = 0;
}
QVector<RawSample> PictureAndALgBuffer::window_RawSample() const
{
    QMutexLocker locker(&m_mutex);
    return rawOrderedUnsafe();
}
/*QVector<BigPackageSample> PictureAndALgBuffer::window_BigPackageSample() const
{
    QMutexLocker locker(&m_mutex);
    if(!BigPackage_filled)
    {
        const int count=BigPackage_writeIndex;
        QVector<BigPackageSample> out;
        out.reserve(count);
        for(int i=0;i<count;i++)
        {
            out.push_back(m_bigPackage[i]);
        }
        return out;
    }

    QVector<BigPackageSample> out;
    out.reserve(BigPackage_capacity);
    for(int i=0;i<BigPackage_capacity;++i)
    {
        const int idx=(BigPackage_writeIndex+i)%BigPackage_capacity;
        out.push_back(m_bigPackage[idx]);
    }
    return out;
}*/


void PictureAndALgBuffer::clear()
{
    QMutexLocker locker(&m_mutex);
    RawValue_writeIndex=0;
    //BigPackage_writeIndex=0;
    RawValue_filled=false;
    //BigPackage_filled=false;
    m_algoReadSeq = 0;
    m_plotReadSeq = 0;
}







