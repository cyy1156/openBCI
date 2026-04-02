#include "databuffer.h"

DataBuffer::DataBuffer(int m_RawValue_capacity,int m_BigPackage_capacity)
    :RawValue_capacity(m_RawValue_capacity),
    BigPackage_capacity(m_BigPackage_capacity)
{
    if(RawValue_capacity<=0)
    {
        RawValue_capacity = 1;

    }
    if(BigPackage_capacity<=0)
    {
        BigPackage_capacity=1;
    }
    m_raw.resize(RawValue_capacity);
    m_bigPackage.resize(BigPackage_capacity);

}
void DataBuffer::appendRawvalue(const RawSample &s)
{
    m_raw[RawValue_writeIndex]=s;
    RawValue_writeIndex++;
    if(RawValue_writeIndex>=RawValue_capacity)
    {
        RawValue_writeIndex =0;
        RawValue_filled=true;
    }
}
void DataBuffer::appendBigpackage(const BigPackageSample & s)
{
    m_bigPackage[BigPackage_writeIndex]=s;
    BigPackage_writeIndex++;
    if(BigPackage_writeIndex>=BigPackage_capacity)
    {
        BigPackage_writeIndex=0;
        BigPackage_filled=true;
    }
}
QVector<RawSample> DataBuffer::window_RawSample() const
{
    if(!RawValue_filled)
    {
        const int count=RawValue_writeIndex;
        QVector<RawSample> out;
        out.reserve(count);
        for(int i=0;i<count;i++)
        {
            out.push_back(m_raw[i]);
        }
        return out;
    }

    QVector<RawSample> out;
    out.reserve(RawValue_capacity);
    for(int i=0;i<RawValue_capacity;++i)
    {
        const int idx=(RawValue_writeIndex+i)%RawValue_capacity;
        out.push_back(m_raw[idx]);
    }
    return out;

}
QVector<BigPackageSample> DataBuffer::window_BigPackageSample() const
{
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
}
void DataBuffer::clear()
{
    RawValue_writeIndex=0;
    BigPackage_writeIndex=0;
    RawValue_filled=false;
    BigPackage_filled=false;

}







