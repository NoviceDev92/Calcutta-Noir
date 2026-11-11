#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "game_canvas.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_btnLevel1_clicked();
    void on_btnLevel2_clicked();
    void on_btnLevel3_clicked();
    void on_btnReset_clicked();
    void on_btnCelebration_clicked();
    void onStatsUpdated(double speed, int lap, double x, double z, double angle, QString surface);
    void onSurvivalTelemetryUpdated(double hull, double evacTime, int rank, int totalRacers,
                                    int progressPct, int scrap, QString district);

private:
    Ui::MainWindow *ui;
    GameCanvas *gameCanvas;
};

#endif // MAINWINDOW_H
