#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    gameCanvas = new GameCanvas(this);
    QVBoxLayout *layout = new QVBoxLayout(ui->canvasContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(gameCanvas);

    connect(gameCanvas, &GameCanvas::statsUpdated, this, &MainWindow::onStatsUpdated);
    connect(gameCanvas, &GameCanvas::survivalTelemetryUpdated, this, &MainWindow::onSurvivalTelemetryUpdated);
    connect(ui->btnLevel1, &QPushButton::clicked, this, &MainWindow::on_btnLevel1_clicked);
    connect(ui->btnLevel2, &QPushButton::clicked, this, &MainWindow::on_btnLevel2_clicked);
    connect(ui->btnLevel3, &QPushButton::clicked, this, &MainWindow::on_btnLevel3_clicked);
    connect(ui->btnReset, &QPushButton::clicked, this, &MainWindow::on_btnReset_clicked);
    connect(ui->btnCelebration, &QPushButton::clicked, this, &MainWindow::on_btnCelebration_clicked);

    gameCanvas->setFocus();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_btnLevel1_clicked()
{
    gameCanvas->setStage(KolkataStage::GARIAHAT_COLLAPSE);
    gameCanvas->setFocus();
}

void MainWindow::on_btnLevel2_clicked()
{
    gameCanvas->setStage(KolkataStage::PARK_STREET_RUINS);
    gameCanvas->setFocus();
}

void MainWindow::on_btnLevel3_clicked()
{
    gameCanvas->setStage(KolkataStage::HOWRAH_BRIDGE);
    gameCanvas->setFocus();
}

void MainWindow::on_btnReset_clicked()
{
    gameCanvas->resetKart();
    gameCanvas->setFocus();
}

void MainWindow::on_btnCelebration_clicked()
{
    gameCanvas->setGameState(GameState::GARAGE);
    gameCanvas->setFocus();
}

void MainWindow::onStatsUpdated(double speed, int lap, double x, double z, double angle, QString surface)
{
    Q_UNUSED(lap);
    Q_UNUSED(angle);
    ui->lblSpeed->setText(QString("Speed: %1 km/h").arg(QString::number(speed, 'f', 1)));
    ui->lblPos->setText(QString("Pos: (%1, %2)").arg(static_cast<int>(x)).arg(static_cast<int>(z)));
    ui->lblSurface->setText(QString("Surface: %1").arg(surface));
}

void MainWindow::onSurvivalTelemetryUpdated(double hull, double evacTime, int rank, int totalRacers,
                                            int progressPct, int scrap, QString district)
{
    ui->lblLap->setText(QString("Hull: %1%  |  Evac: %2s").arg(static_cast<int>(hull)).arg(QString::number(evacTime, 'f', 1)));
    ui->lblHeading->setText(QString("Rank: %1/%2 (%3%)  •  ₹%4").arg(rank).arg(totalRacers).arg(progressPct).arg(scrap));
    ui->lblDistrict->setText(QString("District: %1").arg(district));
}
