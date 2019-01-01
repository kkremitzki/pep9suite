#ifndef FULLMICROCODEMEMOIZER_H
#define FULLMICROCODEMEMOIZER_H

#include <QString>
#include "enu.h"
class CPUControlSection;
struct CPURegisterState
{
    quint16 reg_PC_start = 0, reg_PC_end = 0;
    quint16 reg_A = 0, reg_X = 0, reg_SP = 0, reg_OS = 0;
    quint8 reg_IR = 0, bits_NZVCS = 0, reg_IS_start = 0;
};

struct callStack
{
    quint16 greatest_SP = 0, least_SP = 0;
};

struct CPUState
{
    CPURegisterState regState = CPURegisterState();
  //QVector<callStack> call_tracer;
};
class PartialMicrocodedCPU;
class PartialMicrocodedMemoizer
{
public:
    explicit PartialMicrocodedMemoizer(PartialMicrocodedCPU& item);
    Enu::DebugLevels getDebugLevel() const;

    void clear();
    void storeStateInstrEnd();
    void storeStateInstrStart();
    QString memoize();
    QString finalStatistics();
    void setDebugLevel(Enu::DebugLevels level);
    quint8 getRegisterByteStart(Enu::CPURegisters reg) const;
    quint16 getRegisterWordStart(Enu::CPURegisters reg) const;
    bool getStatusBitStart(Enu::EStatusBit bit) const;

private:
    PartialMicrocodedCPU& cpu;
    CPUState registers;
    Enu::DebugLevels level;
    QString formatNum(quint16 number);
    QString formatNum(quint8 number);
    QString formatAddress(quint16 address);
};

#endif // FULLMICROCODEMEMOIZER_H
