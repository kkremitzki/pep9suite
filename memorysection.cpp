#include "memorysection.h"
#include <QApplication>
MemorySection* MemorySection::instance = nullptr;
MemorySection *MemorySection::getInstance()
{
    if(MemorySection::instance == nullptr)
    {
        MemorySection::instance = new MemorySection();
    }
    return MemorySection::instance;
}

MemorySection::MemorySection(QObject *parent) : QObject(parent), waiting(true), inBuffer(), maxBytes(0xffff), iPort(-2), oPort(-4),
    hadMemoryError(false), errorMessage()
{
    initializeMemory();
}

void MemorySection::initializeMemory()
{
    memory = QVector<quint8>(maxBytes);
}

quint8 MemorySection::getMemoryByte(quint16 address) const
{
    if(address == iPort || address == iPort + 1)
    {
        quint8 value;
        waiting = true;
        emit this->charRequestedFromInput();
        while(waiting)
        {
            QApplication::processEvents();
        }
        if(waiting == false && inBuffer.isEmpty())
        {
            hadMemoryError = true;
            errorMessage = "Memory Error: Requested input but received no input.";
        }
        value = inBuffer[0].toLatin1();
        inBuffer.remove(0,1);
        return value;

    }
    return memory[address];
}

quint16 MemorySection::getMemoryWord(quint16 address) const
{
    if(address>0xfffe) return 0;
    return ((quint16)getMemoryByte(address)<<8)|getMemoryByte(address+1);
}

const QVector<quint8> MemorySection::getMemory() const
{
    return memory;
}

bool MemorySection::hadError() const
{
    return hadMemoryError;
}

const QString MemorySection::getErrorMessage()
{
    return errorMessage;
}

void MemorySection::setMemoryByte(quint16 address, quint8 value)
{
    if(address>=maxBytes)
    {
        hadMemoryError = true;
        errorMessage = "Memory Error: Out of bounds access at byte "+QString::number(address,16);
    }
    quint8 old= memory[address];
    if(old == value)return; //Don't continue if the new value is the old value
    onSetMemoryByte(address,value);
}

void MemorySection::setMemoryWord(quint16 address, quint16 value)
{
    address&=0xFFFE; //Memory access ignores the lowest order bit
    quint8 hi=memory[address],lo=memory[address+1]; //Cache old memory values
    if((((quint16)hi<<8)|lo)==value)return; //Don't continue if the new value is the old value
    onSetMemoryWord(address,value);
}

void MemorySection::clearMemory() noexcept
{
    //Set all memory values to 0
    for(int it=0;it<memory.length();it++)
    {
        memory[it]=0;
    }
}

void MemorySection::clearErrors() noexcept
{
    hadMemoryError = false;
    errorMessage = "";
}

void MemorySection::onMemorySizeChanged(quint16 maxBytes)
{
    this->maxBytes = maxBytes;
    initializeMemory();
}

void MemorySection::onIPortChanged(quint16 newIPort)
{
    iPort = newIPort;
}

void MemorySection::onOPortChanged(quint16 newOPort)
{
    oPort = newOPort;
}

void MemorySection::onAppendInBuffer(const QString &newData)
{
    inBuffer.append(newData);
}

void MemorySection::onCancelWaiting()
{
    waiting = false;
}

void MemorySection::onSetMemoryByte(quint16 address, quint8 val)
{
    if(address>=maxBytes)
    {
        hadMemoryError = true;
        errorMessage = "Memory Error: Out of bounds access at byte "+QString::number(address,16);
    }
    quint8 old = memory[address];
    if(address == oPort || address == oPort + 1)
    {
        emit this->charWrittenToOutput(val);
    }
    memory[address] = val;
    if(old != val) emit memoryChanged(address,old,val);
}

void MemorySection::onSetMemoryWord(quint16 address, quint16 val)
{
    //Do not enforce aligned memory access here, as precondition code in homeworks depends on access being unaligned.
    onSetMemoryByte(address,val/256);
    onSetMemoryByte(address+1, val%256);

}

void MemorySection::onClearMemory() noexcept
{
    //On memory reset, only clear RAM
    clearMemory();
    clearErrors();
}