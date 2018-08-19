#include "asmtracepane.h"
#include "ui_asmtracepane.h"
#include "asmcode.h"
#include "pepasmhighlighter.h"
#include "asmprogram.h"
#include <QPainter>
AsmTracePane::AsmTracePane(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AsmTracePane)
{
    ui->setupUi(this);
    pepHighlighter = new PepASMHighlighter(PepColors::lightMode, ui->tracePaneTextEdit->document());
    ui->tracePaneTextEdit->setFont(QFont(Pep::codeFont, Pep::codeFontSize));
    connect(((AsmTraceTextEdit*)ui->tracePaneTextEdit), &AsmTraceTextEdit::breakpointAdded, this, &AsmTracePane::onBreakpointAddedProp);
    connect(((AsmTraceTextEdit*)ui->tracePaneTextEdit), &AsmTraceTextEdit::breakpointRemoved, this, &AsmTracePane::onBreakpointRemovedProp);
}

AsmTracePane::~AsmTracePane()
{
    delete ui;
}

void AsmTracePane::clearSourceCode()
{
    ui->tracePaneTextEdit->clear();
#pragma message("TODO: Check on activeProgram lifecycle")
    activeProgram = nullptr;
}

void AsmTracePane::highlightOnFocus()
{
}

bool AsmTracePane::hasFocus()
{
    return ui->tracePaneTextEdit->hasFocus();
}

void AsmTracePane::setProgram(QSharedPointer<AsmProgram> program)
{
    this->activeProgram = program;
    ui->tracePaneTextEdit->setTextFromCode(program);
}

void AsmTracePane::setBreakpoints(QSet<quint16> memAddresses)
{
    ui->tracePaneTextEdit->setBreakpoints(memAddresses);
}

const QSet<quint16> AsmTracePane::getBreakpoints() const
{
    return ui->tracePaneTextEdit->getBreakpoints();
}

void AsmTracePane::writeSettings(QSettings &)
{
}

void AsmTracePane::readSettings(QSettings &)
{

}

void AsmTracePane::onFontChanged(QFont font)
{
    ui->tracePaneTextEdit->setFont(font);
}

void AsmTracePane::onDarkModeChanged(bool darkMode)
{
    if(darkMode) pepHighlighter->rebuildHighlightingRules(PepColors::darkMode);
    else pepHighlighter->rebuildHighlightingRules(PepColors::lightMode);
    ((AsmTraceTextEdit*)ui->tracePaneTextEdit)->onDarkModeChanged(darkMode);
    pepHighlighter->rehighlight();
}

void AsmTracePane::onRemoveAllBreakpoints()
{
    ((AsmTraceTextEdit*)ui->tracePaneTextEdit)->onRemoveAllBreakpoints();
}

void AsmTracePane::onBreakpointAdded(quint16 line)
{
    ((AsmTraceTextEdit*)ui->tracePaneTextEdit)->onBreakpointAdded(line);
}

void AsmTracePane::onBreakpointRemoved(quint16 line)
{
    ((AsmTraceTextEdit*)ui->tracePaneTextEdit)->onBreakpointRemoved(line);
}


void AsmTracePane::mouseReleaseEvent(QMouseEvent *)
{

}

void AsmTracePane::mouseDoubleClickEvent(QMouseEvent *)
{

}

void AsmTracePane::onBreakpointAddedProp(quint16 line)
{
    emit breakpointAdded(line);
}

void AsmTracePane::onBreakpointRemovedProp(quint16 line)
{
    emit breakpointRemoved(line);
}

AsmTraceTextEdit::AsmTraceTextEdit(QWidget *parent): QPlainTextEdit(parent), colors(PepColors::lightMode)
{
    breakpointArea = new AsmTraceBreakpointArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, &AsmTraceTextEdit::updateBreakpointAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &AsmTraceTextEdit::updateBreakpointArea);
}

AsmTraceTextEdit::~AsmTraceTextEdit()
{
    delete breakpointArea;
}

void AsmTraceTextEdit::breakpointAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(breakpointArea);
    painter.fillRect(event->rect(), colors.lineAreaBackground); // light grey
    QTextBlock block;
    int blockNumber, top, bottom;
    block = firstVisibleBlock();
    blockNumber = block.blockNumber();
    top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
    bottom = top + (int) blockBoundingRect(block).height();
    // Store painter's previous Antialiasing hint so it can be restored at the end
    bool antialias = painter.renderHints() & QPainter::Antialiasing;
    painter.setRenderHint(QPainter::Antialiasing, true);
    while (block.isValid() && top < event->rect().bottom()) {
        // If the current block is in the repaint zone
        if (block.isVisible() && bottom >= event->rect().top()) {
            // And if it has a breakpoint
            if(lineToAddr.contains(blockNumber) && breakpoints.contains(blockNumber)) {
                painter.setPen(PepColors::transparent);
                painter.setBrush(colors.combCircuitRed);
                painter.drawEllipse(QPoint(fontMetrics().height()/2, top+fontMetrics().height()/2),
                                    fontMetrics().height()/2 -1, fontMetrics().height()/2 -1);
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + (int) blockBoundingRect(block).height();
        ++blockNumber;
    }
    painter.setRenderHint(QPainter::Antialiasing, antialias);
}

int AsmTraceTextEdit::breakpointAreaWidth()
{
    return 5 + fontMetrics().height();
}

void AsmTraceTextEdit::breakpointAreaMousePress(QMouseEvent *event)
{
    QTextBlock block;
    int blockNumber, top, bottom, lineNumber;
    block = firstVisibleBlock();
    blockNumber = block.blockNumber();
    top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
    bottom = top + (int) blockBoundingRect(block).height();
    // For each visible block (usually a line), iterate until the current block contains the location of the mouse event.
    while (block.isValid() && top <= event->pos().y()) {
        if (event->pos().y()>=top && event->pos().y()<=bottom) {
            // Check if the clicked line is a code line
            if(lineToAddr.contains(blockNumber)) {
                lineNumber = lineToAddr[blockNumber];
                // If the clicked code line has a breakpoint, remove it.
                if(breakpoints.contains(lineNumber)) {
                    breakpoints.remove(lineNumber);
                    emit breakpointRemoved(lineNumber);
                }
                // Otherwise add a breakpoint.
                else {
                    breakpoints.insert(lineNumber);
                    emit breakpointAdded(lineNumber);
                }
            }
            break;
        }

        block = block.next();
        top = bottom;
        bottom = top + (int) blockBoundingRect(block).height();
        ++blockNumber;
    }
    update();
}

const QSet<quint16> AsmTraceTextEdit::getBreakpoints() const
{
    return breakpoints;
}

void AsmTraceTextEdit::setTextFromCode(QSharedPointer<const AsmProgram> code)
{
    QStringList finList, traceList;
    int visualIt = 0;
    const AsmCode* codePtr;
    breakpoints.clear();
    for(int it = 0; it < code->numberOfLines(); it++)
    {
        codePtr = code->getCodeOnLine(it);
        traceList = codePtr->getAssemblerTrace().split("\n");
        if(dynamic_cast<const UnaryInstruction*>(codePtr) != nullptr ||
                dynamic_cast<const NonUnaryInstruction*>(codePtr) != nullptr)
        {
            lineToAddr[visualIt] = codePtr->getMemoryAddress();
            addrToLine[codePtr->getMemoryAddress()] = visualIt;
            if(codePtr->hasBreakpoint()) {
                onBreakpointAdded(codePtr->getMemoryAddress());

            }
        }
        finList.append(traceList);
        visualIt += traceList.length();
    }
    setPlainText(finList.join("\n"));
    update();
}

void AsmTraceTextEdit::setBreakpoints(QSet<quint16> memAddresses)
{
    breakpoints = memAddresses;
}

void AsmTraceTextEdit::highlightActiveLine()
{
    if(activeLine<0) return;
    else {
        qDebug() << "highlighted line" << addrToLine[activeLine];
    }
}

void AsmTraceTextEdit::setActiveLine(int line)
{
    activeLine = line;
}

void AsmTraceTextEdit::onRemoveAllBreakpoints()
{
    breakpoints.clear();
    update();
}

void AsmTraceTextEdit::onBreakpointAdded(quint16 line)
{
#pragma message("TOOD: check if line is in addrToLine")
    breakpoints.insert(addrToLine[line]);
}

void AsmTraceTextEdit::onBreakpointRemoved(quint16 line)
{
    breakpoints.remove(addrToLine[line]);
}

void AsmTraceTextEdit::onDarkModeChanged(bool darkMode)
{
    if(darkMode) colors = PepColors::darkMode;
    else colors = PepColors::lightMode;
}

void AsmTraceTextEdit::updateBreakpointAreaWidth(int )
{
    setViewportMargins(breakpointAreaWidth(), 0, 0, 0);
}

void AsmTraceTextEdit::updateBreakpointArea(const QRect &rect, int dy)
{
    if (dy)
        breakpointArea->scroll(0, dy);
    else
        breakpointArea->update(0, rect.y(), breakpointArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateBreakpointAreaWidth(0);
}

void AsmTraceTextEdit::resizeEvent(QResizeEvent *evt)
{
    QPlainTextEdit::resizeEvent(evt);

    QRect cr = contentsRect();
    breakpointArea->setGeometry(QRect(cr.left(), cr.top(), breakpointAreaWidth(), cr.height()));
}
