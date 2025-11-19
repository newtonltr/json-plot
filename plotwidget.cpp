#include "plotwidget.h"

#include "datarepository.h"

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPointF>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

// Interactive chart widget that lets the operator select variables to plot.
PlotWidget::PlotWidget(DataRepository *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    buildUi();
    updateAxisRanges();
    updateCounterLabel();

    if (m_store) {
        connect(m_store, &DataRepository::packetAccepted, this, &PlotWidget::handlePacketAccepted);
        connect(m_store, &DataRepository::variablesChanged, this, &PlotWidget::handleVariablesChanged);
        handleVariablesChanged(m_store->orderedKeys());
    } else {
        refreshCandidates({});
    }
}

// Assemble the plotting controls and embed the Qt Charts view.
void PlotWidget::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);

    m_chart = new QChart();
    m_chart->setTitle(tr("Variable Plot"));
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);

    m_axisX = new QValueAxis();
    m_axisX->setTitleText(tr("counter"));
    m_axisX->setTickCount(10);

    m_axisY = new QValueAxis();
    m_axisY->setTitleText(tr("Value"));

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);

    m_startButton = new QPushButton(tr("Start Plotting"), this);
    m_stopButton = new QPushButton(tr("Stop Plotting"), this);
    m_clearButton = new QPushButton(tr("Clear"), this);
    m_stopButton->setEnabled(false);

    connect(m_startButton, &QPushButton::clicked, this, &PlotWidget::handleStart);
    connect(m_stopButton, &QPushButton::clicked, this, &PlotWidget::handleStop);
    connect(m_clearButton, &QPushButton::clicked, this, &PlotWidget::handleClear);

    m_windowMinSpin = new QDoubleSpinBox(this);
    m_windowMinSpin->setRange(0.0, 3600.0);
    m_windowMinSpin->setDecimals(1);
    m_windowMinSpin->setValue(0.0);
    m_windowMinSpin->setSuffix(tr(" s"));
    m_windowMinSpin->setToolTip(tr("Minimum sliding-window offset in seconds"));

    m_windowMaxSpin = new QDoubleSpinBox(this);
    m_windowMaxSpin->setRange(1.0, 7200.0);
    m_windowMaxSpin->setDecimals(1);
    m_windowMaxSpin->setValue(30.0);
    m_windowMaxSpin->setSuffix(tr(" s"));
    m_windowMaxSpin->setToolTip(tr("Maximum sliding-window offset in seconds"));

    connect(m_windowMinSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PlotWidget::handleWindowChanged);
    connect(m_windowMaxSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PlotWidget::handleWindowChanged);

    m_yMinEdit = new QLineEdit(this);
    m_yMinEdit->setPlaceholderText(tr("Auto"));
    m_yMaxEdit = new QLineEdit(this);
    m_yMaxEdit->setPlaceholderText(tr("Auto"));

    connect(m_yMinEdit, &QLineEdit::editingFinished, this, &PlotWidget::handleYAxisEdited);
    connect(m_yMaxEdit, &QLineEdit::editingFinished, this, &PlotWidget::handleYAxisEdited);

    m_candidateCombo = new QComboBox(this);
    m_candidateCombo->setMinimumWidth(150);
    m_addButton = new QPushButton(tr("Add"), this);
    m_removeButton = new QPushButton(tr("Remove Selected"), this);
    m_removeButton->setEnabled(false);

    connect(m_addButton, &QPushButton::clicked, this, &PlotWidget::handleAddVariable);
    connect(m_removeButton, &QPushButton::clicked, this, &PlotWidget::handleRemoveVariable);

    m_selectedList = new QListWidget(this);
    m_selectedList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_selectedList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_removeButton->setEnabled(!m_selectedList->selectedItems().isEmpty());
    });

    m_counterLabel = new QLabel(QStringLiteral("0"), this);

    auto *controlLayout = new QHBoxLayout();
    controlLayout->addWidget(m_startButton);
    controlLayout->addWidget(m_stopButton);
    controlLayout->addWidget(m_clearButton);
    controlLayout->addSpacing(15);
    controlLayout->addWidget(new QLabel(tr("Window Min"), this));
    controlLayout->addWidget(m_windowMinSpin);
    controlLayout->addWidget(new QLabel(tr("Window Max"), this));
    controlLayout->addWidget(m_windowMaxSpin);
    controlLayout->addSpacing(15);
    controlLayout->addWidget(new QLabel(tr("Y Min"), this));
    controlLayout->addWidget(m_yMinEdit);
    controlLayout->addWidget(new QLabel(tr("Y Max"), this));
    controlLayout->addWidget(m_yMaxEdit);
    controlLayout->addStretch();
    controlLayout->addWidget(new QLabel(tr("Plot Count"), this));
    controlLayout->addWidget(m_counterLabel);

    auto *variableLayout = new QHBoxLayout();
    variableLayout->addWidget(new QLabel(tr("Variable"), this));
    variableLayout->addWidget(m_candidateCombo);
    variableLayout->addWidget(m_addButton);
    variableLayout->addSpacing(10);
    variableLayout->addWidget(m_removeButton);
    variableLayout->addWidget(m_selectedList, 1);

    rootLayout->addLayout(controlLayout);
    rootLayout->addLayout(variableLayout);
    rootLayout->addWidget(m_chartView, 1);
}

// Consume repository updates and append points if plotting is active.
void PlotWidget::handlePacketAccepted(const QStringList &keys)
{
    Q_UNUSED(keys);

    if (!m_running || !m_store) {
        return;
    }

    ++m_plotCounter;
    updateCounterLabel();

    for (auto it = m_series.begin(); it != m_series.end(); ++it) {
        const DataRepository::VariableEntry *entry = m_store->entry(it.key());
        if (!entry || !entry->numeric) {
            continue;
        }
        appendPoint(it.key(), entry->numericValue);
    }

    updateAxisRanges();
}

// Refresh the candidate combo whenever new variables appear.
void PlotWidget::handleVariablesChanged(const QStringList &names)
{
    refreshCandidates(names);
}

// Begin plotting new points for selected variables.
void PlotWidget::handleStart()
{
    m_running = true;
    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);
}

// Pause plotting but retain accumulated state.
void PlotWidget::handleStop()
{
    m_running = false;
    m_startButton->setEnabled(true);
    m_stopButton->setEnabled(false);
}

// Clear all curves and reset counters to zero.
void PlotWidget::handleClear()
{
    if (m_running) {
        handleStop();
    }

    m_plotCounter = 0;
    for (auto it = m_series.begin(); it != m_series.end(); ++it) {
        if (it.value().series) {
            it.value().series->clear();
        }
        it.value().minY = 0.0;
        it.value().maxY = 0.0;
        it.value().hasData = false;
    }
    updateCounterLabel();
    updateAxisRanges();
}

// Add the currently selected variable to the chart.
void PlotWidget::handleAddVariable()
{
    const QString variable = m_candidateCombo->currentText().trimmed();
    if (variable.isEmpty() || m_series.contains(variable)) {
        return;
    }

    auto *series = new QLineSeries(this);
    series->setName(variable);
    m_chart->addSeries(series);
    series->attachAxis(m_axisX);
    series->attachAxis(m_axisY);

    SeriesInfo info;
    info.series = series;
    m_series.insert(variable, info);

    auto *item = new QListWidgetItem(variable);
    m_selectedList->addItem(item);
    m_selectedList->setCurrentItem(item);
    refreshCandidates(m_store ? m_store->orderedKeys() : QStringList{});
}

// Remove one or more variables that are currently selected in the list.
void PlotWidget::handleRemoveVariable()
{
    QList<QListWidgetItem *> items = m_selectedList->selectedItems();
    QStringList toRemove;
    for (QListWidgetItem *item : items) {
        toRemove.append(item->text());
    }

    for (const QString &name : toRemove) {
        removeSeries(name);
    }

    refreshCandidates(m_store ? m_store->orderedKeys() : QStringList{});
    updateAxisRanges();
}

// Keep axis ranges consistent when the sliding window edits change.
void PlotWidget::handleWindowChanged()
{
    if (m_windowMaxSpin->value() <= m_windowMinSpin->value()) {
        m_windowMaxSpin->setValue(m_windowMinSpin->value() + 1.0);
    }
    updateAxisRanges();
}

// React to manual Y min/max edits.
void PlotWidget::handleYAxisEdited()
{
    updateAxisRanges();
}

// Populate the combo box with variables not already plotted.
void PlotWidget::refreshCandidates(const QStringList &names)
{
    QStringList filtered;
    filtered.reserve(names.size());
    for (const QString &name : names) {
        if (!m_series.contains(name)) {
            filtered.append(name);
        }
    }

    const QString current = m_candidateCombo->currentText();
    m_candidateCombo->clear();
    m_candidateCombo->addItems(filtered);
    int idx = m_candidateCombo->findText(current);
    if (idx >= 0) {
        m_candidateCombo->setCurrentIndex(idx);
    } else if (!filtered.isEmpty()) {
        m_candidateCombo->setCurrentIndex(0);
    }

    const bool hasOptions = !filtered.isEmpty();
    m_candidateCombo->setEnabled(hasOptions);
    m_addButton->setEnabled(hasOptions);
}

// Compute axis windows either from user input or from observed data.
void PlotWidget::updateAxisRanges()
{
    double axisMax = std::max(1.0, static_cast<double>(m_plotCounter));
    double axisMin = 0.0;

    const double windowMin = m_windowMinSpin->value();
    const double windowMax = m_windowMaxSpin->value();

    if (windowMax > windowMin) {
        axisMax = std::max(1.0, static_cast<double>(m_plotCounter) - windowMin);
        axisMin = static_cast<double>(m_plotCounter) - windowMax;
        if (axisMin < 0.0) {
            axisMin = 0.0;
        }
        if (axisMax <= axisMin) {
            axisMax = axisMin + 1.0;
        }
    } else {
        axisMax = std::max(axisMax, 10.0);
    }

    m_axisX->setRange(axisMin, axisMax);

    const auto yMin = parseDouble(m_yMinEdit);
    const auto yMax = parseDouble(m_yMaxEdit);
    if (yMin && yMax && *yMax > *yMin) {
        m_axisY->setRange(*yMin, *yMax);
        return;
    }

    double minValue = std::numeric_limits<double>::max();
    double maxValue = std::numeric_limits<double>::lowest();
    bool hasData = false;
    for (auto it = m_series.cbegin(); it != m_series.cend(); ++it) {
        if (!it.value().hasData) {
            continue;
        }
        hasData = true;
        minValue = std::min(minValue, it.value().minY);
        maxValue = std::max(maxValue, it.value().maxY);
    }

    if (!hasData) {
        minValue = -1.0;
        maxValue = 1.0;
    } else if (minValue == maxValue) {
        const double padding = std::max(1.0, std::abs(minValue) * 0.1 + 0.5);
        minValue -= padding;
        maxValue += padding;
    }

    m_axisY->setRange(minValue, maxValue);
}

// Show how many packets have contributed to the plot.
void PlotWidget::updateCounterLabel()
{
    if (m_counterLabel) {
        m_counterLabel->setText(QString::number(m_plotCounter));
    }
}

// Append a new sample to the named line series.
void PlotWidget::appendPoint(const QString &name, double value)
{
    auto it = m_series.find(name);
    if (it == m_series.end() || !it.value().series) {
        return;
    }

    SeriesInfo &info = it.value();
    info.series->append(static_cast<qreal>(m_plotCounter), static_cast<qreal>(value));
    if (!info.hasData) {
        info.minY = info.maxY = value;
        info.hasData = true;
    } else {
        info.minY = std::min(info.minY, value);
        info.maxY = std::max(info.maxY, value);
    }

    const double lowerBound = std::max(0.0, static_cast<double>(m_plotCounter) - m_windowMaxSpin->value());
    const QList<QPointF> points = info.series->points();
    int removeCount = 0;
    for (const QPointF &point : points) {
        if (point.x() < lowerBound) {
            ++removeCount;
        } else {
            break;
        }
    }
    if (removeCount > 0) {
        info.series->removePoints(0, removeCount);
    }
}

// Remove series visuals and bookkeeping when the user deletes a variable.
void PlotWidget::removeSeries(const QString &name)
{
    auto it = m_series.find(name);
    if (it == m_series.end()) {
        return;
    }
    if (it.value().series) {
        m_chart->removeSeries(it.value().series);
        it.value().series->deleteLater();
    }
    m_series.erase(it);
    if (m_selectedList) {
        QList<QListWidgetItem *> items = m_selectedList->findItems(name, Qt::MatchExactly);
        qDeleteAll(items);
    }
}

// Try to parse a user-entered floating point number.
std::optional<double> PlotWidget::parseDouble(QLineEdit *edit) const
{
    if (!edit) {
        return std::nullopt;
    }
    const QString text = edit->text().trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return value;
}

