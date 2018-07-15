#include "iowidget.h"
#include "ui_iowidget.h"
#include "memorysection.h"
#include <QString>
#include <QDebug>
IOWidget::IOWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::IOWidget)
{
    ui->setupUi(this);
}

IOWidget::~IOWidget()
{
    delete ui;
}

void IOWidget::bindToMemorySection(MemorySection *memory)
{
    connect(ui->terminalIO,&TerminalPane::inputReady,memory,&MemorySection::onAppendInBuffer);
    connect(memory,&MemorySection::charRequestedFromInput,this,&IOWidget::onDataRequested);
    connect(memory,&MemorySection::charWrittenToOutput,this,&IOWidget::onDataReceived);
    this->memory = memory;
}

void IOWidget::batchInputToBuffer()
{
    if(ui->tabWidget->currentIndex() == 0)
    {
        memory->onAppendInBuffer(ui->batchInput->toPlainText()+'\n');
    }
}

void IOWidget::onClear()
{
    ui->batchOutput->clearText();
    ui->terminalIO->clearTerminal();
}

void IOWidget::onFontChanged(QFont font)
{
    ui->batchInput->onFontChanged(font);
    ui->batchOutput->onFontChanged(font);
    ui->terminalIO->onFontChanged(font);
}

//quint8 called=0;
void IOWidget::onDataReceived(QChar data)
{
    QString oData = QString::number((quint8)data.toLatin1(),16).leftJustified(2,'0')+" ";
    //qDebug()<<called++;
    switch(ui->tabWidget->currentIndex())
    {
    case 0:
        ui->batchOutput->appendOutput(QString(data));
        break;
    case 1:
        ui->terminalIO->appendOutput(QString(data));
        break;
    default:
        break;
    }
}

void IOWidget::onDataRequested()
{
    switch(ui->tabWidget->currentIndex())
    {
    case 0:
        //If there's no input for the memory, there never will be.
        //So, let the simulation begin to error and unwind.
        memory->onCancelWaiting();
        break;
    case 1:
        ui->terminalIO->waitingForInput();
    default:
        break;
    }
}

void IOWidget::onSimulationStart()
{
    switch(ui->tabWidget->currentIndex())
    {
    case 0:
        //When the simulation starts, pass all needed input to memory's input buffer
        memory->onAppendInBuffer(ui->batchInput->toPlainText().append('\n'));
        break;
    default:
        break;
    }
}

