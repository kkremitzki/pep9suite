#include "isacpu.h"
#include "acpumodel.h"
#include "amemorydevice.h"
#include "pep.h"
#include <functional>
#include <QApplication>
#include "asmprogrammanager.h"
#include "asmprogram.h"

ISACPU::ISACPU(const AsmProgramManager *manager, QSharedPointer<AMemoryDevice> memDevice, QObject *parent):
    ACPUModel(memDevice, parent), InterfaceISACPU(memDevice.get(), manager)
{

}

ISACPU::~ISACPU()
{

}


void ISACPU::stepOver()
{
    // Clear at start, so as to preserve highlighting AFTER finshing a write.
    memory->clearBytesWritten();
    int localCallDepth = getCallDepth();
    do{
        onISAStep();
    } while(localCallDepth < getCallDepth()
            && !getExecutionFinished()
            && !stoppedForBreakpoint()
            && !hadErrorOnStep());

}

bool ISACPU::canStepInto() const
{
    quint8 byte;
    memory->getByte(getCPURegWordStart(Enu::CPURegisters::PC), byte);
    Enu::EMnemonic mnemon = Pep::decodeMnemonic[byte];
    return (mnemon == Enu::EMnemonic::CALL) || Pep::isTrapMap[mnemon];
}

void ISACPU::stepInto()
{
    // Clear at start, so as to preserve highlighting AFTER finshing a write.
    memory->clearBytesWritten();
    onISAStep();
}

void ISACPU::stepOut()
{
    // Clear at start, so as to preserve highlighting AFTER finshing a write.
    memory->clearBytesWritten();
    int localCallDepth = getCallDepth();
    do{
        onISAStep();
    } while(localCallDepth <= getCallDepth()
            && !getExecutionFinished()
            && !stoppedForBreakpoint()
            && !hadErrorOnStep());
}

bool ISACPU::getOperandSpec(quint16 operand, Enu::EAddrMode addrMode, quint16 &opVal)
{
    return decodeOperandSpec(operand, addrMode, &AMemoryDevice::getWord, opVal);
}

void ISACPU::onISAStep()
{
    quint16 opSpec, pc = registerBank.readRegisterWordCurrent(Enu::CPURegisters::PC);
    quint8 is;
    bool okay = memory->readByte(pc, is);
    pc += 1;
    Enu::EMnemonic mnemon = Pep::decodeMnemonic[is];
    Enu::EAddrMode addrMode;

    if(Pep::isTrapMap[mnemon]) {
        executeTrap(mnemon);
    }
    else if(Pep::isUnaryMap[mnemon]){
        executeUnary(mnemon);
    }
    else {
        okay &= memory->readWord(pc, opSpec);
        addrMode = Pep::decodeAddrMode[is];
        pc += 2;
        executeNonunary(mnemon, opSpec, addrMode);
    }
}

void ISACPU::updateAtInstructionEnd()
{

}

bool ISACPU::readOperandSpec(quint16 operand, Enu::EAddrMode addrMode, quint16 &opVal)
{
    return decodeOperandSpec(operand, addrMode, &AMemoryDevice::readWord, opVal);
}

void ISACPU::initCPU()
{

}

bool ISACPU::getStatusBitCurrent(Enu::EStatusBit statusBit) const
{
    quint8 NZVCSbits = registerBank.readStatusBitsCurrent();
    switch(statusBit)
    {
    // Mask out bit of interest, then convert to bool
    case Enu::STATUS_N:
        return(NZVCSbits & Enu::NMask);
    case Enu::STATUS_Z:
        return(NZVCSbits & Enu::ZMask);
    case Enu::STATUS_V:
        return(NZVCSbits & Enu::VMask);
    case Enu::STATUS_C:
        return(NZVCSbits & Enu::CMask);
    case Enu::STATUS_S:
        return(NZVCSbits & Enu::SMask);
    default:
        // Should never occur, but might happen if a bad status bit is passed
        return false;
    }
}

bool ISACPU::getStatusBitStart(Enu::EStatusBit statusBit) const
{
    quint8 NZVCSbits = registerBank.readStatusBitsStart();
    switch(statusBit)
    {
    // Mask out bit of interest, then convert to bool
    case Enu::STATUS_N:
        return(NZVCSbits & Enu::NMask);
    case Enu::STATUS_Z:
        return(NZVCSbits & Enu::ZMask);
    case Enu::STATUS_V:
        return(NZVCSbits & Enu::VMask);
    case Enu::STATUS_C:
        return(NZVCSbits & Enu::CMask);
    case Enu::STATUS_S:
        return(NZVCSbits & Enu::SMask);
    default:
        // Should never occur, but might happen if a bad status bit is passed
        return false;
    }
}

quint8 ISACPU::getCPURegByteCurrent(Enu::CPURegisters reg) const
{
    return registerBank.readRegisterByteCurrent(reg);
}

quint16 ISACPU::getCPURegWordCurrent(Enu::CPURegisters reg) const
{
    return registerBank.readRegisterWordCurrent(reg);
}

quint8 ISACPU::getCPURegByteStart(Enu::CPURegisters reg) const
{
    return registerBank.readRegisterByteStart(reg);
}

quint16 ISACPU::getCPURegWordStart(Enu::CPURegisters reg) const
{
    return registerBank.readRegisterWordStart(reg);
}

QString ISACPU::getErrorMessage() const noexcept
{
    if(memory->hadError()) return memory->getErrorMessage();
    // else if(data->hadErrorOnStep()) return data->getErrorMessage();
    else if(hadErrorOnStep()) return errorMessage;
    else return "";
}

bool ISACPU::hadErrorOnStep() const noexcept
{
    return memory->hadError() || controlError;
}

bool ISACPU::stoppedForBreakpoint() const noexcept
{
    return asmBreakpointHit;
}

void ISACPU::onSimulationStarted()
{
#pragma message("TODO")
}

void ISACPU::onSimulationFinished()
{
    #pragma message("TODO")
}

void ISACPU::onDebuggingStarted()
{
    #pragma message("TODO")
}

void ISACPU::onDebuggingFinished()
{
    #pragma message("TODO")
}

void ISACPU::onCancelExecution()
{
    #pragma message("TODO")
}

bool ISACPU::onRun()
{
    timer.start();
    // If debugging, there is the potential to hit breakpoints, so a different main loop is needed.
    // Partially, this is to handle breakpoints gracefully, and partially to prevent "run" mode from being slowed down by debug features.
    if(inDebug) {
        // Always execute at least once, otherwise cannot progress past breakpoints
        do {
            // Since the sim runs at about 5Mhz, do not process events every single cycle to increase performance.
            if(asmInstructionCounter % 500 == 0) {
                QApplication::processEvents();
            }
            onISAStep();
        } while(!hadErrorOnStep() && !executionFinished && !(asmBreakpointHit));
    }
    else {
        while(!hadErrorOnStep() && !executionFinished) {
            // Since the sim runs at about 5Mhz, do not process events every single cycle to increase performance.
            if(asmInstructionCounter % 500 == 0) {
                QApplication::processEvents();
            }
            onISAStep();
        }
    }

    //If there was an error on the control flow
    if(hadErrorOnStep()) {
        if(memory->hadError()) {
            qDebug() << "Memory section reporting an error";
            //emit simulationFinished();
            return false;
        }
        else {
            qDebug() << "Control section reporting an error";
            //emit simulationFinished();
            return false;
        }
    }

    // If a breakpoint was reached, return before final statistics are computed or the simulation is finished.
    if(asmBreakpointHit) {
        return false;
    }

    auto value = timer.elapsed();
    //qDebug().nospace().noquote() << memoizer->finalStatistics() << "\n";
    qDebug().nospace().noquote() << "Executed "<< asmInstructionCounter << " instructions.";
    qDebug().nospace().noquote() << "Execution time (ms): " << value;
    qDebug().nospace().noquote() << "Instructions per second: " << asmInstructionCounter / (((float)value/1000));
    return true;
}

void ISACPU::onResetCPU()
{
    InterfaceISACPU::reset();
    registerBank.clearRegisters();
    registerBank.clearStatusBits();
}

bool ISACPU::decodeOperandSpec(quint16 operand, Enu::EAddrMode addrMode,
                               bool (AMemoryDevice::*readFunc)(quint16, quint16 &) const,
                               quint16 &opVal)
{
    bool rVal = true;
    quint16 effectiveAddress = 0;
    switch(addrMode) {
    case Enu::EAddrMode::I:
        opVal = operand;
        break;
    case Enu::EAddrMode::D:
        effectiveAddress = operand;
        rVal  = std::invoke(readFunc, memory.get(), effectiveAddress, opVal);
        break;
    case Enu::EAddrMode::S:
        effectiveAddress = operand + getCPURegWordCurrent(Enu::CPURegisters::SP);
        rVal  = std::invoke(readFunc, memory.get(), effectiveAddress, opVal);
        break;
    case Enu::EAddrMode::X:
        effectiveAddress = operand + getCPURegWordCurrent(Enu::CPURegisters::X);
        rVal  = std::invoke(readFunc, memory.get(), effectiveAddress, opVal);
        break;
    case Enu::EAddrMode::SX:
        effectiveAddress = operand
                + getCPURegWordCurrent(Enu::CPURegisters::SP)
                + getCPURegWordCurrent(Enu::CPURegisters::X);
        rVal  = std::invoke(readFunc, memory.get(), effectiveAddress, opVal);
        break;
    case Enu::EAddrMode::N:
        effectiveAddress = operand;
        rVal  = std::invoke(readFunc, memory.get(), effectiveAddress, effectiveAddress);
        rVal  &= std::invoke(readFunc, memory.get(), effectiveAddress, opVal);
        break;
    case Enu::EAddrMode::SF:
        effectiveAddress = operand + getCPURegWordCurrent(Enu::CPURegisters::SP);
        rVal  = std::invoke(readFunc, memory.get(), effectiveAddress, effectiveAddress);
        rVal  &= std::invoke(readFunc, memory.get(), effectiveAddress, opVal);
        break;
    case Enu::EAddrMode::SFX:
        effectiveAddress = operand + getCPURegWordCurrent(Enu::CPURegisters::SP);
        rVal  = std::invoke(readFunc, memory.get(), effectiveAddress, effectiveAddress);
        effectiveAddress += getCPURegWordCurrent(Enu::CPURegisters::X);
        rVal  &= std::invoke(readFunc, memory.get(), effectiveAddress, opVal);
        break;
    default:
        break;
    }
    return rVal;
}

void ISACPU::executeUnary(Enu::EMnemonic mnemon)
{
    quint16 temp, sp, pc, acc, idx;
    sp = registerBank.readRegisterWordCurrent(Enu::CPURegisters::SP);
    acc = registerBank.readRegisterWordCurrent(Enu::CPURegisters::A);
    idx = registerBank.readRegisterWordCurrent(Enu::CPURegisters::X);
    quint8 nzvc = registerBank.readStatusBitsCurrent();
    /*
     * All status bit calculations take the form
     * nzvc = (nzvc & ~MASK) | ((some & calculation) * MASK); // Some comment
     *
     * The "(nzvc & ~MASK)" masks out only the bit about to be set.
     * The "(some & calculation)" calculate the correct boolean value for the bit.
     * The " * MASK" then shifts it into the proper position, before being |'ed with
     * the NZVC bits (with the original bit already masked out).
     */
    switch(mnemon) {
    case Enu::EMnemonic::STOP:
        executionFinished = true;
        break;

    case Enu::EMnemonic::RET:
        memory->readWord(sp, temp);
        registerBank.writeRegisterWord(Enu::CPURegisters::PC, temp);
        sp += 2;
        registerBank.writeRegisterWord(Enu::CPURegisters::SP, sp);
        break;

    case Enu::EMnemonic::RETTR:
#pragma message("TODO")
        break;

    case Enu::EMnemonic::MOVSPA:
        registerBank.writeRegisterWord(Enu::CPURegisters::A, sp);
        break;

    case Enu::EMnemonic::MOVFLGA:
        registerBank.writeRegisterWord(Enu::CPURegisters::A, registerBank.readStatusBitsCurrent());
        break;

    case Enu::EMnemonic::MOVAFLG:
        // Only move the low order byte of accumulator to the status bits.
        registerBank.writeStatusBits(static_cast<quint8>(acc));
        break;

    case Enu::EMnemonic::NOTA: //Modifies NZ bits
        acc = ~acc;
        registerBank.writeRegisterWord(Enu::CPURegisters::A, acc);
        nzvc = (nzvc & ~Enu::NMask) | ((acc & 0x8000) ? 1:0 * Enu::NMask); // Is negative if high order bit is 1.
        nzvc = (nzvc & ~Enu::ZMask) | ((acc == 0) * Enu::ZMask); // Is zero if all bits are 0's.
        break;

    case Enu::EMnemonic::NOTX: //Modifies NZ bits
        idx = ~idx;
        registerBank.writeRegisterWord(Enu::CPURegisters::X, idx);
        nzvc = (nzvc & ~Enu::NMask) | ((idx & 0x8000) ? 1:0 * Enu::NMask); // Is negative if high order bit is 1.
        nzvc = (nzvc & ~Enu::ZMask) | ((idx == 0) * Enu::ZMask); // Is zero if all bits are 0's.
        break;

    case Enu::EMnemonic::NEGA: //Modifies NZV bits
        acc = ~acc + 1;
        registerBank.writeRegisterWord(Enu::CPURegisters::A, acc);
        nzvc = (nzvc & ~Enu::NMask) | ((acc & 0x8000) ? 1:0 * Enu::NMask); // Is negative if high order bit is 1.
        nzvc = (nzvc & ~Enu::ZMask) | ((acc == 0) * Enu::ZMask); // Is zero if all bits are 0's.
        nzvc = (nzvc & ~Enu::VMask) | ((acc == 0x8000) * Enu::VMask); // Only a signed overflow if accumulator is 0x8000.
        break;

    case Enu::EMnemonic::NEGX: //Modifies NZV bits
        idx = ~idx + 1;
        registerBank.writeRegisterWord(Enu::CPURegisters::X, idx);
        nzvc = (nzvc & ~Enu::NMask) | ((idx & 0x8000) ? 1:0 * Enu::NMask); // Is negative if high order bit is 1.
        nzvc = (nzvc & ~Enu::ZMask) | ((idx == 0) * Enu::ZMask); // Is zero if all bits are 0's.
        nzvc = (nzvc & ~Enu::VMask) | ((idx == 0x8000) * Enu::VMask); // Only a signed overflow if index reg is 0x8000.
        break;

    // Arithmetic shift instructions
    case Enu::EMnemonic::ASLA: //Modifies NZVC bits
        temp = static_cast<quint16>(acc << 1);
        registerBank.writeRegisterWord(Enu::CPURegisters::A, temp);
        nzvc = (nzvc & ~Enu::NMask) | ((temp & 0x8000) ? 1:0 * Enu::NMask); // Is negative if high order bit is 1.
        nzvc = (nzvc & ~Enu::ZMask) | ((temp == 0) * Enu::ZMask); // Is zero if all bits are 0's.
        // Signed overflow occurs when the starting & ending values of the high order bit differ (a xor temp == 1).
        // Then shift the result over by 15 places to only keep high order bit (which is the sign).
        nzvc = (nzvc & ~Enu::VMask) | (((acc ^ temp) >> 15) ? 1:0 * Enu::VMask);
        // Carry out if accumulator starts with high order 1.
        nzvc = (nzvc & ~Enu::CMask) | ((acc & 0x8000) ? 1:0 * Enu::CMask);
        break;

    case Enu::EMnemonic::ASLX: //Modifies NZVC bits
        temp = static_cast<quint16>(idx << 1);
        registerBank.writeRegisterWord(Enu::CPURegisters::X, idx);
        nzvc = (nzvc & ~Enu::NMask) | ((temp & 0x8000) ? 1:0 * Enu::NMask); // Is negative if high order bit is 1.
        nzvc = (nzvc & ~Enu::ZMask) | ((temp == 0) * Enu::ZMask); // Is zero if all bits are 0's.
        // Signed overflow occurs when the starting & ending values of the high order bit differ (a xor temp == 1).
        // Then shift the result over by 15 places to only keep high order bit (which is the sign).
        nzvc = (nzvc & ~Enu::VMask) | (((idx ^ temp) >> 15) ? 0 : 1 * Enu::VMask);
        // Carry out if index reg starts with high order 1.
        nzvc = (nzvc & ~Enu::CMask) | ((idx & 0x8000) ? 1:0 * Enu::CMask);
        break;

    case Enu::EMnemonic::ASRA: //Modifies NZC bits
        // Shift all bits to the right by 1 position. Since using unsigned shift, must explicitly
        // perform sign extension by hand.
        temp = static_cast<quint16>(acc >> 1 |
                                    // If the high order bit is 1, then sign extend with 1, else 0.
                                    ((acc & 0x8000) ? 1:0) << 15 );
        registerBank.writeRegisterWord(Enu::CPURegisters::A, temp);
        nzvc = (nzvc & ~Enu::NMask) | ((temp & 0x8000) ? 1:0 * Enu::NMask); // Is negative if high order bit is 1.
        nzvc = (nzvc & ~Enu::ZMask) | ((temp == 0) * Enu::ZMask); // Is zero if all bits are 0's.
        nzvc = (nzvc & ~Enu::CMask) | ((acc & 0x1) ? 1:0 * Enu::CMask); // Carry out if accumulator starts with low order 1.
        break;

    case Enu::EMnemonic::ASRX: //Modifies NZC bits
        // Shift all bits to the right by 1 position. Since using unsigned shift, must explicitly
        // perform sign extension by hand.
        temp = static_cast<quint16>(idx >> 1 |
                                    // If the high order bit is 1, then sign extend with 1, else 0.
                                    ((idx & 0x8000) ? 1:0) << 15 );
        registerBank.writeRegisterWord(Enu::CPURegisters::X, temp);
        nzvc = (nzvc & ~Enu::NMask) | ((temp & 0x8000) ? 1:0 * Enu::NMask); // Is negative if high order bit is 1.
        nzvc = (nzvc & ~Enu::ZMask) | ((temp == 0) * Enu::ZMask); // Is zero if all bits are 0's.
        nzvc = (nzvc & ~Enu::CMask) | ((idx & 0x1) ? 1:0 * Enu::CMask); // Carry out if index reg starts with low order 1.
        break;

    #pragma message("Next to check")
    // Rotate instructions.
    case Enu::EMnemonic::RORA: //Modifies C bits
        temp = static_cast<quint16>(acc >> 1
                                    // Shift the carry in to correct position.
                                    | ((nzvc & Enu::CMask) ? 1:0 << 15 ));
        registerBank.writeRegisterWord(Enu::CPURegisters::A, temp);
        nzvc = (nzvc & ~Enu::CMask) | ((acc & 0x01) ? 1:0 * Enu::CMask); // Carry out if accumulator starts with low order 1.
        break;

    case Enu::EMnemonic::RORX: //Modifies C bit
        temp = static_cast<quint16>(idx >> 1
                                    // Shift the carry in to correct position.
                                    | ((nzvc & Enu::CMask) ? 1:0 << 15 ));
        registerBank.writeRegisterWord(Enu::CPURegisters::X, temp);
        nzvc = (nzvc & ~Enu::CMask) | ((idx & 0x01) ? 1:0 * Enu::CMask); // Carry out if index reg starts with low order 1.
        break;

    case Enu::EMnemonic::ROLA: // Modifies C bit
        temp = static_cast<quint16>(acc << 1
                                    // Bring in carry to low order bit.
                                    | ((nzvc & Enu::CMask) ? 1:0));
        registerBank.writeRegisterWord(Enu::CPURegisters::A, temp);
        nzvc = (nzvc & ~Enu::CMask) | ((acc & 0x8000) ? 1:0 * Enu::CMask); // Carry out if accumulator starts with high order 1
        break;

    case Enu::EMnemonic::ROLX: // Modifies C bit
        temp = static_cast<quint16>(acc << 1
                                    // Bring in carry to low order bit.
                                    | ((nzvc & Enu::CMask) ? 1:0));
        registerBank.writeRegisterWord(Enu::CPURegisters::A, temp);
        nzvc = (nzvc & ~Enu::CMask) | ((acc & 0x800) ? 1:0 * Enu::CMask); // Carry out if index reg starts with high order 1
        break;
    default:
        // Should never occur, but gaurd against to make compiler happy.
        return;
    }
    registerBank.writeStatusBits(nzvc);
}

void ISACPU::executeNonunary(Enu::EMnemonic mnemon, quint16 opSpec, Enu::EAddrMode addrMode)
{
    #pragma message("TODO")
}

void ISACPU::executeTrap(Enu::EMnemonic mnemon)
{
    quint16 pc;
    // The
    quint16 tempAddr = manager->getOperatingSystem()->getBurnValue() - 9;
    quint16 pcAddr = manager->getOperatingSystem()->getBurnValue() - 1;
    switch(mnemon) {
    // Non-unary traps
    case Enu::EMnemonic::NOP:;
        [[fallthrough]];
    case Enu::EMnemonic::DECI:;
        [[fallthrough]];
    case Enu::EMnemonic::DECO:;
        [[fallthrough]];
    case Enu::EMnemonic::HEXO:;
        [[fallthrough]];
    case Enu::EMnemonic::STRO:;
        // though not part of the specification, the Pep9 hardware must increment the program counter
        // in order for non-unary traps to function correctly
        pc = registerBank.readRegisterWordCurrent(Enu::CPURegisters::PC) + 2;
        registerBank.writeRegisterWord(Enu::CPURegisters::PC, pc);
        [[fallthrough]];
    // Unary traps
    case Enu::EMnemonic::NOP0:;
        [[fallthrough]];
    case Enu::EMnemonic::NOP1:;
#pragma message("These offsets don't seem right...")
        memory->writeByte(tempAddr - 1, registerBank.readRegisterByteCurrent(Enu::CPURegisters::IS) /*IS*/);
        memory->writeWord(tempAddr - 3, registerBank.readRegisterWordCurrent(Enu::CPURegisters::SP) /*SP*/);
        memory->writeWord(tempAddr - 5, registerBank.readRegisterWordCurrent(Enu::CPURegisters::PC) /*PC*/);
        memory->writeWord(tempAddr - 7, registerBank.readRegisterWordCurrent(Enu::CPURegisters::X) /*X*/);
        memory->writeWord(tempAddr - 9, registerBank.readRegisterWordCurrent(Enu::CPURegisters::A) /*A*/);
        memory->writeWord(tempAddr - 10, registerBank.readRegisterWordCurrent(Enu::CPURegisters::PC) /*NZVC*/);
        memory->readWord(pcAddr, pc);
        registerBank.writeRegisterWord(Enu::CPURegisters::SP, tempAddr - 10);
        registerBank.writeRegisterWord(Enu::CPURegisters::PC, pc);
        // Though not part of the specification, clear out the index register to
        // prevent bug in OS where non-unary instructions fail due to junk
        // in the high order byte of the index register.
        registerBank.writeRegisterWord(Enu::CPURegisters::X, 0);
    }
}
