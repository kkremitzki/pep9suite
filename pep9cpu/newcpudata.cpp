#include "newcpudata.h"
#include "microcode.h"
#include "microcodeprogram.h"
#include "amemorydevice.h"

NewCPUDataSection::NewCPUDataSection(Enu::CPUType type, QSharedPointer<AMemoryDevice> memDev, QObject *parent): QObject(parent), memDevice(memDev),
    cpuFeatures(type), mainBusState(Enu::None),
    registerBank(32), memoryRegisters(6), controlSignals(20), clockSignals(10), emitEvents(true), hadDataError(false), errorMessage(""),
    isALUCacheValid(false), ALUHasOutputCache(false), ALUOutputCache(0), ALUStatusBitCache(0)
{
    presetStaticRegisters();
}

NewCPUDataSection::~NewCPUDataSection()
{
    //This code should not be called during the normal lifetime of Pep9CPU
}

bool NewCPUDataSection::aluFnIsUnary() const
{
    //The only alu functions that are unary are 0 & 10..15
    return controlSignals[Enu::ALU] == 0 || controlSignals[Enu::ALU] >= 10;
}

bool NewCPUDataSection::getAMuxOutput(quint8& result) const
{
        if(controlSignals[Enu::AMux] == 0 && cpuFeatures == Enu::CPUType::TwoByteDataBus) {
            //Which could come from MDRE when EOMux is 0
            if(controlSignals[Enu::EOMux] == 0) {
                result = memoryRegisters[Enu::MEM_MDRE];
                return true;
            }
            //Or comes from MDRO if EOMux is 1
            else if(controlSignals[Enu::EOMux] == 1) {
                result = memoryRegisters[Enu::MEM_MDRO];
                return true;
            }
            //Or has no has no output when EOMux is disabled
            else return false;

        }
        else if(controlSignals[Enu::AMux] == 0 && cpuFeatures == Enu::CPUType::OneByteDataBus) {
            result = memoryRegisters[Enu::MEM_MDR];
            return true;
        }
        else if(controlSignals[Enu::AMux] == 1) {
            return valueOnABus(result);
        }
        else return false;
}

bool NewCPUDataSection::calculateCSMuxOutput(bool &result) const
{
    //CSMux either outputs C when CS is 0
    if(controlSignals[Enu::CSMux]==0) {
        result = NZVCSbits & Enu::CMask;
        return true;
    }
    //Or outputs S when CS is 1
    else if(controlSignals[Enu::CSMux] == 1)  {
        result = NZVCSbits & Enu::SMask;
        return true;
    }
    //Otherwise it does not have valid output
    else return false;
}

bool NewCPUDataSection::calculateALUOutput(quint8 &res, quint8 &NZVC) const
{
    /*
     * Profiling determined calculateALUOutput(...) to be the most computationally intensive part of the program.
     * This function is used multiple times per cycle, so we cache the result for increased performance.
     */
    if(isALUCacheValid) {
        res = ALUOutputCache;
        NZVC = ALUStatusBitCache;
        return ALUHasOutputCache;
    }
    // This function should not set any errors.
    // Errors will be handled by step(..)
    quint8 a, b;
    bool carryIn = false;
    bool hasA = getAMuxOutput(a), hasB = valueOnBBus(b);
    bool hasCIn = calculateCSMuxOutput(carryIn);
    if(!((aluFnIsUnary() && hasA) || (hasA && hasB))) {
        // The ALU output calculation would not be meaningful given its current function and inputs
        isALUCacheValid = true;
        ALUHasOutputCache = false;
        return ALUHasOutputCache;
    }
    // Unless otherwise noted, do not return true (sucessfully) early, or the calculation for the NZ bits will be skipped.
    switch(controlSignals[Enu::ALU]) {
    case Enu::A_func: // A
        res = a;
        break;
    case Enu::ApB_func: // A plus B
        res = a + b;
        NZVC |= Enu::CMask * quint8{res<a||res<b}; // Carry out if result is unsigned less than a or b.
        // There is a signed overflow iff the high order bits of the input are the same,
        // and the inputs & output differs in sign.
        // Shifts in 0's (unsigned chars), so after shift, only high order bit remain.
        NZVC |= Enu::VMask * ((~(a ^ b) & (a ^ res)) >> 7) ;
        break;
    case Enu::ApnBp1_func: // A plus ~B plus 1
        hasCIn = true;
        carryIn = 1;
        [[fallthrough]];
    case Enu::ApnBpCin_func: // A plus ~B plus Cin
        b = ~b;
        [[fallthrough]];
    case Enu::ApBpCin_func: // A plus B plus Cin
        // Expected carry in, none was provided, so ALU calculation yeilds a meaningless result
        if (!hasCIn) return false;
        // Might cause overflow, but overflow is well defined for unsigned ints
        res = a + b + quint8{carryIn};
        NZVC |= Enu::CMask * quint8{res<a||res<b}; // Carry out if result is unsigned less than a or b.
        // There is a signed overflow iff the high order bits of the input are the same,
        // and the inputs & output differs in sign.
        // Shifts in 0's (unsigned chars), so after shift, only high order bit remain.
        NZVC |= Enu::VMask * ((~(a ^ b) & (a ^ res)) >> 7) ;
        break;
    case Enu::AandB_func: // A * B
        res = a & b;
        break;
    case Enu::nAandB_func: // ~(A * B)
        res = ~(a & b);
        break;
    case Enu::AorB_func: // A + B
        res = a | b;
        break;
    case Enu::nAorB_func: // ~(A + B)
        res = ~(a | b);
        break;
    case Enu::AxorB_func: // A xor B
        res = a ^ b;
        break;
    case Enu::nA_func: // ~A
        res = ~a;
        break;
    case Enu::ASLA_func: // ASL A
        res = static_cast<quint8>(a<<1);
        NZVC |= Enu::CMask * ((a & 0x80) >> 7); // Carry out equals the hi order bit
        NZVC |= Enu::VMask * (((a << 1) ^a) >>7); // Signed overflow if a<hi> doesn't match a<hi-1>
        break;
    case Enu::ROLA_func: // ROL A
        if (!hasCIn) return false;
        res = static_cast<quint8>(a<<1 | quint8{carryIn});
        NZVC |= Enu::CMask * ((a & 0x80) >> 7); // Carry out equals the hi order bit
        NZVC |= Enu::VMask * (((a << 1) ^a) >>7); // Signed overflow if a<hi> doesn't match a<hi-1>
        break;
    case Enu::ASRA_func: // ASR A
        hasCIn = true;
        carryIn = a & 128; // RORA and ASRA only differ by how the carryIn is calculated
        [[fallthrough]];
    case Enu::RORA_func: // ROR a
        if (!hasCIn) return false;
        // A will not be sign extended since it is unsigned.
        // Widen carryIn so that << yields a meaningful result.
        res = static_cast<quint8>(a >> 1 | static_cast<quint8>(carryIn) << 7);
        // Carry out is lowest order bit of a
        NZVC |= Enu::CMask * (a & 1);
        break;
    case Enu::NZVCA_func: // Move A to NZVC
        res = 0;
        NZVC |= Enu::NMask & a;
        NZVC |= Enu::ZMask & a;
        NZVC |= Enu::VMask & a;
        NZVC |= Enu::CMask & a;
        return true; // Must return early to avoid NZ calculation
    default: // If the default has been hit, then an invalid function was selected
        return false;
    }
    // Calculate N, then shift to correct position
    NZVC |= (res & 0x80) ? Enu::NMask : 0; // Result is negative if high order bit is 1
    // Calculate Z, then shift to correct position
    NZVC |= (res == 0) ? Enu::ZMask : 0;
    // Save the result of the ALU calculation
    ALUOutputCache = res;
    ALUStatusBitCache = NZVC;
    isALUCacheValid = true;
    ALUHasOutputCache = true;
    return ALUHasOutputCache;

}

Enu::CPUType NewCPUDataSection::getCPUType() const
{
    return cpuFeatures;
}

quint8 NewCPUDataSection::getRegisterBankByte(quint8 registerNumber) const
{
    quint8 rval;
    if(registerNumber>Enu::maxRegisterNumber) return 0;
    else rval = registerBank[registerNumber];
    return rval;

}

quint16 NewCPUDataSection::getRegisterBankWord(quint8 registerNumber) const
{
    quint16 returnValue;
    if(registerNumber + 1 > Enu::maxRegisterNumber) returnValue = 0;
    else {
        returnValue = static_cast<quint16>(quint16{registerBank[registerNumber]} << 8);
        returnValue |= registerBank[registerNumber + 1];
    }
    return returnValue;

}

quint8 NewCPUDataSection::getRegisterBankByte(Enu::CPURegisters registerNumber) const
{
    // Call the overloaded version taking a quint8
    return getRegisterBankByte(static_cast<quint8>(registerNumber));
}

quint16 NewCPUDataSection::getRegisterBankWord(Enu::CPURegisters registerNumber) const
{
    // Call the overloaded version taking a quint8
    return getRegisterBankWord(static_cast<quint8>(registerNumber));
}

quint8 NewCPUDataSection::getMemoryRegister(Enu::EMemoryRegisters registerNumber) const
{
    return memoryRegisters[registerNumber];
}

bool NewCPUDataSection::valueOnABus(quint8 &result) const
{
    if(controlSignals[Enu::A] == Enu::signalDisabled) return false;
    result = getRegisterBankByte(controlSignals[Enu::A]);
    return true;
}

bool NewCPUDataSection::valueOnBBus(quint8 &result) const
{
    if(controlSignals[Enu::B] == Enu::signalDisabled) return false;
    result = getRegisterBankByte(controlSignals[Enu::B]);
    return true;
}

bool NewCPUDataSection::valueOnCBus(quint8 &result) const
{
    if(controlSignals[Enu::CMux] == 0) {
        // If CMux is 0, then the NZVC bits (minus S) are directly routed to result
        result = (NZVCSbits & (~Enu::SMask));
        return true;
    }
    else if(controlSignals[Enu::CMux] == 1) {
        quint8 temp = 0; // Discard NZVC bits for this calculation, they are unecessary for calculating C's output
        // Otherwise the value of C depends solely on the ALU
        return calculateALUOutput(result, temp);
    }
    else return false;
}

Enu::MainBusState NewCPUDataSection::getMainBusState() const
{
    return mainBusState;
}

bool NewCPUDataSection::getStatusBit(Enu::EStatusBit statusBit) const
{
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

void NewCPUDataSection::onSetStatusBit(Enu::EStatusBit statusBit, bool val)
{
    bool oldVal = false;
    int mask = 0;
    switch(statusBit)
    {
    case Enu::STATUS_N:
        mask = Enu::NMask;
        break;
    case Enu::STATUS_Z:
        mask = Enu::ZMask;
        break;
    case Enu::STATUS_V:
        mask = Enu::VMask;
        break;
    case Enu::STATUS_C:
        mask = Enu::CMask;
        break;
    case Enu::STATUS_S:
        mask = Enu::SMask;
        break;
    default:
        // Should never occur, but might happen if a bad status bit is passed
        return;
    }

    // Mask out the original value, then or it with the properly shifted bit
    oldVal = NZVCSbits&mask;
    NZVCSbits = (NZVCSbits&~mask) | ((val?1:0)*mask);
    if(emitEvents) {
        if(oldVal != val) emit statusBitChanged(statusBit, val);
    }
}

void NewCPUDataSection::onSetRegisterByte(quint8 reg, quint8 val)
{
    if(reg > 21) return; // Don't allow static registers to be written to
    quint8 oldVal = registerBank[reg];
    registerBank[reg] = val;
    if(emitEvents) {
        if(oldVal != val) emit registerChanged(reg, oldVal, val);
    }
}

void NewCPUDataSection::onSetRegisterWord(quint8 reg, quint16 val)
{
   if(reg + 1 >21) return; // Don't allow static registers to be written to
   quint8 oldHigh = registerBank[reg], oldLow = registerBank[reg+1];
   quint8 newHigh = val / 256, newLow = val & quint8{255};
   registerBank[reg] = newHigh;
   registerBank[reg + 1] = newLow;
   if(emitEvents) {
       if(oldHigh != val) emit registerChanged(reg, oldHigh, newHigh);
       if(oldLow != val) emit registerChanged(reg, oldLow, newLow);
   }
}

void NewCPUDataSection::onSetMemoryRegister(Enu::EMemoryRegisters reg, quint8 val)
{
    quint8 oldVal = memoryRegisters[reg];
    memoryRegisters[reg] = val;
    if(emitEvents) {
    if(oldVal != val) emit memoryRegisterChanged(reg, oldVal, val);
    }
}

void NewCPUDataSection::onSetClock(Enu::EClockSignals clock, bool value)
{
    clockSignals[clock] = value;
}

void NewCPUDataSection::onSetControlSignal(Enu::EControlSignals control, quint8 value)
{
    controlSignals[control] = value;
}

bool NewCPUDataSection::setSignalsFromMicrocode(const MicroCode *line)
{ 
    //To start with, there are no
    //For each control signal in the input line, set the appropriate control
    for(int it = 0; it < controlSignals.length(); it++) {
        controlSignals[it] = line->getControlSignal(static_cast<Enu::EControlSignals>(it));
    }
    //For each clock signal in the input microcode, set the appropriate clock
    for(int it = 0;it < clockSignals.length(); it++) {
        clockSignals[it] = line->getClockSignal((Enu::EClockSignals)it);
    }
    return true;
}

void NewCPUDataSection::setEmitEvents(bool b)
{
    emitEvents = b;
}

bool NewCPUDataSection::hadErrorOnStep() const
{
    return hadDataError;
}

QString NewCPUDataSection::getErrorMessage() const
{
    return errorMessage;
}

void NewCPUDataSection::handleMainBusState() noexcept
{
    bool marChanged = false;
    quint8 a, b;
    if(clockSignals[Enu::MARCk] && valueOnABus(a) && valueOnBBus(b)) {
        marChanged =! (a == memoryRegisters[Enu::MEM_MARA] && b == memoryRegisters[Enu::MEM_MARB]);
    }
    switch(mainBusState)
    {
    case Enu::None:
        //One cannot change MAR contents and initiate a R/W on same cycle
        if(!marChanged) {
            if(controlSignals[Enu::MemRead] == 1) mainBusState = Enu::MemReadFirstWait;
            else if(controlSignals[Enu::MemWrite] == 1) mainBusState = Enu::MemWriteFirstWait;
        }
        break;
    case Enu::MemReadFirstWait:
        if(!marChanged && controlSignals[Enu::MemRead] == 1) mainBusState = Enu::MemReadSecondWait;
        else if(marChanged && controlSignals[Enu::MemRead] == 1); //Initiating a new read brings us back to first wait
        else if(controlSignals[Enu::MemWrite] == 1) mainBusState = Enu::MemWriteFirstWait; //Switch from read to write.
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemReadSecondWait:
        if(!marChanged && controlSignals[Enu::MemRead] == 1) mainBusState = Enu::MemReadReady;
        else if(marChanged && controlSignals[Enu::MemRead] == 1) mainBusState = Enu::MemReadFirstWait;
        else if(controlSignals[Enu::MemWrite] == 1) mainBusState = Enu::MemWriteFirstWait;
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemReadReady:
        if(controlSignals[Enu::MemRead] == 1) mainBusState = Enu::MemReadFirstWait; //Another MemRead will bring us back to first MemRead, regardless of it MarChanged
        else if(controlSignals[Enu::MemWrite] == 1) mainBusState = Enu::MemWriteFirstWait;
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemWriteFirstWait:
        if(!marChanged && controlSignals[Enu::MemWrite] == 1) mainBusState = Enu::MemWriteSecondWait;
        else if(marChanged && controlSignals[Enu::MemWrite] == 1); //Initiating a new write brings us back to first wait
        else if(controlSignals[Enu::MemRead] == 1) mainBusState = Enu::MemReadFirstWait; //Switch from write to read.
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemWriteSecondWait:
        if(!marChanged && controlSignals[Enu::MemWrite] == 1) mainBusState = Enu::MemWriteReady;
        else if(marChanged && controlSignals[Enu::MemWrite] == 1) mainBusState = Enu::MemWriteFirstWait; //Initiating a new write brings us back to first wait
        else if(controlSignals[Enu::MemRead] == 1) mainBusState = Enu::MemReadFirstWait; //Switch from write to read.
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    case Enu::MemWriteReady:
        if(controlSignals[Enu::MemWrite]==1)mainBusState = Enu::MemWriteFirstWait; //Another MemWrite will reset the bus state back to first MemWrite
        else if(controlSignals[Enu::MemRead]==1) mainBusState = Enu::MemReadFirstWait; //Switch from write to read.
        else mainBusState = Enu::None; //If neither are check, bus goes back to doing nothing
        break;
    default:
        mainBusState = Enu::None;
        break;
    }
}

void NewCPUDataSection::stepOneByte() noexcept
{
    //Update the bus state first, as the rest of the read / write functionality depends on it
    handleMainBusState();
    if(hadErrorOnStep()) return; //If the bus had an error, give up now

    isALUCacheValid = false;
    //Set up all variables needed by stepping calculation
    Enu::EALUFunc aluFunc = (Enu::EALUFunc) controlSignals[Enu::ALU];
    quint8 a = 0, b = 0, c = 0, alu = 0, NZVC = 0;
    bool hasA = valueOnABus(a), hasB = valueOnBBus(b), hasC = valueOnCBus(c), statusBitError = false;
    bool hasALUOutput = calculateALUOutput(alu,NZVC);

    //Handle write to memory
    if(mainBusState == Enu::MemWriteReady)
    {
        quint16 address = (memoryRegisters[Enu::MEM_MARA]<<8)+memoryRegisters[Enu::MEM_MARB];
        memDevice->writeByte(address, memoryRegisters[Enu::MEM_MDR]);
    }

    //MARCk
    if(clockSignals[Enu::MARCk] && hasA && hasB) {
        onSetMemoryRegister(Enu::MEM_MARA, a);
        onSetMemoryRegister(Enu::MEM_MARB, b);
    }
    else if(clockSignals[Enu::MARCk]) {//Handle error where no data is present
        hadDataError = true;
        errorMessage = "No values on A & B during MARCk";
        return;
    }

    //LoadCk
    if(clockSignals[Enu::LoadCk]) {
        if(controlSignals[Enu::C] == Enu::signalDisabled) {
            hadDataError = true;
            errorMessage = "No destination register specified for LoadCk.";
        }
        else if(!hasC) {
            hadDataError = true;
            errorMessage = "No value on C Bus to clock in.";
        }
        else onSetRegisterByte(controlSignals[Enu::C], c);
    }
    quint16 address;
    quint8 value;
    //MDRCk
    if(clockSignals[Enu::MDRCk]) {
        switch(controlSignals[Enu::MDRMux]) {
        case 0: //Pick memory
            address = (memoryRegisters[Enu::MEM_MARA]<<8) + memoryRegisters[Enu::MEM_MARB];
            if(mainBusState != Enu::MemReadReady) {
                hadDataError = true;
                errorMessage = "No value from data bus to write to MDR";
            }
            else {
                memDevice->getByte(address, value);
                onSetMemoryRegister(Enu::MEM_MDR, value);
            }
            break;
        case 1: //Pick C Bus;
            if(!hasC) {
                hadDataError = true;
                errorMessage = "No value on C bus to write to MDR";
            }
            else onSetMemoryRegister(Enu::MEM_MDR,c);
            break;
        default:
            hadDataError = true;
            errorMessage = "No value to clock into MDR";
            break;
        }

    }

    //NCk
    if(clockSignals[Enu::NCk]) {
        if(aluFunc!=Enu::UNDEFINED_func && hasALUOutput) onSetStatusBit(Enu::STATUS_N,Enu::NMask & NZVC);
        else statusBitError = true;
    }

    //ZCk
    if(clockSignals[Enu::ZCk]) {
        if(aluFunc!=Enu::UNDEFINED_func && hasALUOutput)
        {
            if(controlSignals[Enu::AndZ]==0) {
                onSetStatusBit(Enu::STATUS_Z,Enu::ZMask & NZVC);
            }
            else if(controlSignals[Enu::AndZ]==1) {
                onSetStatusBit(Enu::STATUS_Z,(bool)(Enu::ZMask & NZVC) && getStatusBit(Enu::STATUS_Z));
            }
            else statusBitError = true;
        }
        else statusBitError = true;
    }

    //VCk
    if(clockSignals[Enu::VCk]) {
        if(aluFunc!=Enu::UNDEFINED_func && hasALUOutput) onSetStatusBit(Enu::STATUS_V,Enu::VMask & NZVC);
        else statusBitError = true;
    }

    //CCk
    if(clockSignals[Enu::CCk]) {
        if(aluFunc!=Enu::UNDEFINED_func && hasALUOutput) onSetStatusBit(Enu::STATUS_C,Enu::CMask & NZVC);
        else statusBitError = true;
    }

    //SCk
    if(clockSignals[Enu::SCk]) {
        if(aluFunc!=Enu::UNDEFINED_func && hasALUOutput) onSetStatusBit(Enu::STATUS_S,Enu::CMask & NZVC);
        else statusBitError = true;
    }

    if(statusBitError) {
        hadDataError = true;
        errorMessage = "ALU Error: No output from ALU to clock into status bits.";
    }

}

void NewCPUDataSection::stepTwoByte() noexcept
{
    //Update the bus state first, as the rest of the read / write functionality depends on it
    handleMainBusState();
    if(hadErrorOnStep()) return; //If the bus had an error, give up now

    isALUCacheValid = false;
    //Set up all variables needed by stepping calculation
    Enu::EALUFunc aluFunc = (Enu::EALUFunc) controlSignals[Enu::ALU];
    quint8 a = 0, b = 0, c = 0, alu = 0, NZVC = 0, temp = 0;
    quint16 address;
    bool memSigError = false, hasA = valueOnABus(a), hasB = valueOnBBus(b), hasC = valueOnCBus(c);
    bool statusBitError = false, hasALUOutput = calculateALUOutput(alu, NZVC);

    //Handle write to memory
    if(mainBusState == Enu::MemWriteReady) {
        quint16 address = (memoryRegisters[Enu::MEM_MARA]<<8) + memoryRegisters[Enu::MEM_MARB];
        address&=0xFFFE; //Memory access ignores lowest order bit
        memDevice->writeWord(address, memoryRegisters[Enu::MEM_MDRE]*256 + memoryRegisters[Enu::MEM_MDRO]);
    }

    //MARCk
    if(clockSignals[Enu::MARCk]) {
        if(controlSignals[Enu::MARMux] == 0) {
            //If MARMux is 0, route MDRE, MDRO to MARA, MARB
            onSetMemoryRegister(Enu::MEM_MARA, memoryRegisters[Enu::MEM_MDRE]);
            onSetMemoryRegister(Enu::MEM_MARB, memoryRegisters[Enu::MEM_MDRO]);
        }
        else if(controlSignals[Enu::MARMux] == 1 && hasA && hasB) {
            //If MARMux is 1, route A, B to MARA, MARB
            onSetMemoryRegister(Enu::MEM_MARA, a);
            onSetMemoryRegister(Enu::MEM_MARB,b );
        }
        else {  //Otherwise MARCk is high, but no data flows through MARMux
            hadDataError = true;
            errorMessage = "MARMux has no output but MARCk";
            return;
        }
    }

    //LoadCk
    if(clockSignals[Enu::LoadCk]) {
        if(controlSignals[Enu::C] == Enu::signalDisabled) {
            hadDataError = true;
            errorMessage = "No destination register specified for LoadCk.";
        }
        else if(!hasC) {
            hadDataError = true;
            errorMessage = "No value on C Bus to clock in.";
        }
        else onSetRegisterByte(controlSignals[Enu::C], c);
    }

    //MDRECk
    if(clockSignals[Enu::MDRECk]) {
        switch(controlSignals[Enu::MDREMux])
        {
        case 0: //Pick memory
            address = (memoryRegisters[Enu::MEM_MARA]<<8) + memoryRegisters[Enu::MEM_MARB];
            address &= 0xFFFE; //Memory access ignores lowest order bit
            if(mainBusState != Enu::MemReadReady){
                hadDataError = true;
                errorMessage = "No value from data bus to write to MDRE";
                return;
            }
            else {
                memSigError = memDevice->readByte(address, temp);
                if(!memSigError) {
                    #pragma message("TODO: Handle case where memory errors more gracefully")
                }
                onSetMemoryRegister(Enu::MEM_MDRE, temp);
            }
            break;
        case 1: //Pick C Bus;
            if(!hasC) {
                hadDataError=true;
                errorMessage = "No value on C bus to write to MDRE";
                return;
            }
            else onSetMemoryRegister(Enu::MEM_MDRE,c);
            break;
        default:
            hadDataError = true;
            errorMessage = "No value to clock into MDRE";
            break;
        }

    }

    //MDRECk
    if(clockSignals[Enu::MDROCk])
    {
        switch(controlSignals[Enu::MDROMux])
        {
        case 0: //Pick memory
            address = (memoryRegisters[Enu::MEM_MARA]<<8) + memoryRegisters[Enu::MEM_MARB];
            address &= 0xFFFE; //Memory access ignores lowest order bit
            address += 1;
            if(mainBusState != Enu::MemReadReady){
                hadDataError = true;
                errorMessage = "No value from data bus to write to MDRO";
                return;
            }
            else {
                memSigError = memDevice->readByte(address, temp);
                if(!memSigError) {
                    #pragma message("TODO: Handle case where memory errors more gracefully")
                }
                onSetMemoryRegister(Enu::MEM_MDRO, temp);
            }
            break;
        case 1: //Pick C Bus;
            if(!hasC) {
                hadDataError = true;
                errorMessage = "No value on C bus to write to MDRO";
                return;
            }
            else onSetMemoryRegister(Enu::MEM_MDRO, c);
            break;
        default:
            hadDataError = true;
            errorMessage = "No value to clock into MDRO";
            break;
        }

    }

    //NCk
    if(clockSignals[Enu::NCk]) {
        if(aluFunc != Enu::UNDEFINED_func && hasALUOutput) onSetStatusBit(Enu::STATUS_N, Enu::NMask & NZVC);
        else statusBitError = true;
    }

    //If no ALU output, don't set flags.
    //ZCk
    if(clockSignals[Enu::ZCk]) {
        if(aluFunc != Enu::UNDEFINED_func && hasALUOutput) {
            if(controlSignals[Enu::AndZ] == 0) {
                onSetStatusBit(Enu::STATUS_Z,Enu::ZMask & NZVC);
            }
            else if(controlSignals[Enu::AndZ] == 1) {
                onSetStatusBit(Enu::STATUS_Z, (bool)(Enu::ZMask & NZVC) && getStatusBit(Enu::STATUS_Z));
            }
            else statusBitError = true;
        }
        else statusBitError = true;
    }

    //VCk
    if(clockSignals[Enu::VCk]) {
        if(aluFunc != Enu::UNDEFINED_func && hasALUOutput) onSetStatusBit(Enu::STATUS_V, Enu::VMask & NZVC);
        else statusBitError = true;
    }

    //CCk
    if(clockSignals[Enu::CCk]) {
        if(aluFunc != Enu::UNDEFINED_func && hasALUOutput) onSetStatusBit(Enu::STATUS_C, Enu::CMask & NZVC);
        else statusBitError = true;
    }

    //SCk
    if(clockSignals[Enu::SCk]) {
        if(aluFunc != Enu::UNDEFINED_func && hasALUOutput) onSetStatusBit(Enu::STATUS_S, Enu::CMask & NZVC);
        else statusBitError = true;
    }

    if(statusBitError) {
        hadDataError = true;
        errorMessage = "ALU Error: No output from ALU to clock into status bits.";
    }
}

void NewCPUDataSection::presetStaticRegisters() noexcept
{
    // Pre-assign static registers according to CPU diagram
    registerBank[22] = 0x00;
    registerBank[23] = 0x01;
    registerBank[24] = 0x02;
    registerBank[25] = 0x03;
    registerBank[26] = 0x04;
    registerBank[27] = 0x08;
    registerBank[28] = 0xF0;
    registerBank[29] = 0xF6;
    registerBank[30] = 0xFE;
    registerBank[31] = 0xFF;
}

void NewCPUDataSection::clearControlSignals() noexcept
{
    //Set all control signals to disabled
    for(int it = 0; it < controlSignals.length(); it++) {
        controlSignals[it] = Enu::signalDisabled;
    }
}

void NewCPUDataSection::clearClockSignals() noexcept
{
    //Set all clock signals to low
    for(int it = 0; it < clockSignals.length(); it++) {
        clockSignals[it]=false;
    }
}

void NewCPUDataSection::clearRegisters() noexcept
{
    //Clear all registers in register bank, then restore the static values
    for(int it = 0; it < registerBank.length(); it++) {
        registerBank[it] = 0;
    }
    presetStaticRegisters();

     //Clear all values from memory registers
    for(int it = 0; it < memoryRegisters.length(); it++) {
        memoryRegisters[it] = 0;
    }
}

void NewCPUDataSection::clearErrors() noexcept
{
    hadDataError = false;
    errorMessage.clear();
}

void NewCPUDataSection::onStep() noexcept
{
    //If the error hasn't been handled by now, clear it
    clearErrors();
    if(cpuFeatures == Enu::OneByteDataBus) {
        stepOneByte();
    }
    else if(cpuFeatures == Enu::TwoByteDataBus) {
        stepTwoByte();
    }
}

void NewCPUDataSection::onClock() noexcept
{
    //When the clock button is pushed, execute whatever control signals are set, and the clear their values
    onStep();
    clearClockSignals();
    clearControlSignals();
}

void NewCPUDataSection::onClearCPU() noexcept
{
    //Reset evey value associated with the CPU
    mainBusState = Enu::None;
    NZVCSbits = 0;
    clearErrors();
    clearRegisters();
    clearClockSignals();
    clearControlSignals();
}

void NewCPUDataSection::onSetCPUType(Enu::CPUType type)
{
    if(cpuFeatures != type) {
       cpuFeatures = type;
       emit CPUTypeChanged(type);
    }

}

void NewCPUDataSection::setMemoryDevice(QSharedPointer<AMemoryDevice> newDevice)
{
    memDevice = newDevice;
}