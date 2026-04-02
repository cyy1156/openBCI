#ifndef DATABUFFER_H
#define DATABUFFER_H
#include<array>
#include<cstdint>
#include <QVector>
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
    uint64_t time = 0;
    uint64_t raw_count_at_update = 0; // 关键：记录“大包更新时的RAW计数”
};

struct RawSample {
    double raw = 0.0;   // μV
    uint64_t time = 0;
    uint64_t raw_count = 0;
};

class DataBuffer
{
public:
    explicit DataBuffer(int m_RawValue_capacity=512*5,int m_BigPackage_capacity=1*5);

    void appendRawvalue(const RawSample &s);
    void appendBigpackage(const BigPackageSample &s);
    QVector<RawSample> window_RawSample() const;
    QVector<BigPackageSample> window_BigPackageSample() const;


    void clear();
private:
    int RawValue_capacity =0;
    int BigPackage_capacity=0;
    QVector<RawSample> m_raw;
    QVector<BigPackageSample> m_bigPackage;
    int RawValue_writeIndex=0;
    int BigPackage_writeIndex=0;
    bool RawValue_filled=false;
    bool BigPackage_filled=false;

};

#endif // DATABUFFER_H
