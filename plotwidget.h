#ifndef PLOTWIDGET_H
#define PLOTWIDGET_H

#include <QWidget>

#include <QtCharts/QChartGlobal>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <optional>
#include <QStringList>
#include <QMap>
#include <QtGlobal>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;

class DataRepository;

class PlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlotWidget(DataRepository *store, QWidget *parent = nullptr);

private slots:
    void handlePacketAccepted(const QStringList &keys);
    void handleVariablesChanged(const QStringList &names);
    void handleStart();
    void handleStop();
    void handleClear();
    void handleAddVariable();
    void handleRemoveVariable();
    void handleWindowChanged();
    void handleYAxisEdited();

private:
    struct SeriesInfo
    {
        QLineSeries *series = nullptr;
        double minY = 0.0;
        double maxY = 0.0;
        bool hasData = false;
    };

    void buildUi();
    void refreshCandidates(const QStringList &names);
    void updateAxisRanges();
    void updateCounterLabel();
    void appendPoint(const QString &name, double value);
    void removeSeries(const QString &name);
    std::optional<double> parseDouble(QLineEdit *edit) const;

    DataRepository *m_store = nullptr;

    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    QComboBox *m_candidateCombo = nullptr;
    QListWidget *m_selectedList = nullptr;
    QDoubleSpinBox *m_windowMinSpin = nullptr;
    QDoubleSpinBox *m_windowMaxSpin = nullptr;
    QLineEdit *m_yMinEdit = nullptr;
    QLineEdit *m_yMaxEdit = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QLabel *m_counterLabel = nullptr;

    QMap<QString, SeriesInfo> m_series;
    quint64 m_plotCounter = 0;
    bool m_running = false;
};

#endif // PLOTWIDGET_H
